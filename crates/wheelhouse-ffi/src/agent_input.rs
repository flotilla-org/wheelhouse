//! Input to a conversation's producer (the agent View prototype, #683): a
//! JSON body POSTed to the HTTP server its hello names, on a background
//! thread. A post is a ticket the host asks after, so it can tell a send in
//! flight from one the producer accepted; what the producer does with it
//! shows in the conversation's own stream.
use super::*;
use std::collections::HashMap;
use std::sync::Mutex;
use std::thread;

pub const WHEELHOUSE_AGENT_INPUT_SENDING: u32 = 0;
pub const WHEELHOUSE_AGENT_INPUT_ACCEPTED: u32 = 1;
pub const WHEELHOUSE_AGENT_INPUT_FAILED: u32 = 2;

/// Each ticket's status and, once failed, why.
static TICKETS: Mutex<Option<HashMap<u64, (u32, String)>>> = Mutex::new(None);
static NEXT_TICKET: AtomicU64 = AtomicU64::new(1);

fn settle(ticket: u64, status: u32, why: String) {
    if let Some(tickets) = TICKETS.lock().unwrap().as_mut() {
        tickets.insert(ticket, (status, why));
    }
}

/// Posts `body` to `path` on `endpoint` (`unix://PATH`) and returns its
/// ticket at once: 0 for an endpoint that isn't `unix://`. Ask after it
/// with wheelhouse_agent_input_status and let it go with
/// wheelhouse_agent_input_forget.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_input_post(endpoint: Text, path: Text, body: Text, error_out: *mut *mut c_char) -> u64 {
    pure(error_out, || {
        let endpoint = endpoint.read()?;
        let socket = endpoint.strip_prefix("unix://").ok_or_else(|| format!("unsupported input endpoint {endpoint}"))?.to_owned();
        let (path, body) = (path.read()?, body.read()?);
        let ticket = NEXT_TICKET.fetch_add(1, Ordering::Relaxed);
        TICKETS.lock().unwrap().get_or_insert_default().insert(ticket, (WHEELHOUSE_AGENT_INPUT_SENDING, String::new()));
        thread::spawn(move || match post(&socket, &path, &body) {
            Ok(()) => settle(ticket, WHEELHOUSE_AGENT_INPUT_ACCEPTED, String::new()),
            Err(error) => settle(ticket, WHEELHOUSE_AGENT_INPUT_FAILED, error.to_string()),
        });
        Ok(ticket)
    })
    .unwrap_or(0)
}

/// A ticket's WHEELHOUSE_AGENT_INPUT_* status; a failure's reason goes to
/// `why_out`. An unknown ticket reads as failed.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_input_status(ticket: u64, why_out: *mut Bytes, error_out: *mut *mut c_char) -> u32 {
    pure(error_out, || {
        let (status, why) = TICKETS
            .lock()
            .unwrap()
            .as_ref()
            .and_then(|t| t.get(&ticket).cloned())
            .unwrap_or((WHEELHOUSE_AGENT_INPUT_FAILED, "unknown ticket".into()));
        if status == WHEELHOUSE_AGENT_INPUT_FAILED {
            write_bytes(why_out, why.into_bytes())?;
        }
        Ok(status)
    })
    .unwrap_or(WHEELHOUSE_AGENT_INPUT_FAILED)
}

#[no_mangle]
pub extern "C" fn wheelhouse_agent_input_forget(ticket: u64) {
    if let Some(tickets) = TICKETS.lock().unwrap().as_mut() {
        tickets.remove(&ticket);
    }
}

#[cfg(unix)]
fn post(socket: &str, path: &str, body: &str) -> std::io::Result<()> {
    use std::io::{Read, Write};
    use std::os::unix::net::UnixStream;

    let mut stream = UnixStream::connect(socket)?;
    write!(stream, "POST {path} HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len())?;
    let mut reply = String::new();
    stream.read_to_string(&mut reply)?;
    let status = reply.lines().next().unwrap_or_default();
    if status.split(' ').nth(1).is_some_and(|code| code.starts_with('2')) {
        Ok(())
    } else {
        // The producer's JSON error says why, after the headers.
        let why = reply.split("\r\n\r\n").nth(1).unwrap_or(status);
        Err(std::io::Error::other(why.to_owned()))
    }
}

// Windows has AF_UNIX too, but std has no stream for it; the prototype is
// Unix-only.
#[cfg(not(unix))]
fn post(_socket: &str, _path: &str, _body: &str) -> std::io::Result<()> {
    Err(std::io::Error::other("agent input needs a Unix socket"))
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::os::unix::net::UnixListener;
    use std::time::{Duration, Instant};

    fn settled(ticket: u64) -> (u32, String) {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            let mut why = Bytes { data: std::ptr::null_mut(), len: 0 };
            let status = unsafe { wheelhouse_agent_input_status(ticket, &mut why, std::ptr::null_mut()) };
            if status != WHEELHOUSE_AGENT_INPUT_SENDING || Instant::now() > deadline {
                let text = if why.data.is_null() {
                    String::new()
                } else {
                    unsafe { String::from_utf8_lossy(slice(why.data, why.len).unwrap()).into_owned() }
                };
                if !why.data.is_null() {
                    unsafe { wheelhouse_bytes_free(why) };
                }
                return (status, text);
            }
            thread::sleep(Duration::from_millis(5));
        }
    }

    fn post_to(socket: &str, body: &str) -> u64 {
        let endpoint = format!("unix://{socket}");
        unsafe {
            wheelhouse_agent_input_post(Text::borrowed(&endpoint), Text::borrowed("/run"), Text::borrowed(body), std::ptr::null_mut())
        }
    }

    /// One request, answered with `reply`; returns what was posted.
    fn serve_once(listener: UnixListener, reply: &'static str) -> thread::JoinHandle<String> {
        thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            // The whole request first: closing on unread bytes resets the
            // connection before the client reads the reply.
            let mut request = vec![];
            let mut chunk = [0; 4096];
            while !complete(&request) {
                let n = stream.read(&mut chunk).unwrap();
                request.extend_from_slice(&chunk[..n]);
            }
            stream.write_all(reply.as_bytes()).unwrap();
            String::from_utf8_lossy(&request).into_owned()
        })
    }

    fn complete(request: &[u8]) -> bool {
        let text = String::from_utf8_lossy(request);
        let Some((head, body)) = text.split_once("\r\n\r\n") else { return false };
        let length = head.lines().find_map(|l| l.strip_prefix("Content-Length: ")).and_then(|n| n.parse().ok()).unwrap_or(0);
        body.len() >= length
    }

    #[test]
    fn a_post_is_sending_then_accepted_or_failed_with_why() {
        let dir = std::env::temp_dir().join(format!("wh-input-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let socket = dir.join("a.sock");
        let _ = std::fs::remove_file(&socket);
        let server = serve_once(UnixListener::bind(&socket).unwrap(), "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
        let ticket = post_to(socket.to_str().unwrap(), r#"{"messages":[]}"#);
        assert_ne!(ticket, 0);
        assert_eq!(settled(ticket).0, WHEELHOUSE_AGENT_INPUT_ACCEPTED);
        assert!(server.join().unwrap().ends_with(r#"{"messages":[]}"#));
        wheelhouse_agent_input_forget(ticket);

        let _ = std::fs::remove_file(&socket);
        let server = serve_once(UnixListener::bind(&socket).unwrap(), "HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"no user message\"}");
        let ticket = post_to(socket.to_str().unwrap(), "{}");
        assert_eq!(settled(ticket), (WHEELHOUSE_AGENT_INPUT_FAILED, r#"{"error":"no user message"}"#.into()));
        server.join().unwrap();

        let _ = std::fs::remove_file(&socket);
        let ticket = post_to(socket.to_str().unwrap(), "{}");
        assert_eq!(settled(ticket).0, WHEELHOUSE_AGENT_INPUT_FAILED, "no producer listening");
        assert_eq!(post_to_endpoint("tcp://x"), 0);
        let _ = std::fs::remove_dir_all(&dir);
    }

    fn post_to_endpoint(endpoint: &str) -> u64 {
        unsafe { wheelhouse_agent_input_post(Text::borrowed(endpoint), Text::borrowed("/run"), Text::borrowed("{}"), std::ptr::null_mut()) }
    }
}
