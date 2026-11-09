//! Input to a conversation's producer (the agent View prototype, #683): a
//! JSON body POSTed to the HTTP server its hello names. A post is a ticket
//! the host asks after, so it can tell a send in flight from one the
//! producer accepted; what the producer does with it shows in the
//! conversation's own stream.
use super::*;
use std::collections::HashMap;
use std::sync::Mutex;
use wheelhouse_agent::{Endpoint, Post, PostStatus};

use crate::agent_watch::route;

pub const WHEELHOUSE_AGENT_INPUT_SENDING: u32 = 0;
pub const WHEELHOUSE_AGENT_INPUT_ACCEPTED: u32 = 1;
pub const WHEELHOUSE_AGENT_INPUT_FAILED: u32 = 2;

static TICKETS: Mutex<Option<HashMap<u64, Post>>> = Mutex::new(None);
static NEXT_TICKET: AtomicU64 = AtomicU64::new(1);

/// Posts `body` to `path` on the Local Endpoint whose socket is `endpoint`,
/// reached as wheelhouse_agent_watch_open reaches it, and returns its
/// ticket at once: 0 for an endpoint it can't reach. Ask after it with
/// wheelhouse_agent_input_status and let it go with
/// wheelhouse_agent_input_forget.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_input_post(
    transport: Text,
    host: Text,
    endpoint: Text,
    path: Text,
    body: Text,
    error_out: *mut *mut c_char,
) -> u64 {
    pure(error_out, || {
        // The thread is in the path; the route needs none.
        let route = route(&transport.read()?, host.read()?, endpoint.read()?, "-")?;
        let post = Endpoint::shared(&route).map_err(|error| error.to_string())?.post(&path.read()?, body.read()?.into_bytes());
        let ticket = NEXT_TICKET.fetch_add(1, Ordering::Relaxed);
        TICKETS.lock().unwrap().get_or_insert_default().insert(ticket, post);
        Ok(ticket)
    })
    .unwrap_or(0)
}

/// A ticket's WHEELHOUSE_AGENT_INPUT_* status; a failure's reason goes to
/// `why_out`. An unknown ticket reads as failed.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_input_status(ticket: u64, why_out: *mut Bytes, error_out: *mut *mut c_char) -> u32 {
    pure(error_out, || {
        let status = TICKETS
            .lock()
            .unwrap()
            .as_ref()
            .and_then(|t| t.get(&ticket).map(Post::status))
            .unwrap_or(PostStatus::Failed("unknown ticket".into()));
        Ok(match status {
            PostStatus::Sending => WHEELHOUSE_AGENT_INPUT_SENDING,
            PostStatus::Accepted => WHEELHOUSE_AGENT_INPUT_ACCEPTED,
            PostStatus::Failed(why) => {
                write_bytes(why_out, why.into_bytes())?;
                WHEELHOUSE_AGENT_INPUT_FAILED
            }
        })
    })
    .unwrap_or(WHEELHOUSE_AGENT_INPUT_FAILED)
}

#[no_mangle]
pub extern "C" fn wheelhouse_agent_input_forget(ticket: u64) {
    if let Some(tickets) = TICKETS.lock().unwrap().as_mut() {
        tickets.remove(&ticket);
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::os::unix::net::UnixListener;
    use std::thread;
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
        post_over("", "", socket, body)
    }

    fn post_over(transport: &str, host: &str, socket: &str, body: &str) -> u64 {
        let [transport, host, socket, path, body] = [transport, host, socket, "/run", body].map(Text::borrowed);
        unsafe { wheelhouse_agent_input_post(transport, host, socket, path, body, std::ptr::null_mut()) }
    }

    /// One request, answered with `reply`; returns what was posted.
    fn serve_once(listener: UnixListener, reply: &'static [u8]) -> thread::JoinHandle<String> {
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
            stream.write_all(reply).unwrap();
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
        let server = serve_once(UnixListener::bind(&socket).unwrap(), b"HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
        let ticket = post_to(socket.to_str().unwrap(), r#"{"messages":[]}"#);
        assert_ne!(ticket, 0);
        assert_eq!(settled(ticket).0, WHEELHOUSE_AGENT_INPUT_ACCEPTED);
        assert!(server.join().unwrap().ends_with(r#"{"messages":[]}"#));
        wheelhouse_agent_input_forget(ticket);

        let _ = std::fs::remove_file(&socket);
        let server = serve_once(UnixListener::bind(&socket).unwrap(), b"HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"no user message\"}");
        let ticket = post_to(socket.to_str().unwrap(), "{}");
        assert_eq!(settled(ticket), (WHEELHOUSE_AGENT_INPUT_FAILED, r#"{"error":"no user message"}"#.into()));
        server.join().unwrap();

        let _ = std::fs::remove_file(&socket);
        let server = serve_once(UnixListener::bind(&socket).unwrap(), b"HTTP/1.1 200 OK\r\n\r\n\xff\xfe");
        let ticket = post_to(socket.to_str().unwrap(), "{}");
        assert_eq!(settled(ticket).0, WHEELHOUSE_AGENT_INPUT_ACCEPTED, "a body that isn't UTF-8 still accepted");
        server.join().unwrap();

        let _ = std::fs::remove_file(&socket);
        let ticket = post_to(socket.to_str().unwrap(), "{}");
        assert_eq!(settled(ticket).0, WHEELHOUSE_AGENT_INPUT_FAILED, "no producer listening");
        assert_eq!(post_over("mosh", "feta", "/run/p.sock", "{}"), 0, "an unknown transport");
        assert_eq!(post_over("ssh", "-oProxyCommand=x", "/run/p.sock", "{}"), 0, "a host ssh would read as an option");
        let _ = std::fs::remove_dir_all(&dir);
    }
}
