//! The Automation Endpoint's read routes (#554; docs/protocol/automation-endpoint.md):
//! `GET /v1/observed/semantic`, `/v1/observed/self` and `/v1/observed/events`.
//! The UI thread builds the Semantic Tree at a frame's end while a client
//! wants it, and publishes `(generation, tree)` on a watch channel; handlers
//! read and follow that.
use std::{
    sync::{
        atomic::{AtomicU32, AtomicU64, AtomicUsize, Ordering::SeqCst},
        Arc,
    },
    time::Duration,
};

use axum::{
    body::Body,
    extract::{Query, State},
    http::{header, HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    routing::get,
    Router,
};
use flotilla_cbor::{Framing, Limits};
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use tokio::sync::watch;
use wheelhouse_core::{
    automation::{Node, State as Flag, Terminal},
    EntityRef, WorkspaceId,
};

use crate::{Wake, AUTOMATION_FULL, AUTOMATION_OFF};

/// The schema every response carries.
pub const SCHEMA: u32 = 1;
/// The largest response or event; past it terminal text goes.
pub const MAX_RESPONSE: usize = 4 * 1024 * 1024;
/// CBOR within [`MAX_RESPONSE`]. Panels nest, so deeper than the profile's
/// default; elements are bounded by the bytes.
pub const LIMITS: Limits = Limits { max_message: MAX_RESPONSE, max_depth: 512, max_elements: MAX_RESPONSE, max_byte_string: MAX_RESPONSE };
/// The event stream's frames in CBOR (ADR 0063).
pub const FRAMING: Framing = Framing::new(*b"WHEVENTS", LIMITS);
/// How long a read waits for the UI to build the tree.
const FRAME_WAIT: Duration = Duration::from_secs(5);
/// No frame has said yet.
pub const MODE_UNKNOWN: u32 = u32::MAX;

/// What the read routes take.
#[derive(Debug, Default, Deserialize)]
pub struct Params {
    /// An Automation ID: only that node and what is under it.
    pub root: Option<String>,
    /// `terminal_text`: terminals' screens, where the setting allows.
    pub include: Option<String>,
    /// `self`'s Cleat session.
    pub session: Option<String>,
    /// The events' cursor to resume from.
    pub since: Option<String>,
}

impl Params {
    fn terminal_text(&self) -> bool {
        self.include.as_deref().is_some_and(|include| include.split(',').any(|item| item == "terminal_text"))
    }
}

/// The Semantic Tree, as `semantic` answers and a snapshot carries it.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Tree {
    pub schema: u32,
    pub generation: u64,
    pub nodes: Vec<Node>,
    /// Terminal text was dropped to keep within [`MAX_RESPONSE`].
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub truncated: bool,
}

/// One line of the event stream; in CBOR, one frame, with `type` "event".
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Record {
    #[serde(rename = "type", default, skip_serializing_if = "Option::is_none")]
    pub frame: Option<String>,
    pub cursor: String,
    pub event: Event,
}

impl flotilla_cbor::Message for Record {
    const TYPES: &'static [&'static str] = &["event"];
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Event {
    /// The whole tree: first, after every change, and for a cursor that
    /// can't be resumed.
    Snapshot { tree: Tree },
}

/// `self`: the views showing a Cleat session.
#[derive(Serialize)]
struct Caller {
    schema: u32,
    generation: u64,
    session: String,
    views: Vec<CallerView>,
}

#[derive(Serialize)]
struct CallerView {
    id: String,
    workspace: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    subject: Option<EntityRef>,
}

/// What the UI last published.
#[derive(Debug, Default)]
pub struct Published {
    /// The last read it built for ([`Automation::requested`]).
    pub served: u64,
    pub generation: u64,
    /// `None` while automation is off.
    pub tree: Option<Arc<Vec<Node>>>,
}

/// What the routes and the UI thread share.
pub struct Automation {
    /// This process's, so a cursor from another is never resumed.
    pub instance: String,
    wake: Wake,
    /// The `automation` setting, as the UI last said.
    pub mode: AtomicU32,
    /// Reads asking for a fresh build, numbered.
    pub requested: AtomicU64,
    /// Open event streams; while any is, every frame compares the tree.
    pub streams: AtomicUsize,
    pub published: watch::Sender<Arc<Published>>,
}

impl Automation {
    pub fn new(wake: Wake) -> Self {
        Automation {
            instance: crate::address::instance_uuid(),
            wake,
            mode: AtomicU32::new(MODE_UNKNOWN),
            requested: AtomicU64::new(0),
            streams: AtomicUsize::new(0),
            published: watch::channel(Arc::default()).0,
        }
    }

    fn cursor(&self, generation: u64) -> String {
        format!("{}:{generation}", self.instance)
    }

    /// The tree as of the UI's next frame: 404 while automation is off,
    /// 503 when no frame comes in time.
    async fn fresh(&self) -> Result<Arc<Published>, StatusCode> {
        if self.mode.load(SeqCst) == AUTOMATION_OFF {
            return Err(StatusCode::NOT_FOUND);
        }
        let mut published = self.published.subscribe();
        let ticket = self.requested.fetch_add(1, SeqCst) + 1;
        (self.wake)();
        let built = tokio::time::timeout(FRAME_WAIT, published.wait_for(|p| p.served >= ticket)).await;
        let built = built.ok().and_then(Result::ok).map(|p| p.clone()).ok_or(StatusCode::SERVICE_UNAVAILABLE)?;
        built.tree.is_some().then_some(built).ok_or(StatusCode::NOT_FOUND)
    }
}

pub fn router(automation: Arc<Automation>) -> Router {
    Router::new()
        .route("/v1/observed/semantic", get(semantic))
        .route("/v1/observed/self", get(caller))
        .route("/v1/observed/events", get(events))
        .with_state(automation)
}

fn cbor_wanted(headers: &HeaderMap) -> bool {
    let accept = headers.get(header::ACCEPT).and_then(|v| v.to_str().ok()).unwrap_or("");
    accept.split(',').any(|item| item.split(';').next().map(str::trim) == Some("application/cbor"))
}

fn content_type(cbor: bool) -> &'static str {
    if cbor {
        "application/cbor"
    } else {
        "application/json"
    }
}

fn reply(cbor: bool, body: Option<Vec<u8>>) -> Response {
    match body {
        Some(body) => ([(header::CACHE_CONTROL, "no-store"), (header::CONTENT_TYPE, content_type(cbor))], body).into_response(),
        None => (StatusCode::INTERNAL_SERVER_ERROR, "the tree exceeds 4 MiB").into_response(),
    }
}

async fn semantic(State(automation): State<Arc<Automation>>, Query(params): Query<Params>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    let published = match automation.fresh().await {
        Ok(published) => published,
        Err(status) => return status.into_response(),
    };
    match tree(&published, &params) {
        Some(tree) => reply(cbor, fit(tree, |tree| encode(tree, cbor))),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn caller(State(automation): State<Arc<Automation>>, Query(params): Query<Params>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    let Some(session) = params.session.clone().filter(|session| !session.is_empty()) else {
        return (StatusCode::BAD_REQUEST, "session is required").into_response();
    };
    let published = match automation.fresh().await {
        Ok(published) => published,
        Err(status) => return status.into_response(),
    };
    let Some(tree) = tree(&published, &params) else { return StatusCode::NOT_FOUND.into_response() };
    let views = caller_views(&tree.nodes, &session);
    reply(cbor, encode(&Caller { schema: SCHEMA, generation: tree.generation, session, views }, cbor))
}

/// Ends a stream's count when the client goes.
struct Stream(Arc<Automation>);

impl Drop for Stream {
    fn drop(&mut self) {
        self.0.streams.fetch_sub(1, SeqCst);
    }
}

/// A snapshot first, unless `since` is current, then one after each change.
/// The stream ends when automation turns off or `root` goes.
async fn events(State(automation): State<Arc<Automation>>, Query(params): Query<Params>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    automation.streams.fetch_add(1, SeqCst);
    let stream = Stream(automation.clone());
    let published = match automation.fresh().await {
        Ok(published) => published,
        Err(status) => return status.into_response(),
    };
    if tree(&published, &params).is_none() {
        return StatusCode::NOT_FOUND.into_response();
    }
    let sent = (params.since.as_deref() == Some(&automation.cursor(published.generation))).then_some(published.generation);
    let changes = automation.published.subscribe();
    let lines = futures_util::stream::unfold((stream, changes, sent, params), move |(stream, mut changes, mut sent, params)| async move {
        loop {
            let published = changes.borrow_and_update().clone();
            if sent != Some(published.generation) {
                sent = Some(published.generation);
                let tree = tree(&published, &params)?;
                let cursor = stream.0.cursor(tree.generation);
                let line = fit(tree, |tree| {
                    let record = Record { frame: None, cursor: cursor.clone(), event: Event::Snapshot { tree: tree.clone() } };
                    if cbor {
                        FRAMING.encode(&Record { frame: Some("event".into()), ..record }).ok()
                    } else {
                        encode(&record, false).map(|mut line| {
                            line.push(b'\n');
                            line
                        })
                    }
                })?;
                return Some((Ok::<_, std::convert::Infallible>(line), (stream, changes, sent, params)));
            }
            changes.changed().await.ok()?;
        }
    });
    let content_type = if cbor { "application/cbor" } else { "application/x-ndjson" };
    ([(header::CACHE_CONTROL, "no-store"), (header::CONTENT_TYPE, content_type)], Body::from_stream(lines)).into_response()
}

/// What `params` read of `published`: `None` while automation is off, or
/// when no node has the `root` asked for.
pub fn tree(published: &Published, params: &Params) -> Option<Tree> {
    let all = published.tree.as_ref()?;
    let mut nodes = match &params.root {
        Some(root) => vec![find(all, root)?.clone()],
        None => all.to_vec(),
    };
    if !params.terminal_text() {
        drop_terminal_text(&mut nodes);
    }
    Some(Tree { schema: SCHEMA, generation: published.generation, nodes, truncated: false })
}

/// The node with Automation ID `id`, at any depth.
pub fn find<'a>(nodes: &'a [Node], id: &str) -> Option<&'a Node> {
    nodes.iter().flat_map(Node::preorder).find(|node| node.id == id)
}

/// Remove every terminal's screen; whether there was any.
pub fn drop_terminal_text(nodes: &mut [Node]) -> bool {
    let mut dropped = false;
    for node in nodes {
        node.visit_mut(&mut |node| {
            if let Some(terminal) = &mut node.props.terminal {
                dropped |= terminal.text.take().is_some() | terminal.cursor.take().is_some();
            }
        });
    }
    dropped
}

/// `tree` encoded, without terminal text and marked truncated if it
/// doesn't fit otherwise; `None` if it still doesn't.
pub fn fit(mut tree: Tree, encode: impl Fn(&Tree) -> Option<Vec<u8>>) -> Option<Vec<u8>> {
    encode(&tree).or_else(|| {
        drop_terminal_text(&mut tree.nodes);
        tree.truncated = true;
        encode(&tree)
    })
}

/// `value` as JSON or CBOR, or `None` past [`MAX_RESPONSE`].
pub fn encode<T: Serialize>(value: &T, cbor: bool) -> Option<Vec<u8>> {
    if cbor {
        return flotilla_cbor::to_vec(value, &LIMITS).ok();
    }
    let body = serde_json::to_vec(&json_safe(serde_json::to_value(value).ok()?)).ok()?;
    (body.len() <= MAX_RESPONSE).then_some(body)
}

/// The views under `nodes` that show `session`, with their workspace and
/// its Workspace Subject.
fn caller_views(nodes: &[Node], session: &str) -> Vec<CallerView> {
    let mut views = Vec::new();
    for workspace in nodes {
        for node in workspace.preorder() {
            if node.props.terminal.as_ref().is_some_and(|terminal| terminal.session.as_deref() == Some(session)) {
                views.push(CallerView { id: node.id.clone(), workspace: workspace.id.clone(), subject: workspace.props.subject.clone() });
            }
        }
    }
    views
}

/// The largest integer JSON readers keep exactly.
const SAFE: u64 = 1 << 53;

/// `value` with each integer beyond ±2^53 as `{"u64": "<decimal>"}`, which
/// JSON readers can't otherwise keep exactly.
pub fn json_safe(value: Value) -> Value {
    match value {
        Value::Number(n) if n.as_u64().is_some_and(|n| n > SAFE) || n.as_i64().is_some_and(|n| n.unsigned_abs() > SAFE) => {
            json!({"u64": n.to_string()})
        }
        Value::Array(items) => items.into_iter().map(json_safe).collect(),
        Value::Object(map) => map.into_iter().map(|(k, v)| (k, json_safe(v))).collect::<Map<_, _>>().into(),
        other => other,
    }
}

/// The tree as the UI gives it at a frame's end: each core's workspaces,
/// which workspaces windows show, and what it knows of terminals.
#[derive(Default)]
pub struct Build {
    pub workspaces: Vec<Node>,
    pub selected: Vec<WorkspaceId>,
    /// By the View's Automation ID.
    pub terminals: Vec<(String, Terminal)>,
    pub valid: bool,
}

impl Build {
    /// The Semantic Tree, with what the UI knows merged into its nodes.
    pub fn assemble(self) -> Vec<Node> {
        let mut nodes = self.workspaces;
        for workspace in &mut nodes {
            if self.selected.iter().any(|id| workspace.id == format!("workspace/{id}")) {
                workspace.state.push(Flag::Selected);
            }
            workspace.visit_mut(&mut |node| {
                for (_, known) in self.terminals.iter().filter(|(id, _)| *id == node.id) {
                    let terminal = node.props.terminal.get_or_insert_with(Terminal::default);
                    terminal.session = known.session.clone().or(terminal.session.take());
                    terminal.cols = known.cols;
                    terminal.rows = known.rows;
                    terminal.text = known.text.clone();
                    terminal.cursor = known.cursor;
                }
            });
        }
        nodes
    }
}

/// Change detection: the generation moves when the tree, terminal text
/// aside, differs from the last one built.
#[derive(Default)]
pub struct Generation {
    pub generation: u64,
    last: Option<Vec<u8>>,
}

impl Generation {
    /// Compare `nodes` with the last tree; whether the generation moved.
    pub fn observe(&mut self, nodes: &[Node]) -> bool {
        let mut nodes = nodes.to_vec();
        drop_terminal_text(&mut nodes);
        let bytes = serde_json::to_vec(&nodes).expect("nodes serialise");
        let changed = self.last.as_ref() != Some(&bytes);
        if changed {
            self.generation += 1;
            self.last = Some(bytes);
        }
        changed
    }
}

/// The UI's side, at each frame's end: note the setting, and while a read
/// waits or a stream is open, build the tree with `build`, compare it, and
/// publish it if a read waits or it changed. `build` gives `None` when the
/// tree isn't available now; reads then time out.
pub fn frame_end(automation: &Automation, generation: &mut Generation, mode: u32, build: impl FnOnce() -> Option<Vec<Node>>) {
    let mode = mode.min(AUTOMATION_FULL);
    automation.mode.store(mode, SeqCst);
    let requested = automation.requested.load(SeqCst);
    let current = automation.published.borrow().clone();
    let waiting = requested > current.served;
    if !waiting && automation.streams.load(SeqCst) == 0 {
        return;
    }
    let tree = match mode {
        AUTOMATION_OFF => None,
        _ => match build() {
            Some(tree) => Some(tree),
            None => return,
        },
    };
    let changed = match &tree {
        Some(tree) => generation.observe(tree),
        // Turning off ends the streams.
        None => current.tree.is_some(),
    };
    if waiting || changed {
        let served = requested.max(current.served);
        automation.published.send_replace(Arc::new(Published { served, generation: generation.generation, tree: tree.map(Arc::new) }));
    }
}

#[cfg(test)]
mod tests;
