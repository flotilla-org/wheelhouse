//! The Automation Endpoint's read routes (#554; docs/protocol/automation-endpoint.md):
//! `GET /v1/observed/semantic` and `GET /v1/observed/self`. The host builds
//! the Semantic Tree on the UI thread at the end of a frame; only encoded
//! bytes cross to the transport thread.
use serde_json::{json, Map, Value};
use std::time::{Duration, Instant};

/// The schema both routes carry.
pub const SCHEMA: u64 = 1;
/// The largest response; past it terminal text goes first.
pub const MAX_RESPONSE: usize = 4 * 1024 * 1024;
/// How long after a read the tree is still compared at each frame's end.
pub const RECENT: Duration = Duration::from_secs(10);
/// The longest `wait` a request may ask for.
pub const MAX_WAIT: Duration = Duration::from_secs(60);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Route {
    Semantic,
    /// The views showing a Cleat session.
    Caller,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Encoding {
    Json,
    Cbor,
}

/// A parsed read request.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Request {
    pub route: Route,
    pub root: Option<String>,
    pub since: Option<u64>,
    pub wait: Duration,
    pub terminal_text: bool,
    pub session: Option<String>,
    pub encoding: Encoding,
}

impl Request {
    /// The request a query string and `Accept` header make, or why not.
    pub fn parse(route: Route, query: Option<&str>, accept: Option<&str>) -> Result<Self, String> {
        let mut request =
            Request { route, root: None, since: None, wait: Duration::ZERO, terminal_text: false, session: None, encoding: Encoding::Json };
        for pair in query.unwrap_or("").split('&').filter(|pair| !pair.is_empty()) {
            let (name, value) = pair.split_once('=').unwrap_or((pair, ""));
            let value = decode(value).ok_or_else(|| format!("{name} is not valid percent-encoded UTF-8"))?;
            match name {
                "root" => request.root = Some(value),
                "since" => request.since = Some(value.parse().map_err(|_| "since must be a generation".to_owned())?),
                "wait" => {
                    let ms: u64 = value.parse().map_err(|_| "wait must be milliseconds".to_owned())?;
                    request.wait = Duration::from_millis(ms).min(MAX_WAIT);
                }
                "include" => request.terminal_text = value.split(',').any(|item| item == "terminal_text"),
                "session" => request.session = Some(value),
                _ => {}
            }
        }
        if route == Route::Caller && request.session.as_deref().is_none_or(str::is_empty) {
            return Err("session is required".into());
        }
        if accept.is_some_and(|accept| accept.split(',').any(|item| item.split(';').next().map(str::trim) == Some("application/cbor"))) {
            request.encoding = Encoding::Cbor;
        }
        Ok(request)
    }
}

/// `%XX` and `+` decoded.
fn decode(text: &str) -> Option<String> {
    let mut bytes = Vec::with_capacity(text.len());
    let mut iter = text.bytes();
    while let Some(byte) = iter.next() {
        bytes.push(match byte {
            b'+' => b' ',
            b'%' => {
                let hex = [iter.next()?, iter.next()?];
                u8::from_str_radix(std::str::from_utf8(&hex).ok()?, 16).ok()?
            }
            byte => byte,
        });
    }
    String::from_utf8(bytes).ok()
}

/// A response: a status, and for 200 a content type and body.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Reply {
    pub status: u16,
    pub content_type: &'static str,
    pub body: Vec<u8>,
}

impl Reply {
    pub fn status(status: u16) -> Self {
        Reply { status, content_type: "text/plain", body: Vec::new() }
    }
}

/// Merge what the frontend knows of a node into it: `props` objects one
/// level deep, `state` flags added.
fn annotate(node: &mut Map<String, Value>, extra: &Map<String, Value>) {
    if let Some(Value::Object(props)) = extra.get("props") {
        let mine = node.entry("props").or_insert_with(|| Value::Object(Map::new()));
        if let Value::Object(mine) = mine {
            for (key, value) in props {
                match (mine.get_mut(key), value) {
                    (Some(Value::Object(inner)), Value::Object(more)) => inner.extend(more.clone()),
                    _ => {
                        mine.insert(key.clone(), value.clone());
                    }
                }
            }
        }
    }
    if let Some(Value::Array(flags)) = extra.get("state") {
        let state = node.entry("state").or_insert_with(|| Value::Array(Vec::new()));
        if let Value::Array(state) = state {
            for flag in flags {
                if !state.contains(flag) {
                    state.push(flag.clone());
                }
            }
        }
    }
}

/// The Semantic Tree: the workspaces each core gave, with the frontend's
/// annotations merged into the nodes they name.
pub fn assemble(workspaces: Vec<Value>, annotations: &[(String, Map<String, Value>)]) -> Vec<Value> {
    fn walk(node: &mut Value, annotations: &[(String, Map<String, Value>)]) {
        let Value::Object(map) = node else { return };
        let id = map.get("id").and_then(Value::as_str).unwrap_or("").to_owned();
        for (_, extra) in annotations.iter().filter(|(target, _)| *target == id) {
            annotate(map, extra);
        }
        if let Some(Value::Array(children)) = map.get_mut("children") {
            for child in children {
                walk(child, annotations);
            }
        }
    }
    let mut workspaces = workspaces;
    for workspace in &mut workspaces {
        walk(workspace, annotations);
    }
    workspaces
}

/// The node with Automation ID `id` in `nodes`, at any depth.
pub fn find<'a>(nodes: &'a [Value], id: &str) -> Option<&'a Value> {
    nodes.iter().find_map(|node| {
        if node.get("id").and_then(Value::as_str) == Some(id) {
            return Some(node);
        }
        find(node.get("children").and_then(Value::as_array).map_or(&[][..], Vec::as_slice), id)
    })
}

/// Remove every terminal's screen text; returns whether there was any.
pub fn drop_terminal_text(node: &mut Value) -> bool {
    let mut dropped = false;
    if let Some(terminal) = node.pointer_mut("/props/terminal").and_then(Value::as_object_mut) {
        dropped |= terminal.remove("text").is_some();
        dropped |= terminal.remove("cursor").is_some();
    }
    if let Some(Value::Array(children)) = node.get_mut("children") {
        for child in children {
            dropped |= drop_terminal_text(child);
        }
    }
    dropped
}

/// The views under `nodes` that show `session`, with their workspace and
/// its Workspace Subject.
pub fn caller_views(nodes: &[Value], session: &str) -> Vec<Value> {
    fn walk(node: &Value, workspace: Option<&Value>, session: &str, out: &mut Vec<Value>) {
        let workspace = if node.get("kind").and_then(Value::as_str) == Some("workspace") { Some(node) } else { workspace };
        if node.get("kind").and_then(Value::as_str) == Some("view")
            && node.pointer("/props/terminal/session").and_then(Value::as_str) == Some(session)
        {
            let mut view = json!({"id": node["id"]});
            if let Some(workspace) = workspace {
                view["workspace"] = workspace["id"].clone();
                if let Some(subject) = workspace.pointer("/props/subject") {
                    view["subject"] = subject.clone();
                }
            }
            out.push(view);
        }
        for child in node.get("children").and_then(Value::as_array).into_iter().flatten() {
            walk(child, workspace, session, out);
        }
    }
    let mut out = Vec::new();
    for node in nodes {
        walk(node, None, session, &mut out);
    }
    out
}

/// What `request` reads at `generation`: 404 for a root that isn't there.
pub fn respond(request: &Request, tree: &[Value], generation: u64, limit: usize) -> Reply {
    let mut nodes = match &request.root {
        Some(root) => match find(tree, root) {
            Some(node) => vec![node.clone()],
            None => return Reply::status(404),
        },
        None => tree.to_vec(),
    };
    if !request.terminal_text {
        for node in &mut nodes {
            drop_terminal_text(node);
        }
    }
    let wrap = |nodes: &[Value], truncated: bool| {
        let mut value = json!({"schema": SCHEMA, "generation": generation});
        match request.route {
            Route::Semantic => value["nodes"] = nodes.to_vec().into(),
            Route::Caller => {
                let session = request.session.clone().unwrap_or_default();
                value["views"] = caller_views(nodes, &session).into();
                value["session"] = session.into();
            }
        }
        if truncated {
            value["truncated"] = true.into();
        }
        value
    };
    let encode = |value: &Value| match request.encoding {
        Encoding::Json => serde_json::to_vec(&json_safe(value)).expect("JSON values serialise"),
        Encoding::Cbor => cbor(value),
    };
    let mut body = encode(&wrap(&nodes, false));
    // Terminal text goes first, then whole workspaces from the end.
    if body.len() > limit && nodes.iter_mut().fold(false, |dropped, node| drop_terminal_text(node) | dropped) {
        body = encode(&wrap(&nodes, true));
    }
    while body.len() > limit && !nodes.is_empty() {
        nodes.pop();
        body = encode(&wrap(&nodes, true));
    }
    let content_type = match request.encoding {
        Encoding::Json => "application/json",
        Encoding::Cbor => "application/cbor",
    };
    Reply { status: 200, content_type, body }
}

/// The largest integer JSON readers keep exactly.
const SAFE: u64 = 1 << 53;

/// `value` with each integer beyond ±2^53 as `{"u64": "<decimal>"}`, which
/// JSON readers can't otherwise keep exactly.
pub fn json_safe(value: &Value) -> Value {
    match value {
        Value::Number(n) if n.as_u64().is_some_and(|n| n > SAFE) || n.as_i64().is_some_and(|n| n.unsigned_abs() > SAFE) => {
            json!({"u64": n.to_string()})
        }
        Value::Array(items) => items.iter().map(json_safe).collect(),
        Value::Object(map) => map.iter().map(|(k, v)| (k.clone(), json_safe(v))).collect::<Map<_, _>>().into(),
        other => other.clone(),
    }
}

/// `value` as deterministic CBOR in flotilla ADR 0063's profile (RFC 8949
/// section 4.2.1): the shortest heads, floats in the shortest width that
/// keeps them, map keys text in the bytewise order of their encodings,
/// definite lengths, and no tags.
pub fn cbor(value: &Value) -> Vec<u8> {
    let mut out = Vec::new();
    encode(value, &mut out);
    out
}

fn head(major: u8, n: u64, out: &mut Vec<u8>) {
    let major = major << 5;
    match n {
        0..=23 => out.push(major | n as u8),
        24..=0xff => out.extend([major | 24, n as u8]),
        0x100..=0xffff => {
            out.push(major | 25);
            out.extend((n as u16).to_be_bytes());
        }
        0x1_0000..=0xffff_ffff => {
            out.push(major | 26);
            out.extend((n as u32).to_be_bytes());
        }
        _ => {
            out.push(major | 27);
            out.extend(n.to_be_bytes());
        }
    }
}

fn encode(value: &Value, out: &mut Vec<u8>) {
    match value {
        Value::Null => out.push(0xf6),
        Value::Bool(false) => out.push(0xf4),
        Value::Bool(true) => out.push(0xf5),
        Value::Number(n) => {
            if let Some(n) = n.as_u64() {
                head(0, n, out);
            } else if let Some(n) = n.as_i64() {
                head(1, (-1 - n) as u64, out);
            } else {
                float(n.as_f64().unwrap_or(f64::NAN), out);
            }
        }
        Value::String(s) => {
            head(3, s.len() as u64, out);
            out.extend(s.as_bytes());
        }
        Value::Array(items) => {
            head(4, items.len() as u64, out);
            for item in items {
                encode(item, out);
            }
        }
        Value::Object(map) => {
            let mut entries: Vec<(Vec<u8>, &Value)> = map
                .iter()
                .map(|(k, v)| {
                    let mut key = Vec::new();
                    encode(&Value::String(k.clone()), &mut key);
                    (key, v)
                })
                .collect();
            entries.sort_by(|a, b| a.0.cmp(&b.0));
            head(5, entries.len() as u64, out);
            for (key, value) in entries {
                out.extend(key);
                encode(value, out);
            }
        }
    }
}

/// `x` in the shortest of half, single and double width that holds it
/// exactly.
fn float(x: f64, out: &mut Vec<u8>) {
    if let Some(half) = half(x) {
        out.push(0xf9);
        out.extend(half.to_be_bytes());
    } else if f64::from(x as f32) == x {
        out.push(0xfa);
        out.extend((x as f32).to_bits().to_be_bytes());
    } else {
        out.push(0xfb);
        out.extend(x.to_bits().to_be_bytes());
    }
}

/// `x` as an IEEE half, when one holds it exactly.
fn half(x: f64) -> Option<u16> {
    if x.is_nan() {
        return Some(0x7e00);
    }
    let single = x as f32;
    if f64::from(single) != x {
        return None;
    }
    let bits = single.to_bits();
    let sign = ((bits >> 16) & 0x8000) as u16;
    let exponent = ((bits >> 23) & 0xff) as i32;
    let mantissa = bits & 0x7f_ffff;
    if exponent == 0xff {
        return (mantissa == 0).then_some(sign | 0x7c00);
    }
    if exponent == 0 && mantissa == 0 {
        return Some(sign);
    }
    let e = exponent - 127;
    if (-14..=15).contains(&e) {
        // Normal: the mantissa's low 13 bits must be zero.
        (mantissa & 0x1fff == 0).then(|| sign | (((e + 15) as u16) << 10) | (mantissa >> 13) as u16)
    } else if (-24..-14).contains(&e) {
        // Subnormal: value = m * 2^-24.
        let full = mantissa | 0x80_0000;
        let shift = (-14 - e) as u32 + 13;
        (full & ((1 << shift) - 1) == 0).then(|| sign | (full >> shift) as u16)
    } else {
        None
    }
}

/// The change detection both routes share. `generation` moves when the
/// serialised tree changes, compared at frame end while a client waits or
/// has read within [`RECENT`]; otherwise nothing is built.
#[derive(Default)]
pub struct Generation {
    pub generation: u64,
    last: Option<Vec<u8>>,
    read: Option<Instant>,
}

impl Generation {
    /// Whether frame end should build and compare the tree.
    pub fn active(&self, waiting: bool, now: Instant) -> bool {
        waiting || self.read.is_some_and(|read| now.duration_since(read) < RECENT)
    }

    /// Compare `tree` with the last one seen, moving the generation if it
    /// changed. Terminal screen text doesn't count.
    pub fn observe(&mut self, tree: &[Value]) -> u64 {
        let mut tree = tree.to_vec();
        for node in &mut tree {
            drop_terminal_text(node);
        }
        let bytes = cbor(&Value::Array(tree));
        if self.last.as_ref() != Some(&bytes) {
            self.generation += 1;
            self.last = Some(bytes);
        }
        self.generation
    }

    pub fn read(&mut self, now: Instant) {
        self.read = Some(now);
    }
}

#[cfg(test)]
mod tests;
