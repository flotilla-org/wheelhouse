//! The Automation Endpoint's read routes (#554; docs/protocol/automation-endpoint.md):
//! `GET /v1/observed/semantic`, `/v1/observed/self` and `/v1/observed/events`.
//! The UI thread builds the Semantic Tree at a frame's end while a client
//! wants it, and publishes `(generation, tree)` on a watch channel; handlers
//! read and follow that.
use std::{
    borrow::Cow,
    collections::HashMap,
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
use futures_util::StreamExt;
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
/// The most event streams open at once; more get 503.
pub const MAX_STREAMS: usize = 64;

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
/// It borrows the tree it sends.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Record<'a> {
    #[serde(rename = "type", default, skip_serializing_if = "Option::is_none")]
    pub frame: Option<String>,
    pub cursor: String,
    pub event: Event<'a>,
}

impl flotilla_cbor::Message for Record<'_> {
    const TYPES: &'static [&'static str] = &["event"];
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Event<'a> {
    /// The whole tree: first, after every change, and for a cursor that
    /// can't be resumed.
    Snapshot { tree: Cow<'a, Tree> },
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
    /// Reads waiting that asked for terminal text; while none has, the UI
    /// gives no screens.
    pub screens: AtomicUsize,
    /// Streams that asked for terminal text: the UI gives screens only when
    /// the tree has changed.
    pub screen_streams: AtomicUsize,
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
            screens: AtomicUsize::new(0),
            screen_streams: AtomicUsize::new(0),
            published: watch::channel(Arc::default()).0,
        }
    }

    fn cursor(&self, generation: u64) -> String {
        format!("{}:{generation}", self.instance)
    }

    /// The tree as of the UI's next frame, with `screens` if asked: 404
    /// while automation is off, 503 when no frame comes in time.
    async fn fresh(self: &Arc<Self>, screens: bool) -> Result<Arc<Published>, StatusCode> {
        if self.mode.load(SeqCst) == AUTOMATION_OFF {
            return Err(StatusCode::NOT_FOUND);
        }
        // Counted before the ticket, so the frame that serves it sees it.
        let _screens = screens.then(|| Held::new(self, |a| &a.screens).0);
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
        None => (StatusCode::INTERNAL_SERVER_ERROR, "the response exceeds 4 MiB").into_response(),
    }
}

async fn semantic(State(automation): State<Arc<Automation>>, Query(params): Query<Params>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    let published = match automation.fresh(params.terminal_text()).await {
        Ok(published) => published,
        Err(status) => return status.into_response(),
    };
    match tree(&published, &params) {
        Some(tree) => reply(cbor, fit(Cow::Owned(tree), |tree| encode(tree, cbor))),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

async fn caller(State(automation): State<Arc<Automation>>, Query(params): Query<Params>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    let Some(session) = params.session.clone().filter(|session| !session.is_empty()) else {
        return (StatusCode::BAD_REQUEST, "session is required").into_response();
    };
    // `self` gives no screens.
    let published = match automation.fresh(false).await {
        Ok(published) => published,
        Err(status) => return status.into_response(),
    };
    let all = published.tree.as_deref().map_or(&[][..], Vec::as_slice);
    let nodes = match &params.root {
        Some(root) => match find(all, root) {
            Some(node) => std::slice::from_ref(node),
            None => return StatusCode::NOT_FOUND.into_response(),
        },
        None => all,
    };
    let views = caller_views(nodes, &session);
    reply(cbor, encode(&Caller { schema: SCHEMA, generation: published.generation, session, views }, cbor))
}

/// One of an [`Automation`]'s counts, held up while this lives.
struct Held(Arc<Automation>, fn(&Automation) -> &AtomicUsize);

impl Held {
    /// Held, and the count before.
    fn new(automation: &Arc<Automation>, count: fn(&Automation) -> &AtomicUsize) -> (Held, usize) {
        (Held(automation.clone(), count), count(automation).fetch_add(1, SeqCst))
    }
}

impl Drop for Held {
    fn drop(&mut self) {
        (self.1)(&self.0).fetch_sub(1, SeqCst);
    }
}

/// A snapshot first, unless `since` is current, then one after each change
/// (with a `root`, each change under it). The stream ends when automation
/// turns off or `root` goes. A first snapshot over [`MAX_RESPONSE`] is a
/// 500; a later one breaks the stream off unfinished.
async fn events(State(automation): State<Arc<Automation>>, Query(params): Query<Params>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    // The cap first, so a refused client never makes the UI build.
    let (stream, open) = Held::new(&automation, |a| &a.streams);
    if open >= MAX_STREAMS {
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    let screens = params.terminal_text().then(|| Held::new(&automation, |a| &a.screen_streams).0);
    let published = match automation.fresh(params.terminal_text()).await {
        Ok(published) => published,
        Err(status) => return status.into_response(),
    };
    let Some(first) = tree(&published, &params) else {
        return StatusCode::NOT_FOUND.into_response();
    };
    let last = subtree(&params, &first);
    let first = match params.since.as_deref() == Some(&automation.cursor(published.generation)) {
        true => None,
        false => match event(&automation, &first, cbor) {
            None => return reply(cbor, None),
            line => line,
        },
    };
    let state = (stream, screens, automation.published.subscribe(), published.generation, last, params);
    let rest = futures_util::stream::unfold(state, move |(stream, screens, mut changes, mut sent, mut last, params)| async move {
        loop {
            let published = changes.borrow_and_update().clone();
            // Automation turned off.
            published.tree.as_ref()?;
            if sent != published.generation {
                sent = published.generation;
                let tree = tree(&published, &params)?;
                let seen = subtree(&params, &tree);
                if seen.is_some() && seen == last {
                    continue;
                }
                last = seen;
                // Too large: break off, unlike the clean end of turning off.
                let line = event(&stream.0, &tree, cbor).ok_or_else(|| std::io::Error::other("the snapshot exceeds 4 MiB"));
                return Some((line, (stream, screens, changes, sent, last, params)));
            }
            changes.changed().await.ok()?;
        }
    });
    let lines = futures_util::stream::iter(first.map(Ok)).chain(rest);
    let content_type = if cbor { "application/cbor" } else { "application/x-ndjson" };
    ([(header::CACHE_CONTROL, "no-store"), (header::CONTENT_TYPE, content_type)], Body::from_stream(lines)).into_response()
}

/// With a `root`, the nodes a stream sends, to skip changes elsewhere.
fn subtree(params: &Params, tree: &Tree) -> Option<Vec<u8>> {
    params.root.as_ref().and_then(|_| serde_json::to_vec(&tree.nodes).ok())
}

/// `tree` as one event, a line of JSON or a CBOR frame; `None` past
/// [`MAX_RESPONSE`].
fn event(automation: &Automation, tree: &Tree, cbor: bool) -> Option<Vec<u8>> {
    let cursor = automation.cursor(tree.generation);
    fit(Cow::Borrowed(tree), |tree| {
        let record = Record { frame: None, cursor: cursor.clone(), event: Event::Snapshot { tree: Cow::Borrowed(tree) } };
        if cbor {
            FRAMING.encode(&Record { frame: Some("event".into()), ..record }).ok()
        } else {
            encode(&record, false).map(|mut line| {
                line.push(b'\n');
                line
            })
        }
    })
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
pub fn fit(mut tree: Cow<Tree>, encode: impl Fn(&Tree) -> Option<Vec<u8>>) -> Option<Vec<u8>> {
    encode(&tree).or_else(|| {
        let tree = tree.to_mut();
        drop_terminal_text(&mut tree.nodes);
        tree.truncated = true;
        encode(tree)
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
        let terminals: HashMap<_, _> = self.terminals.into_iter().collect();
        let mut nodes = self.workspaces;
        for workspace in &mut nodes {
            if self.selected.iter().any(|id| workspace.id == format!("workspace/{id}")) {
                workspace.state.push(Flag::Selected);
            }
            workspace.visit_mut(&mut |node| {
                if let Some(known) = terminals.get(&node.id) {
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
    /// The screens are set aside while it is serialised, never copied.
    pub fn observe(&mut self, nodes: &mut [Node]) -> bool {
        let mut screens = Vec::new();
        for node in nodes.iter_mut() {
            node.visit_mut(&mut |node| {
                if let Some(terminal) = &mut node.props.terminal {
                    screens.push((terminal.text.take(), terminal.cursor.take()));
                }
            });
        }
        // Called from C, so no panics: a tree that won't serialise counts as
        // changed.
        let bytes = serde_json::to_vec(&*nodes).ok();
        let mut screens = screens.into_iter();
        for node in nodes.iter_mut() {
            node.visit_mut(&mut |node| {
                if let Some(terminal) = &mut node.props.terminal {
                    (terminal.text, terminal.cursor) = screens.next().unwrap_or_default();
                }
            });
        }
        let changed = bytes.is_none() || self.last != bytes;
        if changed {
            self.generation += 1;
            self.last = bytes;
        }
        changed
    }
}

/// The UI's side, at each frame's end: note the setting, and while a read
/// waits or a stream is open, build the tree with `build`, compare it, and
/// publish it if a read waits or it changed. `build` is told whether to give
/// screens: when a waiting read asked, or when the tree changed and a stream
/// asked. It gives `None` when the tree isn't available now; reads then time
/// out. A setting it doesn't know is taken as off.
pub fn frame_end(automation: &Automation, generation: &mut Generation, mode: u32, mut build: impl FnMut(bool) -> Option<Vec<Node>>) {
    let mode = if mode > AUTOMATION_FULL { AUTOMATION_OFF } else { mode };
    automation.mode.store(mode, SeqCst);
    let requested = automation.requested.load(SeqCst);
    let current = automation.published.borrow().clone();
    let waiting = requested > current.served;
    if !waiting && automation.streams.load(SeqCst) == 0 {
        return;
    }
    let screens = automation.screens.load(SeqCst) > 0;
    let mut tree = match mode {
        AUTOMATION_OFF => None,
        _ => match build(screens) {
            Some(tree) => Some(tree),
            None => return,
        },
    };
    let changed = match &mut tree {
        Some(tree) => generation.observe(tree),
        // Turning off ends the streams.
        None => current.tree.is_some(),
    };
    // Streams that asked get screens with each change, not every frame.
    if changed && tree.is_some() && !screens && automation.screen_streams.load(SeqCst) > 0 {
        tree = build(true).or(tree);
    }
    if waiting || changed {
        let served = requested.max(current.served);
        automation.published.send_replace(Arc::new(Published { served, generation: generation.generation, tree: tree.map(Arc::new) }));
    }
}

#[cfg(test)]
mod tests;
