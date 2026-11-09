//! A client of the Agent View protocol, decided on wheelhouse#691: HTTP on a
//! Local Endpoint, one watch stream per thread, older pages on request.
//! A producer on another host is reached over an ssh forward (#737).
mod forward;

use std::collections::HashMap;
use std::io;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{mpsc, Arc, Mutex, Weak};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use conversation_events::{Event, Hello, Page};
use reqwest::header::{ACCEPT, CONTENT_TYPE};
use reqwest::{Method, Request, StatusCode, Url};
use rigging_http_client::{http_channel_label, HttpClient, ReqwestHttpClient};
use rigging_http_wire::{Encoding, Items, LIMITS};
use serde_json::Value;
use tokio::runtime::{Builder, Handle};
use tokio::sync::{oneshot, Notify};
use tokio::time::{sleep, timeout};

use forward::Forward;

/// Called from the endpoint's thread when something arrives.
pub type Wake = Arc<dyn Fn() + Send + Sync>;

/// How long a post waits on the producer.
const POST_TIMEOUT: Duration = Duration::from_secs(30);
const RETRY_FIRST: Duration = Duration::from_millis(250);
const RETRY_MOST: Duration = Duration::from_secs(5);

/// The most events a watch holds for its host. Past it the watch waits for the
/// host to take half; one that doesn't within [`HOST_AWAY`], as while its View
/// is hidden, loses the stream, and once it has taken half the watch starts
/// over from a snapshot.
pub const MOST_PENDING: usize = 4096;
/// A host taking events, if slower than they come, does so within this.
const HOST_AWAY: Duration = Duration::from_secs(1);

/// Where a producer's Local Endpoint is.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum Route {
    Local(PathBuf),
    /// A socket on an ssh destination, forwarded to one here.
    Ssh {
        host: String,
        socket: String,
    },
}

/// One Local Endpoint: its client, the ssh forward it goes through if any,
/// and the thread its requests run on. Clones share them; the thread and
/// the forward end with the last.
#[derive(Clone)]
pub struct Endpoint(Arc<Shared>);

struct Shared {
    link: Arc<Link>,
    runtime: Handle,
    stop: Option<oneshot::Sender<()>>,
    thread: Option<JoinHandle<()>>,
}

impl Drop for Shared {
    // Joins the endpoint's thread, so the last clone mustn't drop on it: tasks
    // there hold the link, never an Endpoint.
    fn drop(&mut self) {
        self.stop.take();
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

/// Every endpoint in use, so each has one client and one thread however
/// many watches and posts share it, as each Cleat route has one Connection.
static SHARED: Mutex<Option<HashMap<Route, Weak<Shared>>>> = Mutex::new(None);

impl Endpoint {
    /// The endpoint at `route`, shared with everything else using it.
    pub fn shared(route: &Route) -> io::Result<Endpoint> {
        let mut shared = SHARED.lock().unwrap();
        let shared = shared.get_or_insert_default();
        shared.retain(|_, endpoint| endpoint.strong_count() > 0);
        if let Some(endpoint) = shared.get(route).and_then(Weak::upgrade) {
            return Ok(Endpoint(endpoint));
        }
        let endpoint = Endpoint::open(route)?;
        shared.insert(route.clone(), Arc::downgrade(&endpoint.0));
        Ok(endpoint)
    }

    /// An endpoint of its own, shared with nothing else. Over ssh, the
    /// forward starts with the first request.
    pub fn open(route: &Route) -> io::Result<Endpoint> {
        let forward = match route {
            Route::Local(_) => None,
            Route::Ssh { host, socket } => Some(Forward::new(host, socket)?),
        };
        let path = match (route, &forward) {
            (_, Some(forward)) => forward.local(),
            (Route::Local(path), None) => path.as_path(),
            (Route::Ssh { .. }, None) => unreachable!("an ssh route has its forward"),
        };
        let client = ReqwestHttpClient::local(path).map_err(io::Error::other)?;
        let runtime = Builder::new_current_thread().enable_all().build()?;
        let handle = runtime.handle().clone();
        let (stop, stopped) = oneshot::channel::<()>();
        let thread = thread::Builder::new().name("wheelhouse-agent".into()).spawn(move || {
            let _ = runtime.block_on(stopped);
            // Not a plain drop, which would wait on a stream that never ends.
            runtime.shutdown_background();
        })?;
        let link = Arc::new(Link { client, forward });
        Ok(Endpoint(Arc::new(Shared { link, runtime: handle, stop: Some(stop), thread: Some(thread) })))
    }

    /// Follows `thread` from its newest page; `wake` is called whenever
    /// updates wait in the watch.
    pub fn watch(&self, thread: &str, wake: Wake) -> Watch {
        let (sender, updates) = mpsc::channel();
        let sink = Sink { sender, woken: Arc::new(AtomicBool::new(false)), wake, pending: Arc::default(), taken: Arc::default() };
        let (stop, stopped) = oneshot::channel();
        let follow = follow(self.0.link.clone(), thread.to_owned(), sink.clone());
        self.0.runtime.spawn(async move {
            tokio::select! {
                _ = stopped => {}
                _ = follow => {}
            }
        });
        Watch { endpoint: self.clone(), thread: thread.to_owned(), updates, sink, _stop: stop }
    }

    /// Whether both share one client, forward and thread.
    pub fn same(&self, other: &Endpoint) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }

    /// POSTs `body` (JSON) to `path`, answered in the background.
    pub fn post(&self, path: &str, body: Vec<u8>) -> Post {
        let status = Arc::new(Mutex::new(PostStatus::Sending));
        let settled = status.clone();
        let link = self.0.link.clone();
        let path = path.to_owned();
        self.0.runtime.spawn(async move {
            let outcome = post(&link, &path, body).await;
            *settled.lock().unwrap() = match outcome {
                Ok(()) => PostStatus::Accepted,
                Err(why) => PostStatus::Failed(why),
            };
        });
        Post { _endpoint: self.clone(), status }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum PostStatus {
    Sending,
    Accepted,
    /// Why: the producer's error body, or the transport's.
    Failed(String),
}

/// A post in flight or settled; it keeps its endpoint open until dropped.
pub struct Post {
    _endpoint: Endpoint,
    status: Arc<Mutex<PostStatus>>,
}

impl Post {
    pub fn status(&self) -> PostStatus {
        self.status.lock().unwrap().clone()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Update {
    /// The stream (re)started. With a snapshot, what was read before is
    /// superseded: the events that follow restate the thread.
    Hello(Hello),
    Event(Event),
    /// The answer to [`Watch::older`].
    Page(Result<Page, String>),
    Connection(Connection),
}

#[derive(Clone, Debug, PartialEq)]
pub enum Connection {
    Open,
    /// Dropped or refused for now; the watch connects again by itself.
    Retrying {
        attempt: u32,
        why: String,
    },
    /// Refused for good, as for a thread the producer doesn't have.
    Failed(String),
}

/// A thread's watch stream. Dropping it ends the stream.
pub struct Watch {
    endpoint: Endpoint,
    thread: String,
    updates: mpsc::Receiver<Update>,
    sink: Sink,
    _stop: oneshot::Sender<()>,
}

impl Watch {
    pub fn endpoint(&self) -> &Endpoint {
        &self.endpoint
    }

    /// What came since the last look; anything after wakes the host again.
    pub fn take(&self) -> impl Iterator<Item = Update> + '_ {
        self.sink.woken.store(false, Ordering::Release);
        self.updates.try_iter().inspect(|update| {
            if matches!(update, Update::Event(_)) && self.sink.pending.fetch_sub(1, Ordering::AcqRel) == MOST_PENDING / 2 + 1 {
                self.sink.taken.notify_one();
            }
        })
    }

    /// Asks for up to `limit` completed items before `seq`; the answer comes
    /// as an [`Update::Page`].
    pub fn older(&self, before: u64, limit: u32) {
        let (link, sink) = (self.endpoint.0.link.clone(), self.sink.clone());
        let url = thread_url(&self.thread, &format!("before={before}&limit={limit}"));
        self.endpoint.0.runtime.spawn(async move {
            sink.send(Update::Page(page(&link, url).await));
        });
    }
}

#[derive(Clone)]
struct Sink {
    sender: mpsc::Sender<Update>,
    /// A wake is on its way and the host hasn't looked since: a snapshot of
    /// thousands of events wakes it once.
    woken: Arc<AtomicBool>,
    wake: Wake,
    /// Events sent and not yet taken.
    pending: Arc<AtomicUsize>,
    /// Signalled as the host takes `pending` down to half of [`MOST_PENDING`].
    taken: Arc<Notify>,
}

impl Sink {
    fn send(&self, update: Update) {
        if matches!(update, Update::Event(_)) {
            self.pending.fetch_add(1, Ordering::AcqRel);
        }
        if self.sender.send(update).is_ok() && !self.woken.swap(true, Ordering::AcqRel) {
            (self.wake)();
        }
    }

    fn full(&self) -> bool {
        self.pending.load(Ordering::Acquire) >= MOST_PENDING
    }

    async fn caught_up(&self) {
        while self.pending.load(Ordering::Acquire) > MOST_PENDING / 2 {
            self.taken.notified().await;
        }
    }
}

/// The stream, connected again after each drop: `?after=` the last `seq`
/// read, so nothing repeats unless the producer re-snapshots.
async fn follow(link: Arc<Link>, thread: String, sink: Sink) {
    let mut last = None;
    let mut attempt = 0;
    loop {
        let mut opened = None;
        let why = match stream(&link, &thread, &mut last, &mut opened, &sink).await {
            Ok(()) => "the stream ended".to_owned(),
            Err(Refusal::Retry(why)) => why,
            Err(Refusal::Behind) => {
                // What waits still reads in order; the snapshot restates the rest.
                last = None;
                sink.caught_up().await;
                continue;
            }
            Err(Refusal::Fatal(why)) => {
                sink.send(Update::Connection(Connection::Failed(why)));
                return;
            }
        };
        // A stream that stayed up a while was no failed attempt.
        if opened.is_some_and(|at: Instant| at.elapsed() >= RETRY_MOST) {
            attempt = 0;
        }
        attempt += 1;
        sink.send(Update::Connection(Connection::Retrying { attempt, why }));
        sleep(RETRY_FIRST.saturating_mul(1 << attempt.min(5)).min(RETRY_MOST)).await;
    }
}

enum Refusal {
    Retry(String),
    Fatal(String),
    /// The host has more than [`MOST_PENDING`] events to take.
    Behind,
}

async fn stream(link: &Link, thread: &str, last: &mut Option<u64>, opened: &mut Option<Instant>, sink: &Sink) -> Result<(), Refusal> {
    let query = last.map(|seq| format!("after={seq}")).unwrap_or_default();
    let mut request = Request::new(Method::GET, thread_url(thread, &query));
    let accept = format!("{}, {};q=0.5", Encoding::Cbor.sequence_type(), Encoding::Json.sequence_type());
    request.headers_mut().insert(ACCEPT, accept.parse().unwrap());
    let label = http_channel_label("GET", request.url().as_str());
    let heard = link.ready().await.map_err(Refusal::Retry)?;
    let response = match link.client.execute_streaming(request, &label).await {
        Ok(response) => response,
        Err(error) => return Err(Refusal::Retry(link.why(heard, error).await)),
    };
    let status = response.status();
    let encoding = response.headers().get(CONTENT_TYPE).and_then(|v| v.to_str().ok()).and_then(Encoding::of_sequence);
    let mut body = response.into_body();
    if !status.is_success() {
        let why = body.chunk().await.ok().flatten().map(|b| String::from_utf8_lossy(&b).into_owned()).unwrap_or_default();
        let why = if why.is_empty() { status.to_string() } else { why };
        // A producer that is starting or overloaded may answer later; one
        // that refuses the request won't.
        let fatal = status.is_client_error() && status.as_u16() != 408 && status.as_u16() != 429;
        return Err(if fatal { Refusal::Fatal(why) } else { Refusal::Retry(why) });
    }
    let mut items = Items::new(encoding.unwrap_or(Encoding::Json));
    // In a stream that resumes, an event at or before `last` is a repeat; a
    // snapshot restates open items at their own, earlier `seq`.
    let mut resumed = None;
    while let Some(chunk) = body.chunk().await.map_err(Refusal::Retry)? {
        items.push(&chunk);
        while let Some(value) = items.take::<Value>().map_err(Refusal::Retry)? {
            // A stream starts with a hello, and one that doesn't won't on a
            // retry either. Past it, an event this version can't read is
            // skipped, as in a file.
            let event = match serde_json::from_value::<Event>(value) {
                Ok(event) => event,
                Err(error) if resumed.is_none() => return Err(Refusal::Fatal(format!("no hello this version reads: {error}"))),
                Err(_) => continue,
            };
            match event {
                Event::Hello(hello) => {
                    resumed = Some(hello.snapshot.is_none() && last.is_some());
                    *opened = Some(Instant::now());
                    sink.send(Update::Hello(hello));
                    sink.send(Update::Connection(Connection::Open));
                }
                event if resumed.is_none() => return Err(Refusal::Fatal(format!("{} before the hello", type_of(&event)))),
                event => {
                    let seq = event.seq();
                    if resumed == Some(true) && seq > 0 && last.is_some_and(|last| seq <= last) {
                        continue;
                    }
                    if seq > 0 {
                        *last = Some(last.map_or(seq, |last| last.max(seq)));
                    }
                    if sink.full() && timeout(HOST_AWAY, sink.caught_up()).await.is_err() {
                        return Err(Refusal::Behind);
                    }
                    sink.send(Update::Event(event));
                }
            }
        }
    }
    if items.is_empty() {
        Ok(())
    } else {
        Err(Refusal::Retry("the stream ended mid-event".into()))
    }
}

fn type_of(event: &Event) -> String {
    serde_json::to_value(event).ok().and_then(|v| v.get("type").and_then(Value::as_str).map(str::to_owned)).unwrap_or_default()
}

async fn page(link: &Link, url: Url) -> Result<Page, String> {
    let mut request = Request::new(Method::GET, url);
    request.headers_mut().insert(ACCEPT, "application/cbor, application/json;q=0.5".parse().unwrap());
    let label = http_channel_label("GET", request.url().as_str());
    let heard = link.ready().await?;
    let response = match link.client.execute(request, &label).await {
        Ok(response) => response,
        Err(error) => return Err(link.why(heard, error).await),
    };
    if !response.status().is_success() {
        return Err(failure(response.status(), response.body()));
    }
    let cbor = response.headers().get(CONTENT_TYPE).is_some_and(|v| v.as_bytes().starts_with(b"application/cbor"));
    if cbor {
        flotilla_cbor::from_slice(response.body(), &LIMITS).map_err(|error| error.to_string())
    } else {
        serde_json::from_slice(response.body()).map_err(|error| error.to_string())
    }
}

async fn post(link: &Link, path: &str, body: Vec<u8>) -> Result<(), String> {
    let mut url = base();
    url.set_path(path);
    let mut request = Request::new(Method::POST, url);
    request.headers_mut().insert(CONTENT_TYPE, "application/json".parse().unwrap());
    *request.timeout_mut() = Some(POST_TIMEOUT);
    *request.body_mut() = Some(body.into());
    let label = http_channel_label("POST", request.url().as_str());
    let heard = link.ready().await?;
    let response = match link.client.execute(request, &label).await {
        Ok(response) => response,
        Err(error) => return Err(link.why(heard, error).await),
    };
    if response.status().is_success() {
        Ok(())
    } else {
        Err(failure(response.status(), response.body()))
    }
}

/// The client and the forward its requests go through, if any.
struct Link {
    client: ReqwestHttpClient,
    forward: Option<Forward>,
}

impl Link {
    /// The forward up, ready for a request; Ok has what [`Link::why`] needs.
    async fn ready(&self) -> Result<u64, String> {
        match &self.forward {
            Some(forward) => forward.ready().await,
            None => Ok(0),
        }
    }

    async fn why(&self, heard: u64, error: String) -> String {
        match &self.forward {
            Some(forward) => forward.why(heard, error).await,
            None => error,
        }
    }
}

/// The producer's error body says why; a bare status if it sent none.
fn failure(status: StatusCode, body: &[u8]) -> String {
    if body.is_empty() {
        status.to_string()
    } else {
        String::from_utf8_lossy(body).into_owned()
    }
}

/// The client reaches its endpoint whatever the host; the URL carries only
/// the path and query.
fn base() -> Url {
    Url::parse("http://agent/").unwrap()
}

fn thread_url(thread: &str, query: &str) -> Url {
    let mut url = base();
    url.path_segments_mut().unwrap().extend(["threads", thread, "events"]);
    url.set_query((!query.is_empty()).then_some(query));
    url
}
