//! The client against a stand-in producer on a Unix socket, with synthetic
//! events only (the repo is public).
#![cfg(unix)]

use std::convert::Infallible;
use std::path::PathBuf;
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use axum::body::{Body, Bytes};
use axum::extract::{Path, Query, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::Router;
use conversation_events::{Event, Hello, Page, Snapshot, TextMessageContent, CONTRACT};
use futures_util::stream::{self, StreamExt};
use rigging_http_wire::Encoding;
use serde::Deserialize;
use tokio::net::UnixListener;
use tokio::sync::{broadcast, watch};
use wheelhouse_agent::{Connection, Endpoint, PostStatus, Update, Watch};

const THREAD: &str = "t1";

fn content(seq: u64) -> Event {
    Event::TextMessageContent(TextMessageContent { thread_id: THREAD.into(), seq, message_id: "m1".into(), delta: format!("part {seq}") })
}

/// The producer's side of the protocol, with knobs for what a real one does
/// now and then: drop every stream, forget old `seq`s, replay a few twice.
struct Producer {
    events: Mutex<Vec<Event>>,
    /// The oldest `seq` a resume can still start after; older re-snapshots.
    buffered_from: Mutex<u64>,
    /// How many of the newest events a snapshot holds.
    page: usize,
    live: broadcast::Sender<Event>,
    /// Bumped to drop every open stream.
    cut: watch::Sender<u32>,
    /// Each stream request's query, in order.
    queries: Mutex<Vec<String>>,
    json_only: bool,
    /// A resume starts one event early, so the client must drop the repeat.
    overlap: bool,
}

impl Producer {
    fn new(count: u64, page: usize) -> Arc<Producer> {
        Arc::new(Producer {
            events: Mutex::new((1..=count).map(content).collect()),
            buffered_from: Mutex::new(1),
            page,
            live: broadcast::channel(256).0,
            cut: watch::channel(0).0,
            queries: Mutex::default(),
            json_only: false,
            overlap: false,
        })
    }

    fn emit(&self, seq: u64) {
        self.events.lock().unwrap().push(content(seq));
        let _ = self.live.send(content(seq));
    }

    fn drop_streams(&self) {
        self.cut.send_modify(|n| *n += 1);
    }
}

#[derive(Deserialize)]
struct Params {
    after: Option<u64>,
    before: Option<u64>,
    limit: Option<usize>,
}

async fn events(State(p): State<Arc<Producer>>, Path(thread): Path<String>, Query(q): Query<Params>, headers: HeaderMap) -> Response {
    if thread != THREAD {
        return (StatusCode::NOT_FOUND, "no thread t2").into_response();
    }
    if let Some(before) = q.before {
        let all = p.events.lock().unwrap().clone();
        let older: Vec<Event> = all.into_iter().filter(|e| e.seq() < before).collect();
        let start = older.len().saturating_sub(q.limit.unwrap_or(10));
        let oldest_seq = older.get(start).map_or(0, Event::seq);
        let page = Page { events: older[start..].to_vec(), earlier_items: start as u64, oldest_seq };
        return Encoding::negotiate(&headers).unwrap().render(StatusCode::OK, page);
    }
    p.queries.lock().unwrap().push(q.after.map(|n| format!("after={n}")).unwrap_or_default());
    // Subscribed before reading what is there, so nothing falls between.
    let (live, mut cut) = (p.live.subscribe(), p.cut.subscribe());
    cut.mark_unchanged();
    let all = p.events.lock().unwrap().clone();
    let encoding = if p.json_only { Encoding::Json } else { Encoding::negotiate_sequence(&headers).unwrap() };
    let mut hello = Hello { contract: CONTRACT, thread_id: THREAD.into(), ..Hello::default() };
    let first: Vec<Event> = match q.after {
        Some(after) if after + 1 >= *p.buffered_from.lock().unwrap() => {
            let from = if p.overlap { after.saturating_sub(1) } else { after };
            all.into_iter().filter(|e| e.seq() > from).collect()
        }
        _ => {
            let start = all.len().saturating_sub(p.page);
            hello.snapshot = Some(Snapshot { earlier_items: start as u64, oldest_seq: all.get(start).map_or(0, Event::seq) });
            all[start..].to_vec()
        }
    };
    let sent = first.iter().map(Event::seq).max().unwrap_or(0);
    let head = stream::iter(std::iter::once(Event::Hello(hello)).chain(first));
    let tail = stream::unfold((live, cut), move |(mut live, mut cut)| async move {
        loop {
            tokio::select! {
                event = live.recv() => match event {
                    Ok(event) if event.seq() <= sent => continue,
                    Ok(event) => return Some((event, (live, cut))),
                    Err(_) => return None,
                },
                _ = cut.changed() => return None,
            }
        }
    });
    let body = head.chain(tail).map(move |event| Ok::<_, Infallible>(Bytes::from(encoding.encode_item(&event).unwrap())));
    ([("content-type", encoding.sequence_type())], Body::from_stream(body)).into_response()
}

async fn run(body: String) -> Response {
    if body.contains("second answer") {
        (StatusCode::CONFLICT, r#"{"error":"answered","by":"m7"}"#).into_response()
    } else {
        StatusCode::ACCEPTED.into_response()
    }
}

/// Serves `producer` on a socket of its own until the test ends.
fn serve(name: &str, producer: Arc<Producer>) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("wh-agent-{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let socket = dir.join("producer.sock");
    let app = Router::new().route("/threads/{id}/events", get(events)).route("/threads/{id}/run", post(run)).with_state(producer);
    let (ready, listening) = mpsc::channel();
    let path = socket.clone();
    thread::spawn(move || {
        let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        runtime.block_on(async move {
            let listener = UnixListener::bind(&path).unwrap();
            ready.send(()).unwrap();
            axum::serve(listener, app).await.unwrap();
        });
    });
    listening.recv().unwrap();
    socket
}

/// A watch and everything it has delivered so far.
struct Watcher {
    watch: Watch,
    woken: mpsc::Receiver<()>,
    seen: Vec<Update>,
}

impl Watcher {
    fn open(endpoint: &Endpoint, thread: &str) -> Watcher {
        let (wake, woken) = mpsc::channel();
        let wake = Mutex::new(wake);
        let watch = endpoint.watch(
            thread,
            Arc::new(move || {
                let _ = wake.lock().unwrap().send(());
            }),
        );
        Watcher { watch, woken, seen: vec![] }
    }

    /// Waits until `done` holds of everything delivered.
    fn until(&mut self, what: &str, done: impl Fn(&[Update]) -> bool) {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            self.seen.extend(self.watch.take());
            if done(&self.seen) {
                return;
            }
            let left = deadline.checked_duration_since(Instant::now()).unwrap_or_else(|| panic!("no {what}: {:#?}", self.seen));
            let _ = self.woken.recv_timeout(left);
        }
    }

    fn watch_endpoint(&self) -> Endpoint {
        self.watch.endpoint().clone()
    }

    fn seqs(&self) -> Vec<u64> {
        self.seen.iter().filter_map(|u| if let Update::Event(e) = u { Some(e.seq()) } else { None }).collect()
    }

    fn hellos(&self) -> Vec<&Hello> {
        self.seen.iter().filter_map(|u| if let Update::Hello(h) = u { Some(h) } else { None }).collect()
    }
}

fn has_seq(seq: u64) -> impl Fn(&[Update]) -> bool {
    move |seen| seen.iter().any(|u| matches!(u, Update::Event(e) if e.seq() == seq))
}

#[test]
fn a_watcher_joins_at_the_newest_page_and_follows_in_either_encoding() {
    for json_only in [false, true] {
        let mut producer = Producer::new(30, 5);
        Arc::get_mut(&mut producer).unwrap().json_only = json_only;
        let socket = serve(&format!("join-{json_only}"), producer.clone());
        let endpoint = Endpoint::open(&socket).unwrap();
        let mut watcher = Watcher::open(&endpoint, THREAD);
        watcher.until("snapshot", has_seq(30));
        let snapshot = watcher.hellos()[0].snapshot.clone().unwrap();
        assert_eq!((snapshot.earlier_items, snapshot.oldest_seq), (25, 26));
        producer.emit(31);
        producer.emit(32);
        watcher.until("live events", has_seq(32));
        assert_eq!(watcher.seqs(), (26..=32).collect::<Vec<_>>(), "json only: {json_only}");
        assert!(watcher.seen.contains(&Update::Connection(Connection::Open)));
    }
}

#[test]
fn a_dropped_stream_resumes_after_its_last_seq_without_repeats() {
    let mut producer = Producer::new(10, 10);
    Arc::get_mut(&mut producer).unwrap().overlap = true;
    let socket = serve("resume", producer.clone());
    let endpoint = Endpoint::open(&socket).unwrap();
    let mut watcher = Watcher::open(&endpoint, THREAD);
    watcher.until("snapshot", has_seq(10));
    producer.drop_streams();
    watcher.until("a retry", |seen| seen.iter().any(|u| matches!(u, Update::Connection(Connection::Retrying { .. }))));
    producer.emit(11);
    watcher.until("the event after the drop", has_seq(11));
    assert_eq!(watcher.seqs(), (1..=11).collect::<Vec<_>>());
    assert_eq!(*producer.queries.lock().unwrap(), ["", "after=10"]);
    assert!(watcher.hellos()[1].snapshot.is_none());
}

#[test]
fn a_resume_from_an_evicted_seq_starts_over_with_a_snapshot() {
    let producer = Producer::new(10, 4);
    let socket = serve("evicted", producer.clone());
    let endpoint = Endpoint::open(&socket).unwrap();
    let mut watcher = Watcher::open(&endpoint, THREAD);
    watcher.until("snapshot", has_seq(10));
    for seq in 11..=20 {
        producer.events.lock().unwrap().push(content(seq));
    }
    *producer.buffered_from.lock().unwrap() = 15;
    producer.drop_streams();
    watcher.until("the second snapshot", |seen| seen.iter().filter(|u| matches!(u, Update::Hello(_))).count() == 2);
    watcher.until("its events", has_seq(20));
    let snapshot = watcher.hellos()[1].snapshot.clone().expect("a fresh snapshot, marked as one");
    assert_eq!((snapshot.earlier_items, snapshot.oldest_seq), (16, 17));
    assert_eq!(watcher.seqs(), [7, 8, 9, 10, 17, 18, 19, 20]);
}

#[test]
fn older_items_come_back_a_page_at_a_time() {
    let producer = Producer::new(30, 5);
    let socket = serve("older", producer);
    let endpoint = Endpoint::open(&socket).unwrap();
    let mut watcher = Watcher::open(&endpoint, THREAD);
    watcher.until("snapshot", has_seq(30));
    watcher.watch.older(26, 10);
    watcher.until("a page", |seen| seen.iter().any(|u| matches!(u, Update::Page(_))));
    let page = watcher.seen.iter().find_map(|u| if let Update::Page(p) = u { Some(p.clone()) } else { None }).unwrap().unwrap();
    assert_eq!(page.events.iter().map(Event::seq).collect::<Vec<_>>(), (16..=25).collect::<Vec<_>>());
    assert_eq!((page.earlier_items, page.oldest_seq), (15, 16));
}

#[test]
fn a_thread_the_producer_does_not_have_fails_for_good() {
    let producer = Producer::new(3, 3);
    let socket = serve("missing", producer.clone());
    let endpoint = Endpoint::open(&socket).unwrap();
    let mut watcher = Watcher::open(&endpoint, "t2");
    watcher.until("failure", |seen| seen.iter().any(|u| matches!(u, Update::Connection(Connection::Failed(_)))));
    assert_eq!(watcher.seen, [Update::Connection(Connection::Failed("no thread t2".into()))]);
}

#[test]
fn a_producer_that_is_not_there_yet_is_retried() {
    let dir = std::env::temp_dir().join(format!("wh-agent-absent-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let endpoint = Endpoint::open(&dir.join("nobody.sock")).unwrap();
    let mut watcher = Watcher::open(&endpoint, THREAD);
    watcher.until("two retries", |seen| seen.iter().filter(|u| matches!(u, Update::Connection(Connection::Retrying { .. }))).count() >= 2);
}

#[test]
fn a_post_is_accepted_or_fails_with_the_producers_reason() {
    let socket = serve("post", Producer::new(0, 0));
    let endpoint = Endpoint::open(&socket).unwrap();
    let settle = |body: &str| {
        let post = endpoint.post("/threads/t1/run", body.as_bytes().to_vec());
        let deadline = Instant::now() + Duration::from_secs(5);
        while post.status() == PostStatus::Sending && Instant::now() < deadline {
            thread::sleep(Duration::from_millis(5));
        }
        post.status()
    };
    assert_eq!(settle(r#"{"messages":[]}"#), PostStatus::Accepted);
    assert_eq!(settle(r#"{"resume":"second answer"}"#), PostStatus::Failed(r#"{"error":"answered","by":"m7"}"#.into()));
}

#[test]
fn watches_and_posts_of_one_endpoint_share_it_while_it_is_in_use() {
    let socket = serve("shared", Producer::new(3, 3));
    let first = Endpoint::shared(&socket).unwrap();
    let second = Endpoint::shared(&socket).unwrap();
    assert!(first.same(&second));
    let mut watcher = Watcher::open(&second, THREAD);
    drop((first, second));
    // The watch keeps it: a third use gets the same one.
    assert!(Endpoint::shared(&socket).unwrap().same(&watcher.watch_endpoint()));
    watcher.until("snapshot", has_seq(3));
}
