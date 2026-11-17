use std::fs::{metadata, File};
use std::io::{Error, ErrorKind, Read, Seek, SeekFrom};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use conversation_events::TaskOutput;
use reqwest::{StatusCode, Url};
use tokio::sync::Notify;
use tokio::time::sleep;

use crate::{get, on_endpoint, Endpoint, Link, Wake};

/// How often a running task's output is read again.
pub const TAIL_EVERY: Duration = Duration::from_millis(200);

/// What an [`Output`] read since the last look.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct OutputRead {
    /// Appended text, its invalid UTF-8 replaced.
    pub text: String,
    /// The output started over (a file truncated, a producer's buffer
    /// trimmed): what was shown before goes, and `text` is from the start.
    pub reset: bool,
    /// Once set, nothing more comes: all of it was read, or why not.
    pub end: Option<Result<(), String>>,
}

/// A task's output, followed while it runs (#700 D4): its file, when the
/// producer names one on this machine, or else its `url` on the producer's
/// endpoint. Dropping it stops reading.
pub struct Output {
    shared: Arc<Shared>,
    tail: Option<JoinHandle<()>>,
    _endpoint: Option<Endpoint>,
}

struct Shared {
    read: Mutex<Pending>,
    /// The task ended: one more read takes the rest.
    finished: AtomicBool,
    gone: AtomicBool,
    finish: Notify,
    wake: Wake,
}

#[derive(Default)]
struct Pending {
    read: OutputRead,
    /// The start of a character a later read finishes.
    carry: Vec<u8>,
}

impl Shared {
    fn push(&self, bytes: &[u8], reset: bool) {
        if bytes.is_empty() && !reset {
            return;
        }
        let mut pending = self.read.lock().unwrap();
        if reset {
            pending.read.text.clear();
            pending.read.reset = true;
            pending.carry.clear();
        }
        pending.carry.extend_from_slice(bytes);
        let whole = pending.carry.len() - partial(&pending.carry);
        let text = String::from_utf8_lossy(&pending.carry[..whole]).into_owned();
        pending.carry.drain(..whole);
        pending.read.text.push_str(&text);
        drop(pending);
        (self.wake)();
    }

    fn end(&self, end: Result<(), String>) {
        let mut pending = self.read.lock().unwrap();
        if end.is_ok() && !pending.carry.is_empty() {
            let rest = String::from_utf8_lossy(&std::mem::take(&mut pending.carry)).into_owned();
            pending.read.text.push_str(&rest);
        }
        pending.read.end = Some(end);
        drop(pending);
        (self.wake)();
    }

    fn gone(&self) -> bool {
        self.gone.load(Ordering::Acquire)
    }
}

/// How many bytes at the end of `bytes` start a character not yet whole.
fn partial(bytes: &[u8]) -> usize {
    for back in 1..=bytes.len().min(3) {
        let byte = bytes[bytes.len() - back];
        if byte & 0xC0 == 0x80 {
            continue;
        }
        let width = match byte {
            0xC0..=0xDF => 2,
            0xE0..=0xEF => 3,
            0xF0..=0xF7 => 4,
            _ => 1,
        };
        return if width > back { back } else { 0 };
    }
    0
}

impl Output {
    /// Reads `output` of a task, `running` or ended: its file when `local`
    /// names one, else its `url` on `endpoint`. `wake` is called from
    /// another thread whenever there is something to take.
    pub fn open(endpoint: Option<&Endpoint>, output: &TaskOutput, running: bool, wake: Wake) -> Result<Output, String> {
        match (&output.path, &output.url) {
            (Some(path), _) if output.local => Ok(Output::file(PathBuf::from(path), running, wake)),
            (_, Some(url)) => {
                let endpoint = endpoint.ok_or("the task's output is on its producer's endpoint, which isn't reachable")?;
                Ok(Output::fetch(endpoint, on_endpoint(url)?, running, wake))
            }
            _ => Err("the task's output can't be read from this machine".into()),
        }
    }

    fn shared(running: bool, wake: Wake) -> Arc<Shared> {
        Arc::new(Shared {
            read: Mutex::default(),
            finished: AtomicBool::new(!running),
            gone: AtomicBool::new(false),
            finish: Notify::new(),
            wake,
        })
    }

    fn file(path: PathBuf, running: bool, wake: Wake) -> Output {
        let shared = Output::shared(running, wake);
        let tailing = shared.clone();
        let tail = thread::Builder::new().name("wheelhouse-task-output".into()).spawn(move || tail(&path, &tailing));
        match tail {
            Ok(tail) => Output { shared, tail: Some(tail), _endpoint: None },
            Err(error) => {
                shared.end(Err(error.to_string()));
                Output { shared, tail: None, _endpoint: None }
            }
        }
    }

    fn fetch(endpoint: &Endpoint, url: Url, running: bool, wake: Wake) -> Output {
        let shared = Output::shared(running, wake);
        let (link, fetching) = (endpoint.0.link.clone(), shared.clone());
        endpoint.0.runtime.spawn(async move { fetch(&link, url, &fetching).await });
        Output { shared, tail: None, _endpoint: Some(endpoint.clone()) }
    }

    /// The task has ended: what is left is read, and then the output ends.
    pub fn finish(&self) {
        self.shared.finished.store(true, Ordering::Release);
        self.shared.finish.notify_one();
        if let Some(tail) = &self.tail {
            tail.thread().unpark();
        }
    }

    /// What came since the last look.
    pub fn take(&self) -> OutputRead {
        let mut pending = self.shared.read.lock().unwrap();
        let end = pending.read.end.clone();
        OutputRead { end, ..std::mem::take(&mut pending.read) }
    }
}

impl Drop for Output {
    fn drop(&mut self) {
        self.shared.gone.store(true, Ordering::Release);
        self.finish();
    }
}

fn tail(path: &PathBuf, shared: &Shared) {
    let mut offset = 0;
    loop {
        // Read after the task ended, so this read takes everything.
        let finished = shared.finished.load(Ordering::Acquire);
        if shared.gone() {
            return;
        }
        // Only a regular file: opening a FIFO or a device would block, or
        // never end.
        let opened = metadata(path).and_then(|meta| match meta.is_file() {
            true => File::open(path),
            false => Err(Error::other(format!("{} is not a regular file", path.display()))),
        });
        match opened {
            Ok(mut file) => {
                let length = file.metadata().map_or(offset, |m| m.len());
                let reset = length < offset;
                if reset {
                    offset = 0;
                }
                let mut bytes = vec![];
                if let Err(error) = file.seek(SeekFrom::Start(offset)).and_then(|_| file.read_to_end(&mut bytes)) {
                    return shared.end(Err(error.to_string()));
                }
                offset += bytes.len() as u64;
                shared.push(&bytes, reset);
            }
            // A running task may not have written it yet.
            Err(error) if finished || error.kind() != ErrorKind::NotFound => return shared.end(Err(error.to_string())),
            Err(_) => {}
        }
        if finished {
            return shared.end(Ok(()));
        }
        thread::park_timeout(TAIL_EVERY);
    }
}

async fn fetch(link: &Link, url: Url, shared: &Shared) {
    // The producer answers with all of the output so far, which its own
    // buffer bounds; reading only what is new is #780.
    let mut seen = 0;
    loop {
        let finished = shared.finished.load(Ordering::Acquire);
        if shared.gone() {
            return;
        }
        match get(link, url.clone(), "*/*").await {
            Ok(body) => {
                let reset = body.bytes.len() < seen;
                if reset {
                    seen = 0;
                }
                shared.push(&body.bytes[seen..], reset);
                seen = body.bytes.len();
            }
            // A running task's output may not be served until it has some.
            Err((Some(StatusCode::NOT_FOUND), _)) if !finished => {}
            Err((Some(status), why)) if status.is_client_error() => return shared.end(Err(why)),
            Err((_, why)) if finished => return shared.end(Err(why)),
            Err(_) => {}
        }
        if finished {
            return shared.end(Ok(()));
        }
        tokio::select! {
            _ = sleep(TAIL_EVERY) => {}
            _ = shared.finish.notified() => {}
        }
    }
}
