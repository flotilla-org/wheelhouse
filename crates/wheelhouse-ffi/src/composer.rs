//! The Composer's `/` and `@` popups (#694 C7–C9, #701): the completion
//! model fed from a watched conversation, with its root's search.
use super::*;
use std::sync::Arc;

use fearnought_client::{Completion, Endpoint, Key, Popup, RowKind, Search, FILE_LIMIT, VISIBLE_ROWS};
use wheelhouse_rich_text::Typed;

use crate::agent_watch::route;
use crate::rich_text::WheelhouseRichText;

pub struct WheelhouseComposer {
    completion: Completion,
    endpoint: Option<Endpoint>,
    /// The root searched, and its search.
    search: Option<(String, Search)>,
    wake: Option<extern "C" fn()>,
    /// The last edit's text, which an edit borrows.
    edit: String,
    poisoned: bool,
}

impl Handle for WheelhouseComposer {
    const NAME: &'static str = "composer";
    fn poisoned(&mut self) -> &mut bool {
        &mut self.poisoned
    }
}

/// A Composer's completion for a thread whose producer is at endpoint,
/// reached as wheelhouse_agent_watch_open reaches it (an empty endpoint: no
/// root is searched). wake (if not NULL) is called from another thread when
/// file results wait: call wheelhouse_composer_update again. Free it with
/// wheelhouse_composer_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_composer_new(
    transport: Text,
    host: Text,
    endpoint: Text,
    wake: Option<extern "C" fn()>,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseComposer {
    pure(error_out, || {
        let endpoint = match endpoint.read()? {
            path if path.is_empty() => None,
            path => Some(Endpoint::shared(&route(&transport.read()?, host.read()?, path, "-")?).map_err(|e| e.to_string())?),
        };
        let completion = Completion::default();
        Ok(Box::into_raw(Box::new(WheelhouseComposer { completion, endpoint, search: None, wake, edit: String::new(), poisoned: false })))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_composer_free(c: *mut WheelhouseComposer) {
    if !c.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(c))));
    }
}

/// The field now: its text and cursor (a byte offset into text), read
/// against doc's `commands` document and roots (doc may be NULL: no data,
/// no popups). Call it whenever the field changes and when woken. Returns 1
/// when the popup or hint changed: read wheelhouse_composer_popup again.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_composer_update(
    c: *mut WheelhouseComposer,
    doc: *mut WheelhouseRichText,
    text: Text,
    cursor: usize,
    error_out: *mut *mut c_char,
) -> u32 {
    let doc = doc.as_ref();
    run(c, error_out, |c| {
        let before = c.completion.revision();
        let rich = doc.map(|doc| &doc.rich);
        let commands = rich.and_then(|rich| {
            rich.conversation_documents().into_iter().find_map(|d| match d.typed() {
                Typed::Commands(commands) => Some(commands.as_slice()),
                _ => None,
            })
        });
        // The first root that names a search is the one searched.
        let root = rich.and_then(|rich| rich.conversation_capabilities()).and_then(|c| c.roots.iter().find_map(|r| r.search.clone()));
        if c.search.as_ref().map(|(at, _)| at) != root.as_ref() {
            let wake = c.wake;
            c.search = match (&c.endpoint, root) {
                (Some(endpoint), Some(root)) => {
                    endpoint.search(&root, Arc::new(move || wake.map_or((), |wake| wake()))).ok().map(|search| (root, search))
                }
                _ => None,
            };
        }
        if let Some(answer) = c.search.as_ref().and_then(|(_, search)| search.take()) {
            c.completion.answer(&answer.query, answer.matches.unwrap_or_default());
        }
        let text = text.read()?;
        let searched = c.search.as_ref().map(|(root, _)| root.as_str());
        if let Some(query) = c.completion.update(commands, searched, &text, cursor) {
            if let Some((_, search)) = &c.search {
                search.query(&query, FILE_LIMIT);
            }
        }
        Ok(c.completion.revision() != before)
    })
    .unwrap_or(false) as u32
}

pub const WHEELHOUSE_COMPOSER_KEY_UP: u32 = 0;
pub const WHEELHOUSE_COMPOSER_KEY_DOWN: u32 = 1;
/// Tab or Enter.
pub const WHEELHOUSE_COMPOSER_KEY_ACCEPT: u32 = 2;
pub const WHEELHOUSE_COMPOSER_KEY_ESCAPE: u32 = 3;

/// Replace bytes [start, end) of the field's text with text, and put the
/// cursor after it. text is borrowed from the composer until the next call
/// on it.
#[repr(C)]
pub struct WheelhouseComposerEdit {
    pub start: usize,
    pub end: usize,
    pub text: Text,
}

/// A WHEELHOUSE_COMPOSER_KEY_*: 1 if the popup took it, else 0 and the key
/// is the field's own (Enter sends). Accepting a row writes its edit to
/// edit_out and returns 2; apply it, then call wheelhouse_composer_update.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_composer_key(
    c: *mut WheelhouseComposer,
    key: u32,
    edit_out: *mut WheelhouseComposerEdit,
    error_out: *mut *mut c_char,
) -> u32 {
    run(c, error_out, |c| {
        let key = match key {
            WHEELHOUSE_COMPOSER_KEY_UP => Key::Up,
            WHEELHOUSE_COMPOSER_KEY_DOWN => Key::Down,
            WHEELHOUSE_COMPOSER_KEY_ACCEPT => Key::Accept,
            WHEELHOUSE_COMPOSER_KEY_ESCAPE => Key::Escape,
            other => return Err(format!("unknown composer key {other}")),
        };
        let (taken, edit) = c.completion.key(key);
        let Some(edit) = edit else { return Ok(taken as u32) };
        let out = edit_out.as_mut().ok_or("null edit")?;
        c.edit = edit.text;
        *out = WheelhouseComposerEdit { start: edit.range.start, end: edit.range.end, text: Text::borrowed(&c.edit) };
        Ok(2)
    })
    .unwrap_or(0)
}

pub const WHEELHOUSE_COMPOSER_POPUP_NONE: u32 = 0;
/// `/` as the field's first character: the thread's Agent Commands.
pub const WHEELHOUSE_COMPOSER_POPUP_COMMAND: u32 = 1;
/// An `@` token: its `@` Agent Commands, then the root's files.
pub const WHEELHOUSE_COMPOSER_POPUP_MENTION: u32 = 2;

pub const WHEELHOUSE_COMPOSER_ROW_COMMAND: u32 = 0;
pub const WHEELHOUSE_COMPOSER_ROW_FILE: u32 = 1;
pub const WHEELHOUSE_COMPOSER_ROW_DIR: u32 = 2;

/// A popup row: a command's name, dimmed description and right-aligned
/// group (empty for none), or a file's path.
#[repr(C)]
pub struct WheelhouseComposerRow {
    /// WHEELHOUSE_COMPOSER_ROW_*.
    pub kind: u32,
    pub name: Text,
    pub description: Text,
    pub group: Text,
}

/// What the Composer shows: the popup's rows from first (at most
/// WHEELHOUSE_COMPOSER_VISIBLE_ROWS of total), the selected row's index
/// among them (WHEELHOUSE_NONE when it isn't shown), and ghost text after
/// an inserted command (hint, at byte hint_at of the field) until the user
/// types. Every string is borrowed from it until wheelhouse_composer_popup_free.
#[repr(C)]
pub struct WheelhouseComposerPopup {
    /// WHEELHOUSE_COMPOSER_POPUP_*.
    pub kind: u32,
    pub rows: *const WheelhouseComposerRow,
    pub row_count: usize,
    pub first: usize,
    pub total: usize,
    pub selected: usize,
    pub has_hint: u32,
    pub hint: Text,
    pub hint_at: usize,
}

pub const WHEELHOUSE_COMPOSER_VISIBLE_ROWS: usize = VISIBLE_ROWS;

type PopupKeep = (Vec<fearnought_client::Row>, String, Vec<WheelhouseComposerRow>);

/// Free it with wheelhouse_composer_popup_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_composer_popup(
    c: *mut WheelhouseComposer,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseComposerPopup {
    run(c, error_out, |c| {
        let completion = &c.completion;
        let (first, shown) = completion.visible();
        let shown = shown.to_vec();
        let (hint_at, hint) = completion.hint().map_or((0, None), |(at, hint)| (at, Some(hint.to_owned())));
        let hint_text = hint.clone().unwrap_or_default();
        let views: Vec<WheelhouseComposerRow> = shown
            .iter()
            .map(|row| WheelhouseComposerRow {
                kind: match row.kind {
                    RowKind::Command => WHEELHOUSE_COMPOSER_ROW_COMMAND,
                    RowKind::File => WHEELHOUSE_COMPOSER_ROW_FILE,
                    RowKind::Dir => WHEELHOUSE_COMPOSER_ROW_DIR,
                },
                name: Text::borrowed(&row.name),
                description: Text::borrowed(&row.description),
                group: Text::borrowed(row.group.as_deref().unwrap_or_default()),
            })
            .collect();
        let selected = completion.selected().and_then(|s| s.checked_sub(first)).filter(|s| *s < views.len()).unwrap_or(NONE);
        let value = WheelhouseComposerPopup {
            kind: match completion.popup() {
                None => WHEELHOUSE_COMPOSER_POPUP_NONE,
                Some(Popup::Command) => WHEELHOUSE_COMPOSER_POPUP_COMMAND,
                Some(Popup::Mention) => WHEELHOUSE_COMPOSER_POPUP_MENTION,
            },
            rows: views.as_ptr(),
            row_count: views.len(),
            first,
            total: completion.rows().len(),
            selected,
            has_hint: hint.is_some() as u32,
            hint: Text::borrowed(&hint_text),
            hint_at,
        };
        Ok(give::<_, PopupKeep>(value, (shown, hint_text, views)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_composer_popup_free(popup: *mut WheelhouseComposerPopup) {
    take::<_, PopupKeep>(popup)
}

#[cfg(test)]
mod tests {
    use super::*;
    use conversation_events::{Capabilities, Event, Hello, Root, StateSnapshot, COMMANDS_MEDIA_TYPE, CONTRACT};
    use wheelhouse_rich_text::RichText;

    // Through C: a thread's commands document opens the `/` popup, a key
    // inserts the selected row with its hint after it, and with no
    // conversation there is no popup.
    #[test]
    fn the_composer_completes_from_a_conversation_through_c() {
        let mut doc = WheelhouseRichText::new(RichText::watch_conversation(None));
        let roots = vec![Root { path: "/work".into(), local: true, search: None }];
        let hello = Hello {
            contract: CONTRACT,
            thread_id: "t".into(),
            capabilities: Some(Capabilities { roots, ..Default::default() }),
            snapshot: None,
        };
        let commands = serde_json::json!([
            {"name": "compact", "description": "Summarize", "insert": "/compact "},
            {"name": "review", "description": "Review changes", "hint": "[instructions]", "insert": "/review ", "group": "Built in"},
        ]);
        let state = StateSnapshot {
            thread_id: "t".into(),
            seq: 1,
            key: Some("commands".into()),
            media_type: Some(COMMANDS_MEDIA_TYPE.into()),
            snapshot: commands,
        };
        doc.rich.receive(vec![Event::Hello(hello), Event::StateSnapshot(state)]);
        let none = Text::borrowed("");
        unsafe {
            let c = wheelhouse_composer_new(none, none, none, None, ptr::null_mut());
            let popup = |c| {
                let p = wheelhouse_composer_popup(c, ptr::null_mut());
                let rows: Vec<(String, String)> =
                    slice((*p).rows, (*p).row_count).unwrap().iter().map(|r| (r.name.read().unwrap(), r.group.read().unwrap())).collect();
                let read = ((*p).kind, rows, (*p).selected, (*p).has_hint, (*p).hint.read().unwrap(), (*p).hint_at);
                wheelhouse_composer_popup_free(p);
                read
            };
            assert_eq!(wheelhouse_composer_update(c, &mut doc, Text::borrowed("/"), 1, ptr::null_mut()), 1);
            let (kind, rows, selected, ..) = popup(c);
            assert_eq!((kind, selected), (WHEELHOUSE_COMPOSER_POPUP_COMMAND, 0));
            assert_eq!(rows, [("compact".into(), String::new()), ("review".into(), "Built in".into())]);
            assert_eq!(wheelhouse_composer_update(c, &mut doc, Text::borrowed("/"), 1, ptr::null_mut()), 0, "nothing changed");
            let mut edit = WheelhouseComposerEdit { start: 0, end: 0, text: none };
            assert_eq!(wheelhouse_composer_key(c, WHEELHOUSE_COMPOSER_KEY_DOWN, &mut edit, ptr::null_mut()), 1);
            assert_eq!(wheelhouse_composer_key(c, WHEELHOUSE_COMPOSER_KEY_ACCEPT, &mut edit, ptr::null_mut()), 2);
            assert_eq!((edit.start, edit.end, edit.text.read().unwrap()), (0, 1, "/review ".to_owned()));
            wheelhouse_composer_update(c, &mut doc, Text::borrowed("/review "), 8, ptr::null_mut());
            let (kind, _, selected, has_hint, hint, at) = popup(c);
            assert_eq!((kind, selected, has_hint, hint.as_str(), at), (WHEELHOUSE_COMPOSER_POPUP_NONE, NONE, 1, "[instructions]", 8));
            assert_eq!(wheelhouse_composer_key(c, WHEELHOUSE_COMPOSER_KEY_ACCEPT, &mut edit, ptr::null_mut()), 0, "Enter sends");

            wheelhouse_composer_update(c, ptr::null_mut(), Text::borrowed("/"), 1, ptr::null_mut());
            assert_eq!(popup(c).0, WHEELHOUSE_COMPOSER_POPUP_NONE, "no conversation, no commands");
            wheelhouse_composer_free(c);
        }
    }
}
