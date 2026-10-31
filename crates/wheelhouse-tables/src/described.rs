//! A described table (#529): binnacle-described's source over a table a
//! server describes, read live over HTTP. The table is opened by its link;
//! the core holds the source, and a thread of its own runs the transport,
//! waking the host whenever an event comes so the table redraws as data
//! arrives.

use std::path::PathBuf;
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, SystemTime};

use binnacle_core::{Axis, Grid, Key, Stamped};
use binnacle_described::{DescribedSource, HostRequest, Received, Scope};
use binnacle_described_http::{Endpoint, Event, Options, Transport};
use binnacle_format::{format, FixedOffset, FormatContext, FormatOptions, Timestamp, Value};

use crate::{Align, Cell};

/// Called from the transport's thread whenever an event is waiting.
pub type Wake = Box<dyn Fn() + Send + Sync>;

/// How often a table with relative dates redraws, to keep them current.
const RELATIVE_DATES: Duration = Duration::from_secs(1);

/// A table's link, taken apart: where the server is, the table and the
/// rows' scope.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Link {
    pub endpoint: Endpoint,
    pub table: String,
    pub namespace: String,
    pub wheres: Vec<String>,
}

impl Link {
    /// Reads `http://host[:port][/prefix]/tables/{table}[?query]`, or
    /// `http+unix://{socket path, percent-encoded}/tables/{table}[?query]`.
    /// The query may give `namespace` (`default` without one, as the
    /// server reads it) and any number of `where` clauses.
    pub fn parse(link: &str) -> Option<Link> {
        let (link, query) = link.split_once('?').unwrap_or((link, ""));
        let (base, table) = link.rsplit_once("/tables/")?;
        let table = decode(table)?;
        if table.is_empty() || table.contains('/') {
            return None;
        }
        let endpoint = if let Some(socket) = base.strip_prefix("http+unix://") {
            let socket = decode(socket)?;
            if socket.is_empty() {
                return None;
            }
            Endpoint::Unix(PathBuf::from(socket))
        } else {
            let host = base.strip_prefix("http://")?;
            if host.is_empty() || host.starts_with('/') {
                return None;
            }
            Endpoint::Tcp(base.to_owned())
        };
        let mut namespace = None;
        let mut wheres = Vec::new();
        for pair in query.split('&').filter(|p| !p.is_empty()) {
            let (name, value) = pair.split_once('=').unwrap_or((pair, ""));
            let value = decode(&value.replace('+', " "))?;
            match name {
                "namespace" => namespace = Some(value),
                "where" => wheres.push(value),
                // Anything else is the server's business, not the scope's.
                _ => {}
            }
        }
        let namespace = namespace.unwrap_or_else(|| "default".into());
        Some(Link { endpoint, table, namespace, wheres })
    }

    fn scope(&self) -> Scope {
        self.wheres.iter().fold(Scope::new(self.namespace.clone()), |scope, clause| scope.with_where(clause.clone()))
    }
}

/// `text` with its `%XX` escapes decoded, or `None` if one is malformed or
/// the result is not UTF-8.
fn decode(text: &str) -> Option<String> {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' {
            let hex = std::str::from_utf8(bytes.get(i + 1..i + 3)?).ok()?;
            out.push(u8::from_str_radix(hex, 16).ok()?);
            i += 3;
        } else {
            out.push(bytes[i]);
            i += 1;
        }
    }
    String::from_utf8(out).ok()
}

/// The transport on a thread of its own: it serves the source's requests
/// and sends back what comes, waking the host after each event.
struct Watcher {
    requests: Option<tokio::sync::mpsc::UnboundedSender<HostRequest>>,
    events: mpsc::Receiver<Event>,
    thread: Option<thread::JoinHandle<()>>,
}

impl Watcher {
    fn start(options: Options, wake: Wake) -> std::io::Result<Watcher> {
        let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build()?;
        let (requests, mut incoming) = tokio::sync::mpsc::unbounded_channel::<HostRequest>();
        let (events, outgoing) = mpsc::channel();
        let transport = Transport::new(options);
        let wake = std::sync::Arc::new(wake);
        let thread = thread::Builder::new().name("wheelhouse-table".into()).spawn(move || {
            let local = tokio::task::LocalSet::new();
            local.block_on(&runtime, async move {
                while let Some(request) = incoming.recv().await {
                    let (transport, events, wake) = (transport.clone(), events.clone(), wake.clone());
                    tokio::task::spawn_local(async move {
                        transport
                            .watch(&request, |event| {
                                if events.send(event).is_ok() {
                                    wake();
                                }
                            })
                            .await;
                    });
                }
            });
        })?;
        Ok(Watcher { requests: Some(requests), events: outgoing, thread: Some(thread) })
    }

    fn open(&self, request: HostRequest) {
        if let Some(requests) = &self.requests {
            let _ = requests.send(request);
        }
    }
}

impl Drop for Watcher {
    fn drop(&mut self) {
        // Closing the channel ends the thread's loop; a watch still
        // streaming is dropped with its runtime.
        self.requests.take();
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

/// How the watch stands, beyond what the source's status says.
#[derive(Clone, Debug, Default, PartialEq)]
enum Connection {
    #[default]
    Open,
    Retrying {
        attempt: u32,
        status: Option<u16>,
        body: String,
    },
    Failed {
        status: u16,
        body: String,
    },
}

/// A described table: its source and the watch that feeds it.
pub struct Described {
    pub(crate) source: DescribedSource,
    watcher: Watcher,
    connection: Connection,
    zone: FixedOffset,
}

impl Described {
    /// Opens the table at `link`, its dates drawn `utc_offset` seconds east
    /// of UTC; `wake` is called from another thread whenever data comes.
    pub fn open(link: &Link, utc_offset: i32, wake: Wake) -> std::io::Result<Described> {
        let watcher = Watcher::start(Options::new(link.endpoint.clone()), wake)?;
        let source = DescribedSource::new(link.table.clone(), link.scope()).zone(FixedOffset(utc_offset));
        Ok(Described { source, watcher, connection: Connection::Open, zone: FixedOffset(utc_offset) })
    }

    /// Feeds the source what came, telling `grid` what changed, and serves
    /// what the source asks. Returns how long until the table needs drawing
    /// again by itself (relative dates), or `None`.
    pub fn tick(&mut self, grid: &mut Grid) -> Option<Duration> {
        while let Ok(event) = self.watcher.events.try_recv() {
            let received = match event {
                Event::Message(message) => {
                    self.connection = Connection::Open;
                    self.source.receive(message)
                }
                Event::Ended => self.source.ended(),
                Event::Retrying { attempt, status, body, .. } => {
                    self.connection = Connection::Retrying { attempt, status, body: text(&body) };
                    continue;
                }
                Event::Failed { status, body } => {
                    self.connection = Connection::Failed { status, body: text(&body) };
                    continue;
                }
                // Bytes that are no message: the stream goes on.
                _ => continue,
            };
            if let Received::Applied(notices) = received {
                let described = notices.iter().any(|(axis, _)| *axis == Axis::Cols);
                for (axis, notice) in notices {
                    grid.notify(axis, notice);
                }
                if let Some((spec, totals)) = self.source.poll_view() {
                    grid.view_changed(spec, totals);
                }
                // New columns: say what each supports.
                if described {
                    self.source.declare(grid);
                }
            }
        }
        while let Some(request) = self.source.poll_request() {
            self.watcher.open(request);
        }
        let options = FormatOptions::default();
        let relative = self.source.columns().iter().any(|c| options.relative_dates(c.role()));
        (relative && self.source.row_count() != 0).then_some(RELATIVE_DATES)
    }

    /// What to say about the table above it, if anything: connecting,
    /// retrying, failed, or only part of its rows shown.
    pub fn status(&self) -> Option<String> {
        let status = self.source.status();
        match &self.connection {
            Connection::Failed { status, body } => {
                return Some(match (status, body.is_empty()) {
                    (0, _) => format!("Couldn't read the table: {body}"),
                    (_, true) => format!("Couldn't read the table: HTTP {status}"),
                    (_, false) => format!("Couldn't read the table: HTTP {status}: {body}"),
                });
            }
            Connection::Retrying { attempt, status, body } => {
                let why = match status {
                    Some(status) => format!("HTTP {status}"),
                    None => body.clone(),
                };
                return Some(format!("Reconnecting (attempt {attempt}): {why}"));
            }
            Connection::Open => {}
        }
        if !status.described {
            return Some("Connecting…".into());
        }
        if status.partial {
            return Some(format!("Showing the first {} rows", self.source.row_count()));
        }
        None
    }

    pub fn cell(&self, row: Key, col: Key) -> Cell {
        let Some(column) = self.source.column(col) else {
            return Cell { text: String::new(), align: Align::Left };
        };
        let value = self.source.cell(row, col).unwrap_or(&Value::Null);
        let now = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default();
        let ctx = FormatContext::new(Timestamp::from_unix_nanos(now.as_nanos() as i128), &self.zone);
        let formatted = format(value, column.unit(), column.role(), &FormatOptions::default(), &ctx);
        let align = match formatted.align {
            binnacle_format::Align::Start => Align::Left,
            binnacle_format::Align::End => Align::Right,
        };
        Cell { text: formatted.text, align }
    }

    pub fn header(&self, axis: Axis, key: Key, position: u64) -> String {
        match axis {
            Axis::Cols => self.source.column(key).map(|c| c.header().to_owned()).unwrap_or_default(),
            Axis::Rows => position.saturating_add(1).to_string(),
        }
    }

    pub fn pump(&mut self, grid: &mut Grid, seen: impl FnMut(&mut Grid, Stamped)) {
        binnacle_memory::pump(grid, &mut self.source, seen);
    }
}

/// The start of a server's answer, as one line of text.
fn text(body: &[u8]) -> String {
    const MOST: usize = 120;
    let text = String::from_utf8_lossy(body);
    let line = text.split_whitespace().collect::<Vec<_>>().join(" ");
    match line.char_indices().nth(MOST) {
        Some((at, _)) => format!("{}…", &line[..at]),
        None => line,
    }
}
