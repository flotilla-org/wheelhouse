//! A watched agent thread (#699): its stream, pumped into a Rich Text
//! conversation each frame, older pages asked for as the View nears them.
use super::*;
use std::collections::VecDeque;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use conversation_events::Event;
use wheelhouse_agent::{retry_after, Connection, Endpoint, Update, Watch};
use wheelhouse_rich_text::{RichText, SubscriptionId};

use crate::rich_text::WheelhouseRichText;

/// wheelhouse_agent_watch_pump's flags: the conversation changed.
pub const WHEELHOUSE_AGENT_CHANGED: u32 = 1;
/// The producer started over with a fresh snapshot: free the conversation,
/// open a new one and pump again.
pub const WHEELHOUSE_AGENT_RESTART: u32 = 2;

pub const WHEELHOUSE_AGENT_CONNECTING: u32 = 0;
pub const WHEELHOUSE_AGENT_OPEN: u32 = 1;
/// Dropped or refused for now; it connects again by itself.
pub const WHEELHOUSE_AGENT_RETRYING: u32 = 2;
/// Refused for good, as for a thread the producer doesn't have.
pub const WHEELHOUSE_AGENT_FAILED: u32 = 3;

/// Completed items asked for at a time.
const PAGE_ITEMS: u32 = 50;

pub struct WheelhouseAgentWatch {
    watch: Watch,
    pump: Pump,
    connection: Option<Connection>,
    poisoned: bool,
}

/// What the pump keeps between frames.
#[derive(Default)]
struct Pump {
    /// Updates taken and not yet pumped: those after a restart wait for
    /// the new conversation.
    queue: VecDeque<Update>,
    /// The conversation has had a hello, so a snapshot now is a restart.
    started: bool,
    /// An older page is on its way.
    asking: bool,
    /// Pages asked for by a conversation since replaced, to drop.
    stale_pages: u32,
    /// Pages failed since one came: the next waits longer.
    page_failures: u32,
}

impl Handle for WheelhouseAgentWatch {
    const NAME: &'static str = "agent watch";
    fn poisoned(&mut self) -> &mut bool {
        &mut self.poisoned
    }
}

/// Watches `thread` at the Local Endpoint whose socket is `endpoint`,
/// sharing its connection with every other watch and post of it. `wake`
/// (if not NULL) is called from another thread whenever updates wait.
/// Free it with wheelhouse_agent_watch_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_watch_open(
    endpoint: Text,
    thread: Text,
    wake: Option<extern "C" fn()>,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseAgentWatch {
    pure(error_out, || {
        let endpoint = Endpoint::shared(Path::new(&endpoint.read()?)).map_err(|error| error.to_string())?;
        let watch = endpoint.watch(&thread.read()?, Arc::new(move || wake.map_or((), |wake| wake())));
        Ok(Box::into_raw(Box::new(WheelhouseAgentWatch { watch, pump: Pump::default(), connection: None, poisoned: false })))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_watch_free(w: *mut WheelhouseAgentWatch) {
    if !w.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(w))));
    }
}

/// An empty conversation for wheelhouse_agent_watch_pump to feed. Free it
/// with wheelhouse_rich_text_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_watch_conversation(error_out: *mut *mut c_char) -> *mut WheelhouseRichText {
    pure(error_out, || Ok(Box::into_raw(Box::new(WheelhouseRichText::new(RichText::watch_conversation(None)))))).unwrap_or(ptr::null_mut())
}

/// Feeds `doc` what came since the last pump and returns
/// WHEELHOUSE_AGENT_* flags. Asks for older items while View `subscription`
/// reads near the start of what `doc` has.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_watch_pump(
    w: *mut WheelhouseAgentWatch,
    doc: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    let doc = match doc.as_mut() {
        Some(doc) => doc,
        None => return pure(error_out, || Err::<u32, _>("null rich text handle".into())).unwrap_or(0),
    };
    run(w, error_out, |w| {
        let updates = w.watch.take().filter_map(|update| match update {
            Update::Connection(connection) => {
                w.connection = Some(connection);
                None
            }
            update => Some(update),
        });
        let (flags, older) = w.pump.pump(updates, &mut doc.rich, SubscriptionId(subscription));
        if let Some((before, delay)) = older {
            w.watch.older(before, PAGE_ITEMS, delay);
        }
        Ok(flags)
    })
    .unwrap_or(0)
}

impl Pump {
    /// Feeds `rich` the updates and any queued before them; returns the
    /// flags, and the `seq` to ask for older items before and how long to
    /// wait first, if it is time.
    fn pump(
        &mut self,
        updates: impl Iterator<Item = Update>,
        rich: &mut RichText,
        subscription: SubscriptionId,
    ) -> (u32, Option<(u64, Duration)>) {
        let mut flags = 0;
        let mut events = vec![];
        self.queue.extend(updates);
        while let Some(update) = self.queue.pop_front() {
            match update {
                Update::Hello(hello) if hello.snapshot.is_some() && self.started => {
                    self.queue.push_front(Update::Hello(hello));
                    self.started = false;
                    self.stale_pages += u32::from(self.asking);
                    (self.asking, self.page_failures) = (false, 0);
                    return (flags | flush(&mut events, rich) | WHEELHOUSE_AGENT_RESTART, None);
                }
                Update::Hello(hello) => {
                    self.started = true;
                    events.push(Event::Hello(hello));
                }
                Update::Event(event) => events.push(event),
                Update::Page(_) if self.stale_pages > 0 => self.stale_pages -= 1,
                Update::Page(page) => {
                    flags |= flush(&mut events, rich);
                    self.asking = false;
                    match page {
                        Ok(page) => {
                            self.page_failures = 0;
                            flags |= u32::from(rich.receive_page(page)) * WHEELHOUSE_AGENT_CHANGED
                        }
                        Err(_) => self.page_failures += 1,
                    }
                }
                Update::Connection(_) => {}
            }
        }
        flags |= flush(&mut events, rich);
        let (earlier, oldest) = rich.conversation_earlier();
        let near = rich.view(subscription).is_ok_and(|view| view.near_start(rich.doc()));
        if earlier == 0 || !near || self.asking {
            return (flags, None);
        }
        self.asking = true;
        let delay = if self.page_failures == 0 { Duration::ZERO } else { retry_after(self.page_failures) };
        (flags, Some((oldest, delay)))
    }
}

fn flush(events: &mut Vec<Event>, rich: &mut RichText) -> u32 {
    if !events.is_empty() && rich.receive(std::mem::take(events)) {
        WHEELHOUSE_AGENT_CHANGED
    } else {
        0
    }
}

/// The watch's WHEELHOUSE_AGENT_CONNECTING..FAILED state; why it is
/// retrying or failed goes to `why_out`.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_watch_connection(
    w: *mut WheelhouseAgentWatch,
    why_out: *mut Bytes,
    error_out: *mut *mut c_char,
) -> u32 {
    run(w, error_out, |w| {
        Ok(match &w.connection {
            None => WHEELHOUSE_AGENT_CONNECTING,
            Some(Connection::Open) => WHEELHOUSE_AGENT_OPEN,
            Some(Connection::Retrying { why, .. }) => {
                write_bytes(why_out, why.clone().into_bytes())?;
                WHEELHOUSE_AGENT_RETRYING
            }
            Some(Connection::Failed(why)) => {
                write_bytes(why_out, why.clone().into_bytes())?;
                WHEELHOUSE_AGENT_FAILED
            }
        })
    })
    .unwrap_or(WHEELHOUSE_AGENT_FAILED)
}

#[cfg(test)]
mod tests {
    use super::*;
    use conversation_events::{Hello, Page, Snapshot, TextMessageContent, TextMessageEnd, TextMessageStart, CONTRACT};
    use wheelhouse_rich_text::Metrics;

    fn hello(earlier: u64, oldest_seq: u64) -> Update {
        Update::Hello(Hello { contract: CONTRACT, snapshot: Some(Snapshot { earlier_items: earlier, oldest_seq }), ..Hello::default() })
    }

    /// Message `n` as three events at `seq` 3n-2..=3n.
    fn message(n: u64) -> Vec<Event> {
        let id = format!("m{n}");
        let thread_id = "t1".to_owned();
        vec![
            Event::TextMessageStart(TextMessageStart {
                thread_id: thread_id.clone(),
                seq: 3 * n - 2,
                message_id: id.clone(),
                ..Default::default()
            }),
            Event::TextMessageContent(TextMessageContent {
                thread_id: thread_id.clone(),
                seq: 3 * n - 1,
                message_id: id.clone(),
                delta: format!("message {n}"),
            }),
            Event::TextMessageEnd(TextMessageEnd { thread_id, seq: 3 * n, message_id: id }),
        ]
    }

    fn messages(range: std::ops::RangeInclusive<u64>) -> Vec<Update> {
        range.flat_map(message).map(Update::Event).collect()
    }

    fn page(range: std::ops::RangeInclusive<u64>, earlier: u64) -> Update {
        let oldest_seq = 3 * range.start() - 2;
        Update::Page(Ok(Page { events: range.flat_map(message).collect(), earlier_items: earlier, oldest_seq }))
    }

    /// A conversation and a View reading its start, short enough to page.
    fn watched() -> (RichText, SubscriptionId) {
        let mut rich = RichText::watch_conversation(None);
        let id = rich.subscribe();
        let (doc, view) = rich.view_mut(id).unwrap();
        view.set_metrics(doc, Metrics { width: 400.0, line_height: 10.0, chars_per_line: 40.0 });
        view.set_viewport(200.0);
        (rich, id)
    }

    #[test]
    fn older_pages_are_asked_for_one_at_a_time_while_the_view_nears_them() {
        let (mut rich, id) = watched();
        let mut pump = Pump::default();
        let snapshot = [vec![hello(4, 13)], messages(5..=6)].concat();
        assert_eq!(pump.pump(snapshot.into_iter(), &mut rich, id), (WHEELHOUSE_AGENT_CHANGED, Some((13, Duration::ZERO))));
        assert_eq!(pump.pump(std::iter::empty(), &mut rich, id), (0, None), "one page at a time");
        assert_eq!(pump.pump(std::iter::once(page(3..=4, 2)), &mut rich, id), (WHEELHOUSE_AGENT_CHANGED, Some((7, Duration::ZERO))));
        assert_eq!(pump.pump(std::iter::once(page(1..=2, 0)), &mut rich, id), (WHEELHOUSE_AGENT_CHANGED, None));
        assert_eq!(rich.conversation_earlier().0, 0);
        assert!(rich.copy_all().unwrap().contains("message 1"));
    }

    #[test]
    fn a_failed_page_is_asked_for_again_later_each_time_until_one_comes() {
        let (mut rich, id) = watched();
        let mut pump = Pump::default();
        pump.pump([vec![hello(4, 13)], messages(5..=6)].into_iter().flatten(), &mut rich, id);
        let failed = || std::iter::once(Update::Page(Err("refused".to_owned())));
        assert_eq!(pump.pump(failed(), &mut rich, id), (0, Some((13, retry_after(1)))));
        assert_eq!(pump.pump(failed(), &mut rich, id), (0, Some((13, retry_after(2)))));
        assert!(retry_after(2) > retry_after(1));
        assert_eq!(pump.pump(std::iter::once(page(3..=4, 2)), &mut rich, id), (WHEELHOUSE_AGENT_CHANGED, Some((7, Duration::ZERO))));
        assert!(rich.copy_all().unwrap().contains("message 3"));
    }

    #[test]
    fn a_fresh_snapshot_restarts_and_drops_the_page_asked_for_before_it() {
        let (mut rich, id) = watched();
        let mut pump = Pump::default();
        pump.pump([vec![hello(4, 13)], messages(5..=6)].into_iter().flatten(), &mut rich, id);
        let restart = [vec![hello(4, 13)], messages(5..=7)].concat();
        let (flags, older) = pump.pump(restart.into_iter(), &mut rich, id);
        assert_eq!((flags & WHEELHOUSE_AGENT_RESTART, older), (WHEELHOUSE_AGENT_RESTART, None));
        // The host opens a new conversation; the rest waited for it.
        let (mut rich, id) = watched();
        let (flags, older) = pump.pump(std::iter::once(page(3..=4, 2)), &mut rich, id);
        assert_eq!((flags, older), (WHEELHOUSE_AGENT_CHANGED, Some((13, Duration::ZERO))), "the old page dropped, the snapshot read");
        assert!(rich.copy_all().unwrap().contains("message 7"));
        assert!(!rich.copy_all().unwrap().contains("message 3"));
    }

    #[test]
    fn a_resume_after_a_drop_is_no_restart() {
        let (mut rich, id) = watched();
        let mut pump = Pump::default();
        pump.pump([vec![hello(0, 1)], messages(1..=2)].into_iter().flatten(), &mut rich, id);
        let resume = Update::Hello(Hello { contract: CONTRACT, ..Hello::default() });
        let (flags, _) = pump.pump([vec![resume], messages(3..=3)].into_iter().flatten(), &mut rich, id);
        assert_eq!(flags, WHEELHOUSE_AGENT_CHANGED);
        assert!(rich.copy_all().unwrap().contains("message 3"));
    }
}
