//! Input to a conversation's producer (the agent View prototype): a JSON body
//! POSTed to the HTTP server its hello names, on a background thread. What
//! the producer does with it shows in the conversation's own stream.
use super::*;
use std::thread;

/// Posts `body` to `path` on `endpoint` (`unix://PATH`) and returns at once;
/// a failure is written to stderr. Returns 0 for an endpoint that isn't
/// `unix://`.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_input_post(endpoint: Text, path: Text, body: Text, error_out: *mut *mut c_char) -> u32 {
    pure(error_out, || {
        let endpoint = endpoint.read()?;
        let socket = endpoint.strip_prefix("unix://").ok_or_else(|| format!("unsupported input endpoint {endpoint}"))?.to_owned();
        let (path, body) = (path.read()?, body.read()?);
        thread::spawn(move || {
            if let Err(error) = post(&socket, &path, &body) {
                eprintln!("agent input: POST {path} to {socket}: {error}");
            }
        });
        Ok(())
    })
    .is_some() as u32
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
        Err(std::io::Error::other(status.to_owned()))
    }
}

// Windows has AF_UNIX too, but std has no stream for it; the prototype is
// Unix-only.
#[cfg(not(unix))]
fn post(_socket: &str, _path: &str, _body: &str) -> std::io::Result<()> {
    Err(std::io::Error::other("agent input needs a Unix socket"))
}
