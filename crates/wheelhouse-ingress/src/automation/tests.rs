use super::*;
use hegel::generators as gs;
use hegel::TestCase;

/// A JSON value of the shapes the trees carry: objects, arrays, text,
/// booleans, integers (some beyond 2^53) and floats.
fn draw_value(tc: &TestCase, depth: u32) -> Value {
    let leaf = depth == 0;
    match tc.draw(gs::integers::<u8>().max_value(if leaf { 5 } else { 7 })) {
        0 => Value::Null,
        1 => tc.draw(gs::booleans()).into(),
        2 => tc.draw(gs::integers::<u64>()).into(),
        3 => tc.draw(gs::integers::<i64>()).into(),
        4 => tc.draw(gs::floats::<f64>().allow_nan(false).allow_infinity(false)).into(),
        5 => tc.draw(gs::text().max_size(8)).into(),
        6 => (0..tc.draw(gs::integers::<usize>().max_value(3))).map(|_| draw_value(tc, depth - 1)).collect(),
        _ => (0..tc.draw(gs::integers::<usize>().max_value(3)))
            .map(|_| (tc.draw(gs::text().max_size(6)), draw_value(tc, depth - 1)))
            .collect::<Map<_, _>>()
            .into(),
    }
}

/// What JSON carries for `value`, its large integers read back from
/// `{"u64": "..."}`.
fn read_json(bytes: &[u8]) -> Value {
    fn unwrap(value: Value) -> Value {
        match value {
            Value::Object(map) if map.len() == 1 && map.get("u64").is_some_and(Value::is_string) => {
                let text = map["u64"].as_str().unwrap();
                text.parse::<u64>().map(Value::from).unwrap_or_else(|_| text.parse::<i64>().unwrap().into())
            }
            Value::Array(items) => items.into_iter().map(unwrap).collect(),
            Value::Object(map) => map.into_iter().map(|(k, v)| (k, unwrap(v))).collect::<Map<_, _>>().into(),
            other => other,
        }
    }
    unwrap(serde_json::from_slice(bytes).unwrap())
}

/// Equal, but for a float's last bit: serde_json's default reader isn't
/// correctly rounded, though the text it reads is the float's shortest.
fn same(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Number(x), Value::Number(y)) if x.is_f64() || y.is_f64() => {
            let (x, y) = (x.as_f64().unwrap(), y.as_f64().unwrap());
            x == y || (x.to_bits() as i64 - y.to_bits() as i64).abs() <= 1
        }
        (Value::Array(x), Value::Array(y)) => x.len() == y.len() && x.iter().zip(y).all(|(x, y)| same(x, y)),
        (Value::Object(x), Value::Object(y)) => x.len() == y.len() && x.iter().all(|(k, v)| y.get(k).is_some_and(|w| same(v, w))),
        _ => a == b,
    }
}

// The same value both ways. The CBOR passes the profile's safe reader and
// is deterministic: reading and writing it again gives the same bytes.
#[hegel::test(test_cases = 300)]
fn json_and_cbor_encode_the_same_value_and_cbor_keeps_the_profile(tc: TestCase) {
    let value = draw_value(&tc, 3);
    let cbor = encode(&value, true).unwrap();
    flotilla_cbor::check(&cbor, &LIMITS).unwrap();
    let back: Value = flotilla_cbor::from_slice(&cbor, &LIMITS).unwrap();
    assert_eq!(back, value);
    assert_eq!(encode(&back, true).unwrap(), cbor);
    let json = encode(&value, false).unwrap();
    // Every integer JSON carries as a number is one readers keep exactly.
    fn safe(value: &Value) -> bool {
        match value {
            Value::Number(n) => match (n.as_u64(), n.as_i64()) {
                (Some(n), _) => n <= SAFE,
                (_, Some(n)) => n.unsigned_abs() <= SAFE,
                _ => true,
            },
            Value::Array(items) => items.iter().all(safe),
            Value::Object(map) => map.values().all(safe),
            _ => true,
        }
    }
    assert!(safe(&serde_json::from_slice(&json).unwrap()));
    // Objects that happen to look like {"u64": text} are left out.
    if !value.to_string().contains("\"u64\"") {
        assert!(same(&read_json(&json), &value), "{value}");
    }
}

#[test]
fn large_integers_render_as_u64_text_in_json() {
    let value = json!({"small": 1u64 << 53, "large": (1u64 << 53) + 1, "max": u64::MAX, "negative": -(1i64 << 60)});
    let json: Value = serde_json::from_slice(&encode(&value, false).unwrap()).unwrap();
    assert_eq!(json["small"], 1u64 << 53);
    assert_eq!(json["large"], json!({"u64": "9007199254740993"}));
    assert_eq!(json["max"], json!({"u64": "18446744073709551615"}));
    assert_eq!(json["negative"], json!({"u64": "-1152921504606846976"}));
    // CBOR carries them as integers.
    assert_eq!(flotilla_cbor::from_slice::<Value>(&encode(&value, true).unwrap(), &LIMITS).unwrap(), value);
}

const A: &str = "workspace/00000000-0000-0000-0000-00000000000a";
const B: &str = "workspace/00000000-0000-0000-0000-00000000000b";

fn node(id: &str, kind: &str, children: Vec<Value>) -> Value {
    let mut node = json!({"id": id, "kind": kind, "durable": true});
    if !children.is_empty() {
        node["children"] = children.into();
    }
    node
}

fn terminal(id: &str, session: &str, text: &str) -> Value {
    let mut view = node(id, "view", vec![]);
    view["props"] =
        json!({"content": "terminal", "terminal": {"session": session, "text": [text], "cursor": {"row": 0, "col": 0, "visible": true}}});
    view
}

/// Two workspaces: one with a split of a terminal and a sessions View, the
/// other with a terminal showing the same session.
fn nodes() -> Vec<Node> {
    let mut first = node(
        A,
        "workspace",
        vec![node(
            &format!("{A}/panel/1"),
            "panel",
            vec![
                node(&format!("{A}/panel/2"), "panel", vec![terminal(&format!("{A}/view/u:1"), "s1", "$ ls")]),
                node(&format!("{A}/panel/3"), "panel", vec![node(&format!("{A}/view/u:2"), "view", vec![])]),
            ],
        )],
    );
    first["props"] = json!({"subject": {"provider": "p", "kind": "vessel", "id": "v"}});
    serde_json::from_value(json!([first, node(B, "workspace", vec![terminal(&format!("{B}/view/u:1"), "s1", "$ top")])])).unwrap()
}

fn published(nodes: Vec<Node>) -> Published {
    Published { served: 1, generation: 3, tree: Some(Arc::new(nodes)) }
}

/// `query` as axum's `Query` reads it.
fn params(query: &str) -> Params {
    let uri: axum::http::Uri = format!("/?{query}").parse().unwrap();
    Query::<Params>::try_from_uri(&uri).unwrap().0
}

#[test]
fn root_returns_exactly_that_subtree() {
    let published = published(nodes());
    for wanted in nodes().iter().flat_map(Node::preorder) {
        let tree = tree(&published, &params(&format!("root={}&include=terminal_text", wanted.id))).unwrap();
        assert_eq!((tree.schema, tree.generation), (1, 3));
        assert_eq!(tree.nodes, std::slice::from_ref(wanted));
    }
    // Percent-encoded, as a client would send it.
    assert_eq!(tree(&published, &params("root=workspace%2F00000000-0000-0000-0000-00000000000b")).unwrap().nodes[0].id, B);
    assert_eq!(tree(&published, &params("root=workspace/none")), None);
    // Without a root, every workspace; while off, nothing.
    assert_eq!(tree(&published, &params("")).unwrap().nodes.len(), 2);
    assert_eq!(tree(&Published::default(), &params("")), None);
}

#[test]
fn terminal_text_only_when_asked() {
    let published = published(nodes());
    let text = |query| serde_json::to_string(&tree(&published, &params(query)).unwrap()).unwrap();
    assert!(!text("").contains("$ ls") && !text("").contains("cursor"));
    assert!(text("include=terminal_text").contains("$ ls") && text("include=other,terminal_text").contains("cursor"));
}

#[test]
fn caller_context_lists_the_views_showing_a_session() {
    let nodes = nodes();
    assert_eq!(
        serde_json::to_value(caller_views(&nodes, "s1")).unwrap(),
        json!([
            {"id": format!("{A}/view/u:1"), "workspace": A, "subject": {"provider": "p", "kind": "vessel", "id": "v"}},
            {"id": format!("{B}/view/u:1"), "workspace": B},
        ])
    );
    // No view shows it: an empty list, not an error.
    assert!(caller_views(&nodes, "other").is_empty());
}

#[test]
fn the_size_cap_drops_terminal_text_first() {
    let mut nodes = nodes();
    nodes[0].visit_mut(&mut |node| {
        if let Some(terminal) = &mut node.props.terminal {
            terminal.text = Some(vec!["x".repeat(4000)]);
        }
    });
    let tree = tree(&published(nodes), &params("include=terminal_text")).unwrap();
    let within = |limit: usize| move |tree: &Tree| encode(tree, false).filter(|body| body.len() <= limit);
    let whole: Value = serde_json::from_slice(&fit(Cow::Borrowed(&tree), within(MAX_RESPONSE)).unwrap()).unwrap();
    assert!(whole.to_string().contains("xxxx") && whole.get("truncated").is_none());
    // Over the cap: the text goes, the nodes stay.
    let capped: Value = serde_json::from_slice(&fit(Cow::Borrowed(&tree), within(3000)).unwrap()).unwrap();
    assert_eq!(capped["truncated"], true);
    assert!(!capped.to_string().contains("xxxx") && !capped.to_string().contains("cursor"));
    assert_eq!(capped["nodes"].as_array().unwrap().len(), 2);
    assert_eq!(capped["nodes"][0]["children"][0]["children"][0]["children"][0]["props"]["terminal"]["session"], "s1");
    // Still over: none.
    assert_eq!(fit(Cow::Owned(tree), within(100)), None);
}

#[hegel::test(test_cases = 200)]
fn the_generation_only_increases_and_moves_only_on_change(tc: TestCase) {
    let shapes = [nodes(), nodes()[..1].to_vec(), Vec::new()];
    let mut generation = Generation::default();
    let mut last: Option<Vec<Node>> = None;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(12)) {
        let mut next = shapes[tc.draw(gs::integers::<usize>().max_value(2))].clone();
        // Screen text changes alone don't count.
        if tc.draw(gs::booleans()) {
            drop_terminal_text(&mut next);
        }
        let before = generation.generation;
        let shown = next.clone();
        let moved = generation.observe(&mut next);
        assert_eq!(next, shown, "the screens are put back");
        let mut plain = next.clone();
        drop_terminal_text(&mut plain);
        let changed = last.as_ref() != Some(&plain);
        assert_eq!(moved, changed);
        assert_eq!(generation.generation, before + u64::from(changed));
        last = Some(plain);
    }
}

#[test]
fn queries_parse() {
    let parsed = params("include=a,terminal_text&root=w%2Fx&since=i:4&session=s%2B1");
    assert!(parsed.terminal_text());
    assert_eq!((parsed.root.as_deref(), parsed.since.as_deref(), parsed.session.as_deref()), (Some("w/x"), Some("i:4"), Some("s+1")));
    let accept = |value: &str| cbor_wanted(&HeaderMap::from_iter([(header::ACCEPT, value.parse().unwrap())]));
    assert!(accept("application/json, application/cbor;q=0.5"));
    assert!(!accept("application/json"));
}

/// An endpoint on a real socket, with a UI that ends a frame every 10ms
/// showing `tree`, under the `mode` setting; it gives screens only when
/// asked, and `screens` says whether the last build was.
#[cfg(unix)]
struct Endpoint {
    path: std::path::PathBuf,
    automation: Arc<Automation>,
    tree: Arc<std::sync::Mutex<Vec<Node>>>,
    mode: Arc<AtomicU32>,
    screens: Arc<std::sync::atomic::AtomicBool>,
}

#[cfg(unix)]
type Body = std::io::BufReader<std::os::unix::net::UnixStream>;

#[cfg(unix)]
impl Endpoint {
    fn start() -> Self {
        use std::os::unix::fs::DirBuilderExt;
        extern "C" fn wake() {}
        // Short, as macOS's temporary directory leaves no room for a socket.
        let dir = std::path::PathBuf::from(format!("/tmp/wh-{}", &crate::address::instance_uuid()[..8]));
        std::fs::DirBuilder::new().mode(0o700).create(&dir).unwrap();
        let path = dir.join("socket");
        let listener = std::os::unix::net::UnixListener::bind(&path).unwrap();
        listener.set_nonblocking(true).unwrap();
        let automation = Arc::new(Automation::new(wake));
        let routes = router(automation.clone());
        std::thread::spawn(move || {
            let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
            runtime.block_on(async move { axum::serve(tokio::net::UnixListener::from_std(listener).unwrap(), routes).await.unwrap() });
        });
        let tree = Arc::new(std::sync::Mutex::new(nodes()));
        let mode = Arc::new(AtomicU32::new(AUTOMATION_FULL));
        let screens = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let endpoint = Endpoint { path, automation: automation.clone(), tree: tree.clone(), mode: mode.clone(), screens: screens.clone() };
        std::thread::spawn(move || {
            let mut generation = Generation::default();
            loop {
                frame_end(&automation, &mut generation, mode.load(SeqCst), |asked| {
                    screens.store(asked, SeqCst);
                    let mut nodes = tree.lock().unwrap().clone();
                    if !asked {
                        drop_terminal_text(&mut nodes);
                    }
                    Some(nodes)
                });
                std::thread::sleep(Duration::from_millis(10));
            }
        });
        endpoint
    }

    /// A GET's status and head, and its body as it comes.
    fn get(&self, target: &str, headers: &str) -> (u16, String, Body) {
        self.request(&format!("GET {target} HTTP/1.0\r\n{headers}\r\n"))
    }

    /// HTTP/1.0 bodies run to the close, unchunked; HTTP/1.1 ones are chunked.
    fn request(&self, request: &str) -> (u16, String, Body) {
        use std::io::{BufRead, Write};
        let mut stream = std::os::unix::net::UnixStream::connect(&self.path).unwrap();
        stream.write_all(request.as_bytes()).unwrap();
        stream.set_read_timeout(Some(Duration::from_secs(10))).unwrap();
        let mut reader = std::io::BufReader::new(stream);
        let mut head = String::new();
        while !head.ends_with("\r\n\r\n") {
            assert_ne!(reader.read_line(&mut head).unwrap(), 0, "{head}");
        }
        (head[9..12].parse().unwrap(), head, reader)
    }

    fn read(&self, target: &str, headers: &str) -> (u16, String, Vec<u8>) {
        use std::io::Read;
        let (status, head, mut reader) = self.get(target, headers);
        let mut body = Vec::new();
        reader.read_to_end(&mut body).unwrap();
        (status, head, body)
    }

    fn rename(&self, name: &str) {
        self.tree.lock().unwrap()[1].name = Some(name.into());
    }
}

/// Whether the stream closes within `wait` (rather than staying open).
#[cfg(unix)]
fn closes(reader: &mut Body, wait: Duration) -> bool {
    use std::io::Read;
    // macOS refuses a timeout (EINVAL) once the peer has closed; the read
    // then ends at once.
    let _ = reader.get_ref().set_read_timeout(Some(wait));
    let mut rest = Vec::new();
    match reader.read_to_end(&mut rest) {
        Ok(_) => true,
        Err(e) if matches!(e.kind(), std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut) => false,
        Err(e) => panic!("{e}"),
    }
}

/// The next event line, or `None` when none comes within `wait`.
#[cfg(unix)]
fn next(reader: &mut Body, wait: Duration) -> Option<Record<'static>> {
    use std::io::BufRead;
    // macOS refuses a timeout (EINVAL) once the peer has closed; the read
    // then ends at once.
    let _ = reader.get_ref().set_read_timeout(Some(wait));
    let mut line = String::new();
    match reader.read_line(&mut line) {
        Ok(0) => None,
        Ok(_) => Some(serde_json::from_str(&line).unwrap()),
        Err(e) if matches!(e.kind(), std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut) => None,
        Err(e) => panic!("{e}"),
    }
}

#[cfg(unix)]
fn snapshot(record: Record) -> Tree {
    let Event::Snapshot { tree } = record.event;
    tree.into_owned()
}

// A snapshot first; another after a change, and none without one; a
// current cursor resumes without one, and a stale or foreign one gets one.
#[cfg(unix)]
#[test]
fn the_event_stream_sends_a_snapshot_first_and_after_each_change() {
    let endpoint = Endpoint::start();
    let wait = Duration::from_secs(5);
    let (status, head, mut events) = endpoint.get("/v1/observed/events", "");
    assert_eq!(status, 200);
    assert!(head.contains("content-type: application/x-ndjson"), "{head}");
    let first = next(&mut events, wait).unwrap();
    let tree = snapshot(first.clone());
    assert_eq!(first.cursor, format!("{}:{}", endpoint.automation.instance, tree.generation));
    assert_eq!(tree.nodes.len(), 2);
    assert!(!serde_json::to_string(&tree).unwrap().contains("$ ls"), "text only when asked");
    // Nothing changes: nothing comes.
    assert_eq!(next(&mut events, Duration::from_millis(300)), None);
    endpoint.rename("renamed");
    let second = next(&mut events, wait).unwrap();
    let changed = snapshot(second.clone());
    assert_eq!((changed.generation, changed.nodes[1].name.as_deref()), (tree.generation + 1, Some("renamed")));
    // Resuming at the current cursor: nothing until a change.
    let (_, _, mut resumed) = endpoint.get(&format!("/v1/observed/events?since={}", second.cursor), "");
    assert_eq!(next(&mut resumed, Duration::from_millis(300)), None);
    endpoint.rename("again");
    assert_eq!(snapshot(next(&mut resumed, wait).unwrap()).nodes[1].name.as_deref(), Some("again"));
    // A stale cursor, another instance's or nonsense: a snapshot at once.
    for since in [first.cursor.clone(), format!("other:{}", tree.generation + 2), "x".into()] {
        let (_, _, mut stream) = endpoint.get(&format!("/v1/observed/events?since={since}"), "");
        assert_eq!(snapshot(next(&mut stream, wait).unwrap()).nodes[1].name.as_deref(), Some("again"), "{since}");
    }
    // A root narrows each snapshot, and terminal text comes when asked.
    let (_, _, mut rooted) = endpoint.get(&format!("/v1/observed/events?root={B}&include=terminal_text"), "");
    let tree = snapshot(next(&mut rooted, wait).unwrap());
    assert_eq!((tree.nodes.len(), tree.nodes[0].id.as_str()), (1, B));
    assert!(serde_json::to_string(&tree).unwrap().contains("$ top"));
    // Its screens are read when the tree changes, not every frame.
    std::thread::sleep(Duration::from_millis(100));
    assert!(!endpoint.screens.load(SeqCst), "screens read every frame for a stream");
    // A change outside its root sends it nothing; one under it does.
    endpoint.tree.lock().unwrap()[0].name = Some("elsewhere".into());
    assert_eq!(next(&mut rooted, Duration::from_millis(300)), None);
    endpoint.rename("under the root");
    let tree = snapshot(next(&mut rooted, wait).unwrap());
    assert_eq!(tree.nodes[0].name.as_deref(), Some("under the root"));
    assert!(serde_json::to_string(&tree).unwrap().contains("$ top"), "no screens with a change");
    assert_eq!(endpoint.read("/v1/observed/events?root=workspace/none", "").0, 404);
    // Turning automation off ends the streams, and the routes answer 404.
    assert_eq!(snapshot(next(&mut events, wait).unwrap()).nodes[1].name.as_deref(), Some("again"));
    endpoint.mode.store(AUTOMATION_OFF, SeqCst);
    assert!(closes(&mut events, wait), "the stream stayed open");
    assert!(closes(&mut rooted, wait) && closes(&mut resumed, wait));
    let deadline = std::time::Instant::now() + wait;
    while endpoint.automation.streams.load(SeqCst) != 0 {
        assert!(std::time::Instant::now() < deadline, "streams still counted");
        std::thread::sleep(Duration::from_millis(10));
    }
    for route in ["/v1/observed/events", "/v1/observed/semantic", "/v1/observed/self?session=s1"] {
        assert_eq!(endpoint.read(route, "").0, 404, "{route}");
    }
}

// The same events in CBOR, each one of ADR 0063's frames.
#[cfg(unix)]
#[test]
fn the_event_stream_frames_cbor() {
    use std::io::Read;
    let endpoint = Endpoint::start();
    let (status, head, mut events) = endpoint.get("/v1/observed/events", "Accept: application/cbor\r\n");
    assert_eq!(status, 200);
    assert!(head.contains("content-type: application/cbor"), "{head}");
    let mut header = [0; flotilla_cbor::HEADER_LEN];
    events.read_exact(&mut header).unwrap();
    let mut frame = header.to_vec();
    frame.resize(flotilla_cbor::HEADER_LEN + FRAMING.body_len(&header).unwrap(), 0);
    events.read_exact(&mut frame[flotilla_cbor::HEADER_LEN..]).unwrap();
    let record: Record<'static> = FRAMING.decode(&frame).unwrap();
    assert_eq!(record.frame.as_deref(), Some("event"));
    assert_eq!(snapshot(record).nodes.len(), 2);
}

// One-shot reads: the tree as of the next frame, in JSON or CBOR; `self`
// needs a session.
#[cfg(unix)]
#[test]
fn reads_answer_from_the_next_frame() {
    let endpoint = Endpoint::start();
    let (status, _, body) = endpoint.read("/v1/observed/semantic", "");
    assert_eq!(status, 200);
    let json: Tree = serde_json::from_slice(&body).unwrap();
    let mut plain = nodes();
    drop_terminal_text(&mut plain);
    assert_eq!(json.nodes, plain);
    assert!(!endpoint.screens.load(SeqCst), "no screens unless asked");
    let (_, _, body) = endpoint.read("/v1/observed/semantic?include=terminal_text", "");
    assert!(String::from_utf8(body).unwrap().contains("$ ls"));
    let (_, head, body) = endpoint.read("/v1/observed/semantic", "Accept: application/cbor\r\n");
    assert!(head.contains("content-type: application/cbor"), "{head}");
    assert_eq!(flotilla_cbor::from_slice::<Tree>(&body, &LIMITS).unwrap(), json);
    // A change shows in the next read, a generation on.
    endpoint.rename("renamed");
    let renamed: Tree = serde_json::from_slice(&endpoint.read("/v1/observed/semantic", "").2).unwrap();
    assert_eq!((renamed.generation, renamed.nodes[1].name.as_deref()), (json.generation + 1, Some("renamed")));
    let (status, _, body) = endpoint.read("/v1/observed/self?session=s1", "");
    assert_eq!(status, 200);
    assert_eq!(serde_json::from_slice::<Value>(&body).unwrap()["views"].as_array().unwrap().len(), 2);
    assert_eq!(endpoint.read("/v1/observed/self", "").0, 400);
    // `self` gives no text, so asking builds no screens.
    assert_eq!(endpoint.read("/v1/observed/self?session=s1&include=terminal_text", "").0, 200);
    assert!(!endpoint.screens.load(SeqCst), "self built screens");
    assert_eq!(endpoint.read("/v1/observed/semantic?root=workspace/none", "").0, 404);
}

// A setting the ingress doesn't know is taken as off.
#[test]
fn an_unknown_mode_is_off() {
    extern "C" fn wake() {}
    let automation = Automation::new(wake);
    frame_end(&automation, &mut Generation::default(), AUTOMATION_FULL + 1, |_| Some(nodes()));
    assert_eq!(automation.mode.load(SeqCst), AUTOMATION_OFF);
}

// Past the cap, a stream is refused before it makes the UI build.
#[cfg(unix)]
#[test]
fn a_stream_past_the_cap_is_refused_without_a_build() {
    let endpoint = Endpoint::start();
    endpoint.automation.streams.store(MAX_STREAMS, SeqCst);
    let requested = endpoint.automation.requested.load(SeqCst);
    assert_eq!(endpoint.read("/v1/observed/events", "").0, 503);
    assert_eq!(endpoint.automation.requested.load(SeqCst), requested);
    assert_eq!(endpoint.automation.streams.load(SeqCst), MAX_STREAMS);
}

// Turning automation off ends a stream cleanly; a later snapshot over the
// cap breaks it off, without the final chunk, and a first one is a 500.
#[cfg(unix)]
#[test]
fn an_oversize_snapshot_breaks_the_stream_off() {
    use std::io::{BufRead, Read};
    let endpoint = Endpoint::start();
    let stream = |endpoint: &Endpoint| endpoint.request("GET /v1/observed/events HTTP/1.1\r\nConnection: close\r\n\r\n");
    let rest = |mut body: Body| {
        let mut rest = Vec::new();
        body.read_to_end(&mut rest).unwrap();
        rest
    };
    let (status, head, mut body) = stream(&endpoint);
    assert!(status == 200 && head.contains("transfer-encoding: chunked"), "{head}");
    body.read_line(&mut String::new()).unwrap();
    endpoint.mode.store(AUTOMATION_OFF, SeqCst);
    assert!(rest(body).ends_with(b"0\r\n\r\n"), "turning off ends it cleanly");
    endpoint.mode.store(AUTOMATION_FULL, SeqCst);
    while endpoint.automation.mode.load(SeqCst) != AUTOMATION_FULL {
        std::thread::sleep(Duration::from_millis(10));
    }
    let (status, _, mut body) = stream(&endpoint);
    assert_eq!(status, 200);
    body.read_line(&mut String::new()).unwrap();
    endpoint.rename(&"x".repeat(MAX_RESPONSE));
    assert!(!rest(body).ends_with(b"0\r\n\r\n"), "an oversize snapshot ended it cleanly");
    // Over the cap from the start: a 500, as for one read.
    assert_eq!(endpoint.read("/v1/observed/events", "").0, 500);
}

// A client that goes while its stream is idle frees its place.
#[cfg(unix)]
#[test]
fn a_stream_whose_client_goes_is_released_while_idle() {
    let endpoint = Endpoint::start();
    let (_, _, mut events) = endpoint.get("/v1/observed/events", "");
    next(&mut events, Duration::from_secs(5)).unwrap();
    assert_eq!(endpoint.automation.streams.load(SeqCst), 1);
    drop(events);
    let deadline = std::time::Instant::now() + Duration::from_secs(5);
    while endpoint.automation.streams.load(SeqCst) != 0 {
        assert!(std::time::Instant::now() < deadline, "a dead client's stream is still counted");
        std::thread::sleep(Duration::from_millis(10));
    }
}
