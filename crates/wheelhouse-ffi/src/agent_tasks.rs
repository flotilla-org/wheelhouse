//! An agent thread's tasks and roots (#700, #701): stopping a task, reading
//! its output, and searching a root's files.
use super::*;
use std::sync::Arc;

use conversation_events::{FileKind, FileMatch, TaskOutput};
use wheelhouse_agent::{Endpoint, Output, Search, Wake};

use crate::agent_input::ticket;
use crate::agent_watch::route;

fn waker(wake: Option<extern "C" fn()>) -> Wake {
    Arc::new(move || wake.map_or((), |wake| wake()))
}

unsafe fn endpoint(transport: Text, host: Text, endpoint: Text) -> Result<Endpoint, String> {
    // The thread is in each path; the route needs none.
    let route = route(&transport.read()?, host.read()?, endpoint.read()?, "-")?;
    Endpoint::shared(&route).map_err(|error| error.to_string())
}

/// Asks the producer at the Local Endpoint endpoint, reached as
/// wheelhouse_agent_watch_open reaches it, to stop task of thread. Returns a
/// ticket to ask after with wheelhouse_agent_input_status and let go of with
/// wheelhouse_agent_input_forget; 0 for an endpoint it can't reach.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_task_stop(
    transport: Text,
    host: Text,
    endpoint_path: Text,
    thread: Text,
    task: Text,
    error_out: *mut *mut c_char,
) -> u64 {
    pure(error_out, || Ok(ticket(endpoint(transport, host, endpoint_path)?.stop_task(&thread.read()?, &task.read()?)))).unwrap_or(0)
}

pub struct WheelhouseAgentOutput {
    output: Output,
    poisoned: bool,
}

impl Handle for WheelhouseAgentOutput {
    const NAME: &'static str = "agent output";
    fn poisoned(&mut self) -> &mut bool {
        &mut self.poisoned
    }
}

/// Reads a task's output, as its tasks entry names it, while it runs
/// (running 1) or after: the file output_path directly when output_local is
/// 1, else output_url on the endpoint (transport, host and endpoint as for
/// wheelhouse_agent_watch_open; empty endpoint when there is none). wake
/// (if not NULL) is called from another thread when text waits. Free it
/// with wheelhouse_agent_output_free.
#[allow(clippy::too_many_arguments)]
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_output_open(
    transport: Text,
    host: Text,
    endpoint_path: Text,
    output_path: Text,
    output_local: u32,
    output_url: Text,
    running: u32,
    wake: Option<extern "C" fn()>,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseAgentOutput {
    pure(error_out, || {
        let text = |t: Text| -> Result<Option<String>, String> { Ok(Some(t.read()?).filter(|s| !s.is_empty())) };
        let output = TaskOutput { path: text(output_path)?, local: output_local != 0, url: text(output_url)?, media_type: None };
        let endpoint = match endpoint_path.read()?.is_empty() {
            true => None,
            false => Some(endpoint(transport, host, endpoint_path)?),
        };
        let output = Output::open(endpoint.as_ref(), &output, running != 0, waker(wake))?;
        Ok(Box::into_raw(Box::new(WheelhouseAgentOutput { output, poisoned: false })))
    })
    .unwrap_or(ptr::null_mut())
}

/// The task has ended: what is left is read, and then the output ends.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_output_finish(o: *mut WheelhouseAgentOutput, error_out: *mut *mut c_char) -> u32 {
    run(o, error_out, |o| {
        o.output.finish();
        Ok(())
    })
    .is_some() as u32
}

/// Still following the output.
pub const WHEELHOUSE_AGENT_OUTPUT_FOLLOWING: u32 = 0;
/// All of it has been read.
pub const WHEELHOUSE_AGENT_OUTPUT_ENDED: u32 = 1;
/// It can't be read; why goes to why_out.
pub const WHEELHOUSE_AGENT_OUTPUT_FAILED: u32 = 2;

/// Takes the text appended since the last pump into text_out (UTF-8, empty
/// when none) and returns WHEELHOUSE_AGENT_OUTPUT_*. reset_out is set to 1
/// when the output started over: drop what was shown before appending.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_output_pump(
    o: *mut WheelhouseAgentOutput,
    text_out: *mut Bytes,
    reset_out: *mut u32,
    why_out: *mut Bytes,
    error_out: *mut *mut c_char,
) -> u32 {
    run(o, error_out, |o| {
        let read = o.output.take();
        write_bytes(text_out, read.text.into_bytes())?;
        *reset_out.as_mut().ok_or("null reset")? = read.reset as u32;
        Ok(match read.end {
            None => WHEELHOUSE_AGENT_OUTPUT_FOLLOWING,
            Some(Ok(())) => WHEELHOUSE_AGENT_OUTPUT_ENDED,
            Some(Err(why)) => {
                write_bytes(why_out, why.into_bytes())?;
                WHEELHOUSE_AGENT_OUTPUT_FAILED
            }
        })
    })
    .unwrap_or(WHEELHOUSE_AGENT_OUTPUT_FAILED)
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_output_free(o: *mut WheelhouseAgentOutput) {
    if !o.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(o))));
    }
}

pub struct WheelhouseAgentSearch {
    pub(crate) search: Search,
    poisoned: bool,
}

impl Handle for WheelhouseAgentSearch {
    const NAME: &'static str = "agent search";
    fn poisoned(&mut self) -> &mut bool {
        &mut self.poisoned
    }
}

/// Searches a root through its search path (a root's `search`) on the
/// endpoint, reached as wheelhouse_agent_watch_open reaches it. wake (if not
/// NULL) is called from another thread when an answer waits. Free it with
/// wheelhouse_agent_search_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_search_open(
    transport: Text,
    host: Text,
    endpoint_path: Text,
    search: Text,
    wake: Option<extern "C" fn()>,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseAgentSearch {
    pure(error_out, || {
        let search = endpoint(transport, host, endpoint_path)?.search(&search.read()?, waker(wake))?;
        Ok(Box::into_raw(Box::new(WheelhouseAgentSearch { search, poisoned: false })))
    })
    .unwrap_or(ptr::null_mut())
}

/// Asks for up to limit files matching query once it has stood for about
/// 150 ms with no later query; an earlier query's answer is dropped.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_search_query(
    s: *mut WheelhouseAgentSearch,
    query: Text,
    limit: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    run(s, error_out, |s| {
        s.search.query(&query.read()?, limit);
        Ok(())
    })
    .is_some() as u32
}

pub const WHEELHOUSE_FILE: u32 = 0;
pub const WHEELHOUSE_DIR: u32 = 1;
/// A kind this version doesn't know.
pub const WHEELHOUSE_FILE_UNKNOWN: u32 = 2;

pub(crate) fn file_kind(kind: FileKind) -> u32 {
    match kind {
        FileKind::File => WHEELHOUSE_FILE,
        FileKind::Dir => WHEELHOUSE_DIR,
        FileKind::Unknown => WHEELHOUSE_FILE_UNKNOWN,
    }
}

/// A file a root's search found: its path relative to the root.
#[repr(C)]
pub struct WheelhouseFileMatch {
    pub path: Text,
    /// WHEELHOUSE_FILE, _DIR or _FILE_UNKNOWN.
    pub kind: u32,
}

/// The answer to the latest query. failed 1 says why the search failed.
/// Every string is borrowed from it until wheelhouse_file_matches_free.
#[repr(C)]
pub struct WheelhouseFileMatches {
    pub query: Text,
    pub matches: *const WheelhouseFileMatch,
    pub match_count: usize,
    pub failed: u32,
    pub why: Text,
}

type MatchesKeep = (String, Vec<FileMatch>, String, Vec<WheelhouseFileMatch>);

/// The latest query's answer, once it has come; NULL until then, or after
/// it was taken. Free it with wheelhouse_file_matches_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_search_take(
    s: *mut WheelhouseAgentSearch,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseFileMatches {
    run(s, error_out, |s| {
        let Some(answer) = s.search.take() else { return Ok(ptr::null_mut()) };
        let (matches, why) = match answer.matches {
            Ok(matches) => (matches, None),
            Err(why) => (vec![], Some(why)),
        };
        let views: Vec<WheelhouseFileMatch> =
            matches.iter().map(|m| WheelhouseFileMatch { path: Text::borrowed(&m.path), kind: file_kind(m.kind) }).collect();
        let why_text = why.clone().unwrap_or_default();
        let value = WheelhouseFileMatches {
            query: Text::borrowed(&answer.query),
            matches: views.as_ptr(),
            match_count: views.len(),
            failed: why.is_some() as u32,
            why: Text::borrowed(&why_text),
        };
        Ok(give::<_, MatchesKeep>(value, (answer.query, matches, why_text, views)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_file_matches_free(matches: *mut WheelhouseFileMatches) {
    take::<_, MatchesKeep>(matches)
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_search_free(s: *mut WheelhouseAgentSearch) {
    if !s.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(s))));
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::time::{Duration, Instant};

    fn until<T>(mut f: impl FnMut() -> Option<T>) -> T {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if let Some(value) = f() {
                return value;
            }
            assert!(Instant::now() < deadline, "timed out");
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    unsafe fn bytes(b: Bytes) -> String {
        let text = if b.data.is_null() { String::new() } else { String::from_utf8(slice(b.data, b.len).unwrap().to_vec()).unwrap() };
        wheelhouse_bytes_free(b);
        text
    }

    // Through C: a local output is read without any endpoint and ends once
    // finished; a search and a stop against a producer that isn't there
    // answer failed rather than hang, and an unknown transport is refused.
    #[test]
    fn output_search_and_stop_reach_c() {
        let dir = std::env::temp_dir().join(format!("wh-ffi-tasks-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let log = dir.join("task.log");
        std::fs::write(&log, "built\n").unwrap();
        let [none, path] = ["", log.to_str().unwrap()].map(Text::borrowed);
        unsafe {
            let o = wheelhouse_agent_output_open(none, none, none, path, 1, none, 1, None, ptr::null_mut());
            assert!(!o.is_null());
            let mut text = String::new();
            let pump = |text: &mut String| {
                let (mut out, mut why, mut reset) = (Bytes { data: ptr::null_mut(), len: 0 }, Bytes { data: ptr::null_mut(), len: 0 }, 0);
                let state = wheelhouse_agent_output_pump(o, &mut out, &mut reset, &mut why, ptr::null_mut());
                text.push_str(&bytes(out));
                bytes(why);
                state
            };
            until(|| (pump(&mut text) == WHEELHOUSE_AGENT_OUTPUT_FOLLOWING && text == "built\n").then_some(()));
            assert_eq!(wheelhouse_agent_output_finish(o, ptr::null_mut()), 1);
            until(|| (pump(&mut text) == WHEELHOUSE_AGENT_OUTPUT_ENDED).then_some(()));
            wheelhouse_agent_output_free(o);

            let socket = dir.join("nobody.sock");
            let dead = Text::borrowed(socket.to_str().unwrap());
            let s = wheelhouse_agent_search_open(none, none, dead, Text::borrowed("/threads/t1/files"), None, ptr::null_mut());
            assert_eq!(wheelhouse_agent_search_query(s, Text::borrowed("main"), 8, ptr::null_mut()), 1);
            let answer = until(|| Some(wheelhouse_agent_search_take(s, ptr::null_mut())).filter(|a| !a.is_null()));
            assert_eq!(((*answer).query.read().unwrap(), (*answer).failed, (*answer).match_count), ("main".into(), 1, 0));
            wheelhouse_file_matches_free(answer);
            wheelhouse_agent_search_free(s);

            let [t1, k1] = ["t1", "k1"].map(Text::borrowed);
            let ticket = wheelhouse_agent_task_stop(none, none, dead, t1, k1, ptr::null_mut());
            assert_ne!(ticket, 0);
            let mut why = Bytes { data: ptr::null_mut(), len: 0 };
            until(|| (wheelhouse_agent_input_status(ticket, &mut why, ptr::null_mut()) == WHEELHOUSE_AGENT_INPUT_FAILED).then_some(()));
            bytes(why);
            wheelhouse_agent_input_forget(ticket);
            assert_eq!(wheelhouse_agent_task_stop(Text::borrowed("mosh"), none, dead, t1, k1, ptr::null_mut()), 0);
        }
        let _ = std::fs::remove_dir_all(&dir);
    }
}
