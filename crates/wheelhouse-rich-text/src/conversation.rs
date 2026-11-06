//! Conversations as Rich Text: contract events (#548) become Turns and
//! Sections, streamed as #540 decides, and a large file backfills newest
//! first.
//!
//! An item (a message, tool call, reasoning, subagent, Divider or notice)
//! sits in a slot (the root, a subagent's Section, or the tool call that
//! started a subagent) in the order its first line came in the file. That
//! order, and every item's content, depend only on the lines, so ingesting
//! the file in any pieces, tail first or not, gives the same tree.
use std::collections::hash_map::Entry as MapEntry;
use std::collections::{HashMap, HashSet};
use std::ops::Range;
use std::time::{Duration, Instant};

use conversation_events::{
    parse_line, Event, Lines, Message, Outcome, Parent, Role as EventRole, ToolCallResult, ToolKind as EventToolKind,
    ToolStatus as EventToolStatus, Truncated, ANSI, COMPACTION, CONTRACT, DIFF, MARKDOWN,
};
use serde_json::Value;

use super::ingest::{parse_into, Staged};
use super::log::Log;
use super::ring::Ring;
use super::tail::{repair, Fence};
use super::tree::{Attr, Detail, Key, Kind, Role, RunSummary, SectionKind, Span, ToolKind, ToolStatus, Tree};
use super::view::View;

/// The file offset of an item's first line, then its place in that line (a
/// snapshot holds many).
type Order = (u64, u32);

/// The tail first ingested: this many items, or this many bytes.
pub const TAIL_ITEMS: usize = 48;
pub const TAIL_BYTES: usize = 256 * 1024;
const FIRST_STEP: usize = 64 * 1024;

/// How much the tail takes, and a fixed backfill step for tests.
#[derive(Debug, Clone, Copy)]
pub(crate) struct Tuning {
    pub tail_items: usize,
    pub tail_bytes: usize,
    pub step: Option<usize>,
}

impl Default for Tuning {
    fn default() -> Self {
        Tuning { tail_items: TAIL_ITEMS, tail_bytes: TAIL_BYTES, step: None }
    }
}

/// Where a batch's changes go: the tree, the change ring, and each View.
pub(crate) struct Sink<'a> {
    pub doc: &'a mut Tree,
    pub ring: &'a mut Ring,
    /// The generation the changes go under.
    pub generation: u64,
    pub views: Vec<&'a mut View>,
    pub changed: bool,
}

impl Sink<'_> {
    fn count(&self, key: Key) -> usize {
        self.doc.get(key).map_or(0, |id| self.doc.node(id).count as usize)
    }

    fn splice(&mut self, parent: Key, from: usize, to: Option<usize>, staged: Staged) {
        let id = self.doc.get(parent).expect("a splice's parent is in the tree");
        let to = to.unwrap_or_else(|| self.count(parent));
        if let Some(edit) = self.doc.splice(id, from, to, staged, self.ring, self.generation) {
            self.changed = true;
            for view in &mut self.views {
                view.apply_edit(self.doc, &edit);
            }
        }
    }

    /// The staged node `s`'s content onto the node with its key.
    fn update(&mut self, staged: &Staged, s: usize) {
        let Some(id) = self.doc.get(staged.nodes[s].key) else { return };
        if let Some(edit) = self.doc.update(id, staged, s, self.ring, self.generation) {
            self.changed = true;
            for view in &mut self.views {
                view.apply_edit(self.doc, &edit);
            }
        }
    }
}

/// A message's markdown and how far its streaming has frozen.
#[derive(Debug, Default)]
struct Markdown {
    raw: String,
    ended: bool,
    /// Top-level Blocks that won't be parsed again until the message ends.
    frozen: u32,
    /// Where the unfrozen tail starts in `raw`.
    tail: usize,
    /// The tail's last Block is a fenced code Block still open: its lines
    /// are appended without parsing.
    fence: Option<Fence>,
    /// The scratch string the tail's text is in.
    slot: Option<u32>,
}

impl Markdown {
    fn reset(&mut self) {
        self.frozen = 0;
        self.tail = 0;
        self.fence = None;
    }

    fn slot(&mut self, doc: &mut Tree) -> u32 {
        *self.slot.get_or_insert_with(|| doc.new_scratch())
    }

    fn end(&mut self, doc: &mut Tree) {
        self.reset();
        if let Some(slot) = self.slot.take() {
            doc.release_scratch(slot);
        }
    }

    /// The Blocks just parsed under `parent` from the tail: all but the last
    /// freeze, and the last's text stays in scratch.
    fn advance(&mut self, staged: &mut Staged, parent: u32, doc: &mut Tree) {
        let Some(&last) = staged.nodes[parent as usize].children.last() else { return };
        let roots = &staged.nodes[parent as usize].children;
        let at = (staged.nodes[last as usize].source.block.0 as usize).min(self.raw.len());
        let start = self.raw[..at].rfind('\n').map_or(0, |i| i + 1).max(self.tail);
        if roots.len() > 1 {
            self.frozen += roots.len() as u32 - 1;
            self.tail = start;
        }
        self.fence = None;
        if staged.nodes[last as usize].kind == Kind::Code {
            self.fence = Fence::open_at(&self.raw, start);
        }
        let slot = self.slot(doc);
        staged.scratch(last, slot);
    }

    /// From the start, as an insert does.
    fn stage(&mut self, staged: &mut Staged, parent: u32, doc: &mut Tree) {
        self.reset();
        if self.ended {
            self.end(doc);
            parse_into(staged, parent, &self.raw, 0);
        } else {
            let input = format!("{}{}", self.raw, repair(&self.raw));
            parse_into(staged, parent, &input, 0);
            self.advance(staged, parent, doc);
        }
    }

    /// What changed since the last render of the Container `key`.
    fn stream(&mut self, key: Key, sink: &mut Sink) {
        if self.ended {
            // Constructs that reach backwards resolve now; Blocks whose
            // content is unchanged are kept.
            let mut staged = Staged::under(key, 0);
            parse_into(&mut staged, 0, &self.raw, 0);
            sink.splice(key, 0, None, staged);
            self.end(sink.doc);
            return;
        }
        let slot = self.slot(sink.doc);
        if let Some(fence) = &mut self.fence {
            if !fence.closes(&self.raw) {
                let mut staged = Staged::under(key, self.frozen);
                let detail = Detail::Code { language: fence.language.clone() };
                let code = staged.push(0, Kind::Code, None, &fence.text(&self.raw), vec![], detail);
                staged.scratch(code, slot);
                sink.update(&staged, 1);
                return;
            }
        }
        let tail = &self.raw[self.tail..];
        let input = format!("{tail}{}", repair(tail));
        let mut staged = Staged::under(key, self.frozen);
        parse_into(&mut staged, 0, &input, self.tail as u32);
        let from = self.frozen as usize;
        self.advance(&mut staged, 0, sink.doc);
        sink.splice(key, from, None, staged);
    }
}

#[derive(Debug, Clone)]
struct ToolResult {
    status: ToolStatus,
    media_type: String,
    content: String,
    structured: Option<Value>,
    truncated: Option<Truncated>,
    ended: Option<u64>,
}

#[derive(Debug)]
enum Body {
    Text {
        role: Role,
        md: Markdown,
        /// A prompt sent from this View that the producer hasn't echoed.
        pending: bool,
    },
    Reasoning {
        md: Markdown,
    },
    Tool {
        name: String,
        title: String,
        kind: ToolKind,
        args: String,
        nesting: Nesting,
        result: Option<ToolResult>,
        /// Output while it runs, shown until the result replaces it.
        streamed: Option<Box<ToolResult>>,
        /// An open Interrupt names it.
        waiting: bool,
        started: Option<u64>,
        shown: Shown,
    },
    Subagent {
        status: ToolStatus,
    },
    Divider {
        text: String,
    },
    Notice {
        text: String,
    },
}

#[derive(Debug)]
struct Item {
    thread: String,
    order: Order,
    /// Its slot and order when it went in.
    placed: Option<(Key, Order)>,
    /// A snapshot's line set its content: earlier lines only move it.
    snapshot: Option<u64>,
    body: Body,
    dirty: bool,
}

impl Item {
    fn head(&self) -> (Kind, &str, Detail) {
        let section = |section, name: &str, title: &str, tool, status, duration_ms| Detail::Section {
            section,
            name: name.to_owned(),
            title: title.to_owned(),
            tool,
            status,
            duration_ms,
        };
        let streaming = |md: &Markdown| if md.ended { ToolStatus::Ok } else { ToolStatus::Pending };
        match &self.body {
            Body::Text { role, pending, .. } => (Kind::Turn, "", Detail::Turn { role: *role, pending: *pending }),
            Body::Reasoning { md } => (Kind::Section, "", section(SectionKind::Reasoning, "", "", ToolKind::Other, streaming(md), None)),
            Body::Tool { name, kind, result, waiting, started, shown, .. } => {
                let status = tool_head_status(result.as_ref(), *waiting);
                let duration = started.zip(result.as_ref().and_then(|r| r.ended)).and_then(|(start, end)| end.checked_sub(start));
                (Kind::Section, "", section(SectionKind::Tool, name, &shown.title, *kind, status, duration))
            }
            Body::Subagent { status } => {
                (Kind::Section, "", section(SectionKind::Subagent, "", &self.thread, ToolKind::Agent, *status, None))
            }
            Body::Divider { text } => (Kind::Divider, text, Detail::None),
            Body::Notice { text } => (Kind::Notice, text, Detail::None),
        }
    }
}

/// What a live View shows at its foot (the agent View prototype, #683):
/// the open top-level run and its newest unfinished tool call. Backfill
/// reads older lines, so only lines past `at` change it.
#[derive(Debug, Default)]
struct Activity {
    at: u64,
    run: Option<String>,
    /// The call's thread, id and title.
    tool: Option<(String, String, String)>,
}

#[derive(Debug, Default)]
struct Thread {
    parent: Option<(Parent, Order)>,
    items: Vec<Key>,
}

fn message_key(thread: &str, id: &str) -> Key {
    Key::named(&[thread, "message", id])
}

fn reasoning_key(thread: &str, id: &str) -> Key {
    Key::named(&[thread, "reasoning", id])
}

fn tool_key(thread: &str, id: &str) -> Key {
    Key::named(&[thread, "tool", id])
}

/// A tool call's input Block's key: always its first child.
fn input_key(call: Key) -> Key {
    Key::named(&["input", &call.0.to_string()])
}

/// A Tool Run's key, from its first call's: a run streaming in grows at its
/// end and keeps its key.
fn run_key(first: Key) -> Key {
    Key::named(&["run", &first.0.to_string()])
}

/// The slot of a thread outside the one a View shows: never placed, so
/// nothing goes in it.
const NOWHERE: Key = Key(0);

fn subagent_key(thread: &str) -> Key {
    Key::named(&[thread, "thread"])
}

/// For what has no id of its own, its `seq`, or else its offset: both last
/// across reloads of the same file.
fn seq_key(thread: &str, seq: u64, at: u64) -> Key {
    if seq == 0 {
        Key::named(&[thread, "at", &at.to_string()])
    } else {
        Key::named(&[thread, "seq", &seq.to_string()])
    }
}

fn role(role: EventRole) -> Role {
    match role {
        EventRole::User => Role::User,
        EventRole::System | EventRole::Developer => Role::System,
        _ => Role::Assistant,
    }
}

fn tool_kind(kind: Option<EventToolKind>) -> ToolKind {
    match kind.unwrap_or_default() {
        EventToolKind::Shell => ToolKind::Shell,
        EventToolKind::Edit => ToolKind::Edit,
        EventToolKind::Read => ToolKind::Read,
        EventToolKind::Search => ToolKind::Search,
        EventToolKind::Agent => ToolKind::Agent,
        EventToolKind::Web => ToolKind::Web,
        EventToolKind::Mcp => ToolKind::Mcp,
        EventToolKind::Other => ToolKind::Other,
    }
}

fn tool_status(status: Option<EventToolStatus>) -> ToolStatus {
    match status {
        Some(EventToolStatus::Failed) => ToolStatus::Failed,
        Some(EventToolStatus::Cancelled) => ToolStatus::Cancelled,
        _ => ToolStatus::Ok,
    }
}

fn tool_head_status(result: Option<&ToolResult>, waiting: bool) -> ToolStatus {
    match result {
        Some(r) => r.status,
        None if waiting => ToolStatus::Waiting,
        None => ToolStatus::Pending,
    }
}

fn result(e: &ToolCallResult) -> ToolResult {
    ToolResult {
        status: tool_status(e.status),
        media_type: e.media_type.clone().unwrap_or_default(),
        content: e.content.clone(),
        structured: e.structured.clone(),
        truncated: e.truncated.clone(),
        ended: e.timestamp,
    }
}

/// The item a non-creating event changes.
fn target(event: &Event) -> Option<Key> {
    Some(match event {
        Event::TextMessageContent(e) => message_key(&e.thread_id, &e.message_id),
        Event::TextMessageEnd(e) => message_key(&e.thread_id, &e.message_id),
        Event::ReasoningMessageContent(e) => reasoning_key(&e.thread_id, &e.message_id),
        Event::ReasoningMessageEnd(e) => reasoning_key(&e.thread_id, &e.message_id),
        Event::ToolCallArgs(e) => tool_key(&e.thread_id, &e.tool_call_id),
        Event::ToolCallEnd(e) => tool_key(&e.thread_id, &e.tool_call_id),
        Event::ToolCallOutput(e) => tool_key(&e.thread_id, &e.tool_call_id),
        Event::ToolCallResult(e) => tool_key(&e.thread_id, &e.tool_call_id),
        _ => return None,
    })
}

/// How many items an event starts, for choosing the tail.
fn starts(event: &Event) -> usize {
    match event {
        Event::TextMessageStart(_) | Event::ReasoningMessageStart(_) | Event::ToolCallStart(_) => 1,
        Event::MessagesSnapshot(e) => e.messages.len(),
        _ => 0,
    }
}

/// A string past this many characters is folded in a call's input.
const FOLD: usize = 80;
/// A search shows at most this many matches.
const MATCHES: usize = 500;

/// `text` up to `chars` characters, with an ellipsis if it goes on.
fn folded(text: &str, chars: usize) -> String {
    match text.char_indices().nth(chars) {
        Some((at, _)) => format!("{}…", &text[..at]),
        None => text.to_owned(),
    }
}

fn first_line(text: &str) -> &str {
    text.trim().lines().next().unwrap_or("")
}

/// What a call's header shows as its title (#667): the path an edit or a
/// read names and the pattern a search names, else the producer's title;
/// without one, a command, or the tool's name and its first argument.
fn shown_title(name: &str, title: &str, kind: ToolKind, value: Option<&Value>) -> String {
    let arg = |keys: &[&str]| value.and_then(|v| keys.iter().find_map(|k| v.get(*k)?.as_str())).filter(|s| !s.trim().is_empty());
    let named = match kind {
        ToolKind::Edit | ToolKind::Read => arg(&["file_path", "notebook_path", "path"]),
        ToolKind::Search => arg(&["pattern", "query"]),
        _ => None,
    };
    if let Some(named) = named {
        return first_line(named).to_owned();
    }
    if !title.is_empty() {
        return title.to_owned();
    }
    if kind == ToolKind::Shell {
        return arg(&["command"]).map_or_else(String::new, |c| first_line(c).to_owned());
    }
    let summary = value.and_then(Value::as_object).and_then(|o| o.values().find_map(|v| v.as_str().filter(|s| !s.trim().is_empty())));
    summary.map_or_else(String::new, |s| format!("{name} {}", folded(first_line(s), FOLD)))
}

fn fold_strings(value: &mut Value) {
    match value {
        Value::String(s) if s.chars().count() > FOLD => *s = folded(s, FOLD - 20),
        Value::Array(items) => items.iter_mut().for_each(fold_strings),
        Value::Object(fields) => fields.values_mut().for_each(fold_strings),
        _ => {}
    }
}

/// What a call's header and input Block show, worked out from its
/// arguments when they change.
#[derive(Debug, Clone, Default)]
struct Shown {
    title: String,
    /// A shell call's command whole, or else its arguments as compact JSON,
    /// long strings folded; and its language. None without arguments.
    input: Option<(String, &'static str)>,
}

/// How far a call's arguments have closed the JSON value they open, read a
/// delta at a time.
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct Nesting {
    depth: u32,
    string: bool,
    escape: bool,
    opened: bool,
}

impl Nesting {
    pub(crate) fn feed(&mut self, text: &str) {
        for b in text.bytes() {
            match (self.string, b) {
                (true, _) if self.escape => self.escape = false,
                (true, b'\\') => self.escape = true,
                (true, b'"') | (false, b'"') => self.string = !self.string,
                (false, b'{' | b'[') => (self.depth, self.opened) = (self.depth + 1, true),
                (false, b'}' | b']') => self.depth = self.depth.saturating_sub(1),
                _ => {}
            }
        }
    }

    /// Whether they may be a whole value: only then are they parsed, so a
    /// delta costs what it adds, however big the arguments get.
    pub(crate) fn closed(&self) -> bool {
        self.opened && self.depth == 0 && !self.string
    }
}

impl Shown {
    fn of(name: &str, title: &str, kind: ToolKind, args: &str, closed: bool) -> Shown {
        let args = args.trim();
        let value = if closed { serde_json::from_str::<Value>(args).ok() } else { None };
        let title = shown_title(name, title, kind, value.as_ref());
        let input = match value {
            _ if args.is_empty() => None,
            Some(value) if value.as_object().is_some_and(|o| o.is_empty()) => None,
            Some(value) if kind == ToolKind::Shell && value.get("command").and_then(Value::as_str).is_some() => {
                Some((value["command"].as_str().unwrap_or_default().to_owned(), "sh"))
            }
            Some(mut value) => {
                fold_strings(&mut value);
                Some((value.to_string(), "json"))
            }
            // Still streaming in, or not JSON.
            None => Some((folded(args, 2 * FOLD), "")),
        };
        Shown { title, input }
    }
}

/// A call's input Block under `parent`, if it has input.
fn stage_input(call: Key, shown: &Shown, staged: &mut Staged, parent: u32) -> Option<u32> {
    let (text, language) = shown.input.as_ref()?;
    Some(staged.push(parent, Kind::Input, Some(input_key(call)), text, vec![], Detail::Code { language: (*language).into() }))
}

/// How many of the call `key`'s first children are its input: 0 or 1.
fn lead(doc: &Tree, key: Key) -> usize {
    let first = doc.get(key).and_then(|id| doc.children(id).next());
    usize::from(first.is_some_and(|c| doc.key(c) == input_key(key)))
}

/// Where a search match's `path:line` ends in it, or 0.
pub(crate) fn location(line: &str) -> usize {
    let bytes = line.as_bytes();
    for (colon, _) in line.match_indices(':').filter(|&(i, _)| i > 0) {
        let digits = bytes[colon + 1..].iter().take_while(|b| b.is_ascii_digit()).count();
        let end = colon + 1 + digits;
        if digits > 0 && (end == bytes.len() || matches!(bytes[end], b':' | b'-')) {
            return end;
        }
    }
    0
}

/// The result's Blocks under `parent`, by the call's kind, then media type:
/// a read's file as code in the language of its path's extension, a
/// search's matches as a list.
fn stage_result(r: &ToolResult, kind: ToolKind, path: &str, staged: &mut Staged, parent: u32) {
    let media = r.media_type.as_str();
    let text = |content: &str| {
        let mut log = Log::default();
        log.feed(content);
        log.chunks().into_iter().map(|chunk| chunk.text).collect::<Vec<_>>().join("\n")
    };
    if kind == ToolKind::Read && !media.starts_with("image/") {
        // A title that isn't a path, such as "Read v1.2", names no language.
        let file = path.rsplit(['/', '\\']).next().unwrap_or("");
        let extension = file.rsplit_once('.').map_or("", |(_, extension)| extension);
        let named = !file.contains(char::is_whitespace)
            && extension.chars().all(|c| c.is_ascii_alphanumeric())
            && extension.chars().any(|c| c.is_ascii_alphabetic());
        let language = if named { extension } else { "" };
        // A file's text is shown as it is: only terminal output has escapes
        // and carriage returns to interpret.
        let content = if media == ANSI { text(&r.content) } else { r.content.trim_end_matches('\n').to_owned() };
        staged.push(parent, Kind::Code, None, &content, vec![], Detail::Code { language: language.into() });
    } else if kind == ToolKind::Search && media != MARKDOWN && media != DIFF && !media.starts_with("image/") {
        // Any other output, JSON included, is lines of matches: markdown, a
        // diff and an image keep the Blocks their media types give them.
        let content = text(&r.content);
        let lines: Vec<&str> = content.lines().filter(|l| !l.trim().is_empty()).collect();
        if !lines.is_empty() {
            let list = staged.push(parent, Kind::List, None, "", vec![], Detail::List { ordered: false, start: 1, tight: true });
            for line in lines.iter().take(MATCHES) {
                let item = staged.push(list, Kind::ListItem, None, "", vec![], Detail::Item { task: None });
                let end = location(line) as u32;
                let spans = if end > 0 { vec![Span { start: 0, len: end, attr: Attr::Code }] } else { vec![] };
                staged.push(item, Kind::Paragraph, None, line, spans, Detail::None);
            }
        }
        if lines.len() > MATCHES {
            let text = format!("{} more matches", lines.len() - MATCHES);
            staged.push(parent, Kind::Notice, None, &text, vec![], Detail::None);
        }
    } else if media == MARKDOWN {
        parse_into(staged, parent, &r.content, 0);
    } else if media == DIFF {
        let patch = r.structured.as_ref().and_then(|s| s.get("patch").or(Some(s))).and_then(Value::as_str);
        let text = if r.content.is_empty() { patch.unwrap_or("") } else { &r.content };
        let text = text.strip_suffix('\n').unwrap_or(text);
        staged.push(parent, Kind::Code, None, text, vec![], Detail::Code { language: "diff".into() });
    } else if let Some(subtype) = media.strip_prefix("image/") {
        let alt = r.structured.as_ref().and_then(|s| s.get("alt")).and_then(Value::as_str).unwrap_or("");
        let text = if alt.is_empty() { format!("A {subtype} image, not shown") } else { alt.to_owned() };
        staged.push(parent, Kind::Notice, None, &text, vec![], Detail::None);
    } else {
        // text/x-ansi, and text/plain or anything else as ANSI with no codes.
        let mut log = Log::default();
        log.feed(&r.content);
        for chunk in log.chunks() {
            staged.push_with_strings(parent, Kind::Log, &chunk.text, chunk.spans, chunk.strings);
        }
    }
    if let Some(truncated) = &r.truncated {
        let text = format!("Output truncated: {} bytes in all, the rest at {}", truncated.total_bytes, truncated.reference);
        staged.push(parent, Kind::Notice, None, &text, vec![], Detail::None);
    }
}

#[derive(Debug, Default)]
pub(crate) struct Conversation {
    /// The bytes opened with, kept until backfill has read them.
    backlog: Vec<u8>,
    /// Backfill reads the lines before this; 0 once it's done.
    cut: usize,
    /// Where backfill stops: after the hello.
    floor: usize,
    lines: Lines,
    unsupported: bool,
    /// Where the producer takes input, from the hello.
    input: Option<String>,
    activity: Activity,
    /// The ids of user messages, so a sent prompt knows it arrived.
    user_messages: HashSet<String>,
    /// Prompts sent from this View and not yet echoed, by message id: each
    /// shows at the end until the producer's own message replaces it.
    pending: HashMap<String, Key>,
    /// Pending prompts to take out on the next settle.
    withdrawn: Vec<Key>,
    /// Prompts pended so far, so each goes after the last.
    pended: u32,
    items: HashMap<Key, Item>,
    threads: HashMap<String, Thread>,
    /// Each slot's placed items, in order.
    slots: HashMap<Key, Vec<(Order, Key)>>,
    /// Events for items whose first line backfill hasn't reached yet.
    orphans: HashMap<Key, Vec<(u64, Event)>>,
    /// The subagent threads each tool call started, by the call's key.
    subagents: HashMap<Key, Vec<String>>,
    /// Each call an open Interrupt names: the Interrupt's id and line. Only a
    /// resume entry closes one, `cancelled` included; a cancelled run doesn't.
    /// (AG-UI: a run resumes its Interrupts through resume entries.)
    waiting: HashMap<Key, (String, u64)>,
    /// The line that answered each Interrupt. Backfill reads an answer
    /// before the Interrupt it answers.
    answered: HashMap<String, u64>,
    touched: Vec<Key>,
    touched_set: HashSet<Key>,
    created: Vec<Key>,
    /// Backfill's bytes per step, tuned to its budget.
    step: usize,
    tuning: Tuning,
    /// The thread shown at the root: a subagent's, opened as its own View;
    /// None for the conversation's own threads.
    root: Option<String>,
}

impl Conversation {
    /// Ingests the hello and the tail; the rest waits for backfill.
    pub fn open(bytes: &[u8], tuning: Tuning, root: Option<&str>, sink: &mut Sink) -> Conversation {
        let head = bytes.iter().rposition(|&b| b == b'\n').map_or(0, |i| i + 1);
        let mut conversation = Conversation {
            backlog: bytes[..head].to_vec(),
            cut: head,
            lines: Lines::starting_at(head as u64),
            step: tuning.step.unwrap_or(FIRST_STEP),
            tuning,
            root: root.map(str::to_owned),
            ..Conversation::default()
        };
        let first = conversation.backlog.iter().position(|&b| b == b'\n').map_or(head, |i| i + 1);
        if let Ok(Some(hello @ Event::Hello(_))) = parse_line(&conversation.backlog[..first]) {
            conversation.floor = first;
            conversation.apply(0, hello);
        }
        let events = conversation.take_back(|items, bytes| items >= tuning.tail_items || bytes >= tuning.tail_bytes);
        conversation.ingest(events, sink);
        conversation.append(&bytes[head..], sink);
        conversation
    }

    pub fn input(&self) -> Option<&str> {
        self.input.as_deref()
    }

    /// Whether a top-level run is open, and its running tool's title.
    pub fn activity(&self) -> (bool, Option<&str>) {
        let tool = self.activity.tool.as_ref().map(|(_, _, title)| title.as_str());
        (self.activity.run.is_some(), tool)
    }

    pub fn has_user_message(&self, id: &str) -> bool {
        self.user_messages.contains(id)
    }

    pub fn is_pending(&self, id: &str) -> bool {
        self.pending.contains_key(id)
    }

    /// Shows a prompt sent as message `id` at the end, marked pending,
    /// until a user message with that id arrives. Later items go above it.
    pub fn pend(&mut self, id: &str, text: &str, sink: &mut Sink) {
        if self.user_messages.contains(id) || self.pending.contains_key(id) {
            return;
        }
        let key = Key::named(&["", "pending", id]);
        let order = (u64::MAX, self.pended);
        self.pended += 1;
        let md = Markdown { raw: text.to_owned(), ended: true, ..Markdown::default() };
        self.create(key, order, "", || Body::Text { role: Role::User, md, pending: true });
        self.pending.insert(id.to_owned(), key);
        self.settle(sink);
    }

    /// Takes out a pending prompt that won't arrive, such as one that
    /// failed to send.
    pub fn withdraw(&mut self, id: &str, sink: &mut Sink) {
        if let Some(key) = self.pending.remove(id) {
            self.withdrawn.push(key);
            self.settle(sink);
        }
    }

    fn note_activity(&mut self, at: u64, event: &Event) {
        if at < self.activity.at {
            return;
        }
        let thread = event.thread_id();
        if self.threads.get(thread).is_some_and(|t| t.parent.is_some()) {
            return;
        }
        let activity = &mut self.activity;
        match event {
            Event::RunStarted(e) if e.parent.is_none() => (activity.run, activity.tool) = (Some(e.run_id.clone()), None),
            Event::RunFinished(_) | Event::RunError(_) => (activity.run, activity.tool) = (None, None),
            Event::ToolCallStart(e) => {
                activity.tool = Some((thread.to_owned(), e.tool_call_id.clone(), e.title.clone().unwrap_or_default()))
            }
            Event::ToolCallResult(e) if activity.tool.as_ref().is_some_and(|(t, id, _)| t == thread && *id == e.tool_call_id) => {
                activity.tool = None
            }
            _ => return,
        }
        activity.at = at;
    }

    pub fn append(&mut self, bytes: &[u8], sink: &mut Sink) {
        let mut events = vec![];
        self.lines.feed(bytes, |at, line| {
            if let Ok(Some(event)) = parse_line(line) {
                events.push((at, event));
            }
        });
        self.ingest(events, sink);
    }

    /// Bytes backfill has yet to read.
    pub fn remaining(&self) -> usize {
        self.cut.saturating_sub(self.floor)
    }

    /// Ingests earlier lines, inserted at the front, for about `budget`.
    pub fn backfill(&mut self, budget: Duration, sink: &mut Sink) -> usize {
        if self.cut == 0 {
            return 0;
        }
        let started = Instant::now();
        let before = self.cut;
        let step = self.step;
        let events = self.take_back(|_, bytes| bytes >= step);
        self.ingest(events, sink);
        let spent = started.elapsed().as_secs_f64().max(1e-6);
        let rate = (before - self.cut.max(self.floor)) as f64 / spent;
        let next = (rate * budget.as_secs_f64()) as usize;
        self.step = self.tuning.step.unwrap_or(((self.step + next) / 2).clamp(4 * 1024, 8 * 1024 * 1024));
        self.remaining()
    }

    /// Lines before the cut, read backward until `enough(items, bytes)`.
    fn take_back(&mut self, enough: impl Fn(usize, usize) -> bool) -> Vec<(u64, Event)> {
        let mut events = vec![];
        let (mut items, end) = (0, self.cut);
        let mut at = self.cut;
        while at > self.floor && !enough(items, end - at) {
            let start = self.backlog[self.floor..at - 1].iter().rposition(|&b| b == b'\n').map_or(self.floor, |i| self.floor + i + 1);
            if let Ok(Some(event)) = parse_line(&self.backlog[start..at]) {
                items += starts(&event);
                events.push((start as u64, event));
            }
            at = start;
        }
        self.cut = at;
        if at <= self.floor {
            self.cut = 0;
            self.backlog = vec![];
        }
        events.reverse();
        events
    }

    fn pending(&self) -> bool {
        self.cut > 0
    }

    fn ingest(&mut self, events: Vec<(u64, Event)>, sink: &mut Sink) {
        self.created.clear();
        for (at, event) in events {
            self.apply(at, event);
        }
        for key in std::mem::take(&mut self.created) {
            if let Some(mut orphans) = self.orphans.remove(&key) {
                orphans.sort_by_key(|(at, _)| *at);
                for (at, event) in orphans {
                    self.apply(at, event);
                }
            }
        }
        if !self.pending() {
            self.orphans.clear();
        }
        self.settle(sink);
    }

    fn touch(&mut self, key: Key) {
        if self.touched_set.insert(key) {
            self.touched.push(key);
        }
    }

    /// The item `key`, created at `order` if it's new, or moved earlier.
    /// None if a later snapshot already set its content.
    fn create(&mut self, key: Key, order: Order, thread: &str, body: impl FnOnce() -> Body) -> Option<&mut Item> {
        self.touch(key);
        match self.items.entry(key) {
            MapEntry::Occupied(entry) => {
                let item = entry.into_mut();
                item.order = item.order.min(order);
                item.dirty = true;
                if item.snapshot.is_some_and(|s| order.0 < s) {
                    None
                } else {
                    Some(item)
                }
            }
            MapEntry::Vacant(entry) => {
                self.created.push(key);
                self.threads.entry(thread.to_owned()).or_default().items.push(key);
                let item = Item { thread: thread.to_owned(), order, placed: None, snapshot: None, body: body(), dirty: true };
                Some(entry.insert(item))
            }
        }
    }

    fn apply(&mut self, at: u64, event: Event) {
        if self.unsupported {
            return;
        }
        self.note_activity(at, &event);
        if let Event::TextMessageStart(e) = &event {
            if e.role == EventRole::User {
                self.user_messages.insert(e.message_id.clone());
                if let Some(key) = self.pending.remove(&e.message_id) {
                    self.withdrawn.push(key);
                }
            }
        }
        if let Some(key) = target(&event) {
            match self.items.get(&key) {
                None => {
                    if self.pending() {
                        self.orphans.entry(key).or_default().push((at, event));
                    }
                    return;
                }
                Some(item) if item.snapshot.is_some_and(|s| at < s) => return,
                Some(_) => {
                    self.touch(key);
                    self.items.get_mut(&key).unwrap().dirty = true;
                }
            }
        }
        let thread = event.thread_id().to_owned();
        let order = (at, 0);
        match event {
            Event::Hello(hello) => {
                self.input = hello.input;
                if hello.contract != CONTRACT {
                    self.unsupported = true;
                    let text = format!("Conversation contract {} isn't supported (this reads {CONTRACT})", hello.contract);
                    self.create(Key::named(&["", "hello"]), order, "", || Body::Notice { text });
                }
            }
            Event::RunStarted(e) => {
                for entry in e.input.iter().flat_map(|input| &input.resume) {
                    self.answered.insert(entry.interrupt_id.clone(), at);
                    let answered: Vec<Key> =
                        self.waiting.iter().filter(|(_, (id, from))| *id == entry.interrupt_id && *from < at).map(|(k, _)| *k).collect();
                    for key in answered {
                        self.waiting.remove(&key);
                        self.set_waiting(key, false);
                    }
                }
                if let Some(parent) = e.parent {
                    let entry = self.threads.entry(thread.clone()).or_default();
                    if entry.parent.as_ref().is_none_or(|(_, o)| order < *o) {
                        let call = tool_key(&parent.thread_id, &parent.tool_call_id);
                        entry.parent = Some((parent, order));
                        self.subagents.entry(call).or_default().push(thread.clone());
                    }
                    for key in entry.items.clone() {
                        self.touch(key);
                    }
                    self.create(subagent_key(&thread), order, &thread, || Body::Subagent { status: ToolStatus::Pending });
                }
            }
            Event::RunFinished(ref e) => {
                let status = match &e.outcome {
                    Some(Outcome::Cancelled) => {
                        self.create(seq_key(&thread, e.seq, at), order, &thread, || Body::Divider { text: "cancelled".into() });
                        ToolStatus::Cancelled
                    }
                    Some(Outcome::Interrupt { interrupts }) => {
                        for (i, interrupt) in interrupts.iter().enumerate() {
                            match &interrupt.tool_call_id {
                                Some(call) if self.answered.get(&interrupt.id).is_none_or(|&answer| answer < at) => {
                                    let key = tool_key(&thread, call);
                                    self.waiting.insert(key, (interrupt.id.clone(), at));
                                    self.set_waiting(key, true);
                                }
                                Some(_) => {}
                                // Drawing what it asks is #695's; until then it's a notice.
                                None => {
                                    let text = interrupt.message.clone().unwrap_or_else(|| interrupt.reason.clone());
                                    let key = Key::named(&[&thread, &interrupt.id, "interrupt"]);
                                    self.create(key, (at, i as u32), &thread, || Body::Notice { text });
                                }
                            }
                        }
                        ToolStatus::Waiting
                    }
                    _ => ToolStatus::Ok,
                };
                self.subagent_status(at, event, status);
            }
            Event::RunError(ref e) => {
                let text = e.message.clone();
                self.create(seq_key(&thread, e.seq, at), order, &thread, || Body::Notice { text });
                self.subagent_status(at, event, ToolStatus::Failed);
            }
            Event::TextMessageStart(e) => {
                let r = role(e.role);
                if let Some(Item { body: Body::Text { role, .. }, .. }) =
                    self.create(message_key(&thread, &e.message_id), order, &thread, || Body::Text {
                        role: r,
                        md: Markdown::default(),
                        pending: false,
                    })
                {
                    *role = r;
                }
            }
            Event::ReasoningMessageStart(e) => {
                self.create(reasoning_key(&thread, &e.message_id), order, &thread, || Body::Reasoning { md: Markdown::default() });
            }
            Event::TextMessageContent(e) => self.markdown(message_key(&thread, &e.message_id), |md| md.raw.push_str(&e.delta)),
            Event::ReasoningMessageContent(e) => self.markdown(reasoning_key(&thread, &e.message_id), |md| md.raw.push_str(&e.delta)),
            Event::TextMessageEnd(e) => self.markdown(message_key(&thread, &e.message_id), |md| md.ended = true),
            Event::ReasoningMessageEnd(e) => self.markdown(reasoning_key(&thread, &e.message_id), |md| md.ended = true),
            Event::ToolCallStart(e) => {
                let (n, t, k) = (e.tool_call_name, e.title.unwrap_or_default(), tool_kind(e.kind));
                self.tool(at, order, &thread, &e.tool_call_id, (n, t, k, e.timestamp), None);
            }
            Event::ToolCallArgs(e) => {
                if let Some(Body::Tool { name, title, kind, args, nesting, shown, .. }) =
                    self.items.get_mut(&tool_key(&thread, &e.tool_call_id)).map(|i| &mut i.body)
                {
                    args.push_str(&e.delta);
                    nesting.feed(&e.delta);
                    *shown = Shown::of(name, title, *kind, args, nesting.closed());
                }
            }
            Event::ToolCallEnd(e) => self.retitle(tool_key(&thread, &e.tool_call_id), e.title, e.kind),
            Event::ToolCallOutput(e) => {
                if let Some(Body::Tool { result: None, streamed, .. }) =
                    self.items.get_mut(&tool_key(&thread, &e.tool_call_id)).map(|i| &mut i.body)
                {
                    let streamed = streamed.get_or_insert_with(|| {
                        Box::new(ToolResult {
                            status: ToolStatus::Pending,
                            media_type: e.media_type.clone().unwrap_or_default(),
                            content: String::new(),
                            structured: None,
                            truncated: None,
                            ended: None,
                        })
                    });
                    streamed.content.push_str(&e.delta);
                }
            }
            Event::ToolCallResult(e) => {
                let key = tool_key(&thread, &e.tool_call_id);
                if let Some(Body::Tool { result: r, streamed, .. }) = self.items.get_mut(&key).map(|i| &mut i.body) {
                    *r = Some(result(&e));
                    *streamed = None;
                }
                self.retitle(key, e.title, e.kind);
            }
            Event::MessagesSnapshot(e) => self.snapshot(at, &thread, e.messages),
            Event::Custom(e) if e.name == COMPACTION => {
                self.create(seq_key(&thread, e.seq, at), order, &thread, || Body::Divider { text: COMPACTION.into() });
            }
            _ => {}
        }
    }

    /// A title or kind that came after the call's start: the latest wins.
    fn retitle(&mut self, key: Key, title: Option<String>, kind: Option<EventToolKind>) {
        if title.is_none() && kind.is_none() {
            return;
        }
        if let Some(Body::Tool { name, title: t, kind: k, args, nesting, shown, .. }) = self.items.get_mut(&key).map(|i| &mut i.body) {
            if let Some(title) = title {
                *t = title;
            }
            if kind.is_some() {
                *k = tool_kind(kind);
            }
            *shown = Shown::of(name, t, *k, args, nesting.closed());
        }
    }

    fn set_waiting(&mut self, key: Key, waiting: bool) {
        if let Some(item) = self.items.get_mut(&key) {
            if let Body::Tool { waiting: w, .. } = &mut item.body {
                *w = waiting;
                item.dirty = true;
                self.touch(key);
            }
        }
    }

    fn markdown(&mut self, key: Key, f: impl FnOnce(&mut Markdown)) {
        if let Some(Body::Text { md, .. } | Body::Reasoning { md }) = self.items.get_mut(&key).map(|i| &mut i.body) {
            f(md);
        }
    }

    /// A subagent's run ending, kept for its Section until it's known.
    fn subagent_status(&mut self, at: u64, event: Event, status: ToolStatus) {
        let key = subagent_key(event.thread_id());
        match self.items.get_mut(&key) {
            Some(item) => {
                if let Body::Subagent { status: s } = &mut item.body {
                    *s = status;
                    item.dirty = true;
                    self.touch(key);
                }
            }
            None if self.cut > 0 => self.orphans.entry(key).or_default().push((at, event)),
            None => {}
        }
    }

    /// A call's start: its name, title, kind and timestamp.
    fn tool(
        &mut self,
        at: u64,
        order: Order,
        thread: &str,
        id: &str,
        head: (String, String, ToolKind, Option<u64>),
        snapshot: Option<&str>,
    ) {
        let key = tool_key(thread, id);
        let (name, title, kind, started) = head;
        let shown = Shown::of(&name, &title, kind, "", false);
        let fresh = Body::Tool {
            name: name.clone(),
            title: title.clone(),
            kind,
            args: String::new(),
            nesting: Nesting::default(),
            result: None,
            streamed: None,
            waiting: self.waiting.contains_key(&key),
            started,
            shown,
        };
        if let Some(item) = self.create(key, order, thread, || fresh) {
            if let Body::Tool { name: n, title: t, kind: k, args, nesting, started: s, shown, .. } = &mut item.body {
                (*n, *t, *k, *s) = (name, title, kind, started);
                if let Some(arguments) = snapshot {
                    *args = arguments.to_owned();
                    *nesting = Nesting::default();
                    nesting.feed(args);
                    item.snapshot = Some(at);
                }
                *shown = Shown::of(n, t, *k, args, nesting.closed());
            }
        }
        // Subagents it started now go inside it.
        for name in self.subagents.get(&key).cloned().unwrap_or_default() {
            self.touch(subagent_key(&name));
        }
    }

    /// The record form: complete items, which later stream events extend.
    fn snapshot(&mut self, at: u64, thread: &str, messages: Vec<Message>) {
        let mut sub = 0;
        let mut next = || {
            sub += 1;
            (at, sub - 1)
        };
        for m in messages {
            match m.role {
                EventRole::Tool => {
                    let Some(call) = m.tool_call_id.clone() else { continue };
                    let key = tool_key(thread, &call);
                    let r = ToolResult {
                        status: tool_status(m.status),
                        media_type: m.media_type.clone().unwrap_or_default(),
                        content: m.text(),
                        structured: m.structured.clone(),
                        truncated: m.truncated.clone(),
                        ended: None,
                    };
                    match self.items.get_mut(&key) {
                        Some(item) if item.snapshot.is_none_or(|s| s <= at) => {
                            if let Body::Tool { result, .. } = &mut item.body {
                                *result = Some(r);
                                item.dirty = true;
                                self.touch(key);
                            }
                        }
                        Some(_) => {}
                        None if self.cut > 0 => {
                            let event = ToolCallResult {
                                thread_id: thread.to_owned(),
                                message_id: m.id.clone(),
                                tool_call_id: call,
                                content: r.content,
                                status: m.status,
                                media_type: m.media_type,
                                structured: m.structured,
                                truncated: m.truncated,
                                ..ToolCallResult::default()
                            };
                            self.orphans.entry(key).or_default().push((at, Event::ToolCallResult(event)));
                        }
                        None => {}
                    }
                }
                EventRole::Reasoning => {
                    let md = Markdown { raw: m.text(), ended: true, ..Markdown::default() };
                    let order = next();
                    if let Some(item) =
                        self.create(reasoning_key(thread, &m.id), order, thread, || Body::Reasoning { md: Markdown::default() })
                    {
                        item.body = Body::Reasoning { md };
                        item.snapshot = Some(at);
                    }
                }
                EventRole::Activity => {}
                r => {
                    let text = m.text();
                    if !text.is_empty() || m.tool_calls.is_empty() {
                        let order = next();
                        let md = Markdown { raw: text, ended: true, ..Markdown::default() };
                        let body = Body::Text { role: role(r), md, pending: false };
                        if let Some(item) = self.create(message_key(thread, &m.id), order, thread, || Body::Notice { text: String::new() })
                        {
                            item.body = body;
                            item.snapshot = Some(at);
                        }
                    }
                    for call in m.tool_calls {
                        let title = call.title.unwrap_or_default();
                        let order = next();
                        let head = (call.function.name, title, tool_kind(call.kind), None);
                        self.tool(at, order, thread, &call.id, head, Some(&call.function.arguments));
                    }
                }
            }
        }
    }

    fn thread_slot(&self, thread: &str) -> Key {
        if self.root.as_deref() == Some(thread) {
            return Key::ROOT;
        }
        match self.threads.get(thread).and_then(|t| t.parent.as_ref()) {
            Some(_) => subagent_key(thread),
            None if self.root.is_some() => NOWHERE,
            None => Key::ROOT,
        }
    }

    fn slot(&self, item: &Item) -> Key {
        if let Body::Subagent { .. } = item.body {
            if let Some((parent, _)) = self.threads.get(&item.thread).and_then(|t| t.parent.as_ref()) {
                let tool = tool_key(&parent.thread_id, &parent.tool_call_id);
                return if self.items.contains_key(&tool) { tool } else { self.thread_slot(&parent.thread_id) };
            }
        }
        self.thread_slot(&item.thread)
    }

    fn placed(&self, slot: Key) -> bool {
        slot == Key::ROOT || self.items.get(&slot).is_some_and(|i| i.placed.is_some())
    }

    /// An item with its own Blocks, under the staged node `parent`.
    fn stage(&mut self, key: Key, staged: &mut Staged, parent: u32, doc: &mut Tree) {
        let item = self.items.get_mut(&key).expect("a staged item exists");
        let (kind, text, detail) = item.head();
        let node = staged.push(parent, kind, Some(key), text, vec![], detail);
        match &mut item.body {
            Body::Text { md, .. } | Body::Reasoning { md } => md.stage(staged, node, doc),
            Body::Tool { kind, result, streamed, shown, .. } => {
                if let Some(r) = result.as_ref().or(streamed.as_deref()) {
                    stage_result(r, *kind, &shown.title, staged, node);
                }
                // Staged after the result, so the result's ordinals are as
                // an update stages them, then moved first.
                if stage_input(key, shown, staged, node).is_some() {
                    staged.nodes[node as usize].children.rotate_right(1);
                }
            }
            _ => {}
        }
        item.dirty = false;
    }

    /// An item the Tree already has, as it is there, or else a new one.
    fn stage_placed(&mut self, key: Key, staged: &mut Staged, parent: u32, doc: &mut Tree) {
        match doc.get(key) {
            Some(id) => {
                staged.copy(parent, doc, id);
            }
            None => self.stage(key, staged, parent, doc),
        }
    }

    fn is_call(&self, key: Key) -> bool {
        matches!(self.items.get(&key).map(|i| &i.body), Some(Body::Tool { .. }))
    }

    /// The group `list[i]` is in: its run of consecutive calls, or itself.
    /// A slot's children in the Tree are its groups, a run of two or more
    /// calls as a Tool Run (#667).
    fn group_at(&self, list: &[(Order, Key)], i: usize) -> Range<usize> {
        if !self.is_call(list[i].1) {
            return i..i + 1;
        }
        let (mut start, mut end) = (i, i + 1);
        while start > 0 && self.is_call(list[start - 1].1) {
            start -= 1;
        }
        while end < list.len() && self.is_call(list[end].1) {
            end += 1;
        }
        start..end
    }

    /// The groups of `items`, which start and end with whole groups.
    fn groups(&self, items: &[(Order, Key)]) -> Vec<Range<usize>> {
        let mut out: Vec<Range<usize>> = vec![];
        let mut prev = false;
        for (i, &(_, key)) in items.iter().enumerate() {
            let call = self.is_call(key);
            match out.last_mut() {
                Some(last) if call && prev => last.end = i + 1,
                _ => out.push(i..i + 1),
            }
            prev = call;
        }
        out
    }

    /// The Tree index of the group starting at `list[lo]`, where `list` is
    /// `slot`'s list now, `old` was its window `lo..hi`, and `list[hi..]` is
    /// unchanged: counted from whichever end of the list is nearer, so an
    /// append costs what its window does. Only the root and a subagent's
    /// Section hold nothing but groups, so a call's count from its start.
    fn group_index(&self, slot: Key, list: &[(Order, Key)], lo: usize, hi: usize, old: &[(Order, Key)], doc: &Tree) -> usize {
        let only_groups = slot == Key::ROOT || matches!(self.items.get(&slot).map(|i| &i.body), Some(Body::Subagent { .. }));
        if only_groups && list.len() - hi < lo {
            let count = doc.get(slot).map_or(0, |id| doc.node(id).count as usize);
            return count - self.groups(old).len() - self.groups(&list[hi..]).len();
        }
        (0..lo).filter(|&i| i == 0 || !(self.is_call(list[i].1) && self.is_call(list[i - 1].1))).count()
    }

    fn run_detail(&self, calls: &[(Order, Key)]) -> Detail {
        Detail::ToolRun(RunSummary::new(calls.iter().filter_map(|(_, key)| match &self.items.get(key)?.body {
            Body::Tool { kind, result, waiting, .. } => Some((*kind, tool_head_status(result.as_ref(), *waiting))),
            _ => None,
        })))
    }

    fn update_run(&self, calls: &[(Order, Key)], sink: &mut Sink) {
        let key = run_key(calls[0].1);
        let mut head = Staged::under(key, 0);
        head.push(0, Kind::ToolRun, Some(key), "", vec![], self.run_detail(calls));
        sink.update(&head, 1);
    }

    /// The Tool Run `key` is in, as its calls.
    fn run_of(&self, key: Key) -> Option<&[(Order, Key)]> {
        let (slot, order) = self.items.get(&key)?.placed?;
        let list = self.slots.get(&slot)?;
        let group = self.group_at(list, list.binary_search(&(order, key)).ok()?);
        (group.len() > 1).then(|| &list[group])
    }

    /// A change to `slot`'s items, into the Tree: `old`, whose groups start
    /// at the slot's child `at`, is now `new`. Both start and end with whole
    /// groups, and only the groups that differ are staged again; a run that
    /// keeps its first call keeps its Tool Run, and only its calls change.
    fn regroup(&mut self, slot: Key, at: usize, old: &[(Order, Key)], new: &[(Order, Key)], sink: &mut Sink) {
        let (was, now) = (self.groups(old), self.groups(new));
        let same = |a: &[(Order, Key)], b: &[(Order, Key)]| a.iter().map(|e| e.1).eq(b.iter().map(|e| e.1));
        let (prefix, suffix) = trim(was.len(), now.len(), |i, j| same(&old[was[i].clone()], &new[now[j].clone()]));
        let (was, now) = (&was[prefix..was.len() - suffix], &now[prefix..now.len() - suffix]);
        if let ([w], [n]) = (was, now) {
            let (old, new) = (&old[w.clone()], &new[n.clone()]);
            if old.len() > 1 && new.len() > 1 && old[0].1 == new[0].1 {
                self.update_run(new, sink);
                let (prefix, suffix) = trim(old.len(), new.len(), |i, j| old[i].1 == new[j].1);
                let run = run_key(new[0].1);
                let mut staged = Staged::under(run, 0);
                for &(_, key) in &new[prefix..new.len() - suffix] {
                    self.stage_placed(key, &mut staged, 0, sink.doc);
                }
                sink.splice(run, prefix, Some(old.len() - suffix), staged);
                return;
            }
        }
        let mut staged = Staged::under(slot, 0);
        for group in now {
            let items = &new[group.clone()];
            let parent = if items.len() > 1 {
                staged.push(0, Kind::ToolRun, Some(run_key(items[0].1)), "", vec![], self.run_detail(items))
            } else {
                0
            };
            for &(_, key) in items {
                self.stage_placed(key, &mut staged, parent, sink.doc);
            }
        }
        let at = at + lead(sink.doc, slot);
        sink.splice(slot, at + prefix, Some(at + prefix + was.len()), staged);
    }

    /// Takes an item out of its slot, and every item placed inside it.
    fn unplace(&mut self, key: Key, sink: &mut Sink, again: &mut Vec<Key>) {
        let Some((slot, order)) = self.items.get_mut(&key).and_then(|i| i.placed.take()) else { return };
        let mut list = self.slots.remove(&slot).expect("a placed item's slot");
        let at = list.binary_search(&(order, key)).expect("a placed item is in its slot");
        // Its neighbours' groups, which it may have kept apart or held together.
        let lo = if at > 0 { self.group_at(&list, at - 1).start } else { 0 };
        let hi = if at + 1 < list.len() { self.group_at(&list, at + 1).end } else { list.len() };
        let old = list[lo..hi].to_vec();
        list.remove(at);
        let first = self.group_index(slot, &list, lo, hi - 1, &old, sink.doc);
        self.regroup(slot, first, &old, &list[lo..hi - 1], sink);
        self.slots.insert(slot, list);
        let mut stack = vec![key];
        while let Some(container) = stack.pop() {
            for (_, inner) in self.slots.remove(&container).unwrap_or_default() {
                if let Some(item) = self.items.get_mut(&inner) {
                    item.placed = None;
                    item.dirty = true;
                }
                again.push(inner);
                stack.push(inner);
            }
        }
    }

    /// Places what moved or is new, a run of neighbours in one splice, then
    /// renders what changed in place.
    fn settle(&mut self, sink: &mut Sink) {
        for key in std::mem::take(&mut self.withdrawn) {
            self.unplace(key, sink, &mut vec![]);
            if let Some(item) = self.items.remove(&key) {
                if let Some(thread) = self.threads.get_mut(&item.thread) {
                    thread.items.retain(|&k| k != key);
                }
            }
        }
        let mut touched = std::mem::take(&mut self.touched);
        touched.retain(|key| self.items.contains_key(key));
        self.touched_set.clear();
        let mut wanted: Vec<Key> = vec![];
        for &key in &touched {
            let item = &self.items[&key];
            if item.placed != Some((self.slot(item), item.order)) {
                wanted.push(key);
            }
        }
        let mut again = vec![];
        for &key in &wanted {
            self.unplace(key, sink, &mut again);
        }
        wanted.extend(again);
        let mut inserted = HashSet::new();
        loop {
            let mut by_slot: HashMap<Key, Vec<(Order, Key)>> = HashMap::new();
            for &key in &wanted {
                let item = &self.items[&key];
                let slot = self.slot(item);
                if item.placed.is_none() && self.placed(slot) {
                    by_slot.entry(slot).or_default().push((item.order, key));
                }
            }
            if by_slot.is_empty() {
                break;
            }
            let mut slots: Vec<_> = by_slot.into_iter().collect();
            slots.sort_by_key(|(slot, _)| (*slot != Key::ROOT, slot.0));
            for (slot, mut new) in slots {
                new.sort_unstable();
                new.dedup();
                let mut list = self.slots.remove(&slot).unwrap_or_default();
                // Each new item's place in the merged list, then runs of
                // neighbours, each merged in as it goes into the Tree.
                let mut places = Vec::with_capacity(new.len());
                for (i, entry) in new.iter().enumerate() {
                    places.push(list.partition_point(|e| e < entry) + i);
                }
                let mut run = 0;
                while run < new.len() {
                    let mut end = run + 1;
                    while end < new.len() && places[end] == places[end - 1] + 1 {
                        end += 1;
                    }
                    let (at, n) = (places[run], end - run);
                    list.splice(at..at, new[run..end].iter().copied());
                    let lo = if at > 0 { self.group_at(&list, at - 1).start } else { 0 };
                    let hi = if at + n < list.len() { self.group_at(&list, at + n).end } else { list.len() };
                    let old: Vec<(Order, Key)> = list[lo..at].iter().chain(&list[at + n..hi]).copied().collect();
                    let first = self.group_index(slot, &list, lo, hi, &old, sink.doc);
                    self.regroup(slot, first, &old, &list[lo..hi], sink);
                    for &(order, key) in &new[run..end] {
                        self.items.get_mut(&key).unwrap().placed = Some((slot, order));
                        inserted.insert(key);
                    }
                    run = end;
                }
                self.slots.insert(slot, list);
            }
        }
        let (mut runs, mut in_runs) = (vec![], HashSet::new());
        for key in touched {
            if inserted.contains(&key) {
                continue;
            }
            let own = self.slots.get(&key).map_or(0, |list| self.groups(list).len());
            // A run's summary is staged once, after all its calls change.
            if !in_runs.contains(&key) {
                if let Some(run) = self.run_of(key).map(<[_]>::to_vec) {
                    in_runs.extend(run.iter().map(|&(_, k)| k));
                    runs.push(run);
                }
            }
            let Some(item) = self.items.get_mut(&key) else { continue };
            if item.placed.is_none() || !item.dirty {
                continue;
            }
            item.dirty = false;
            let (kind, text, detail) = item.head();
            let mut head = Staged::under(key, 0);
            head.push(0, kind, Some(key), text, vec![], detail);
            sink.update(&head, 1);
            match &mut item.body {
                Body::Text { md, .. } | Body::Reasoning { md } => md.stream(key, sink),
                Body::Tool { kind, result, streamed, shown, .. } => {
                    // Only an input that changed is staged again.
                    let was = lead(sink.doc, key);
                    let current =
                        (was == 1).then(|| sink.doc.get(input_key(key))).flatten().map(|id| (sink.doc.text(id), sink.doc.detail(id)));
                    let same = match (&shown.input, current) {
                        (None, None) => true,
                        (Some((text, language)), Some((t, Detail::Code { language: l }))) => text == t && language == l,
                        _ => false,
                    };
                    if !same {
                        let mut input = Staged::under(key, 0);
                        stage_input(key, shown, &mut input, 0);
                        sink.splice(key, 0, Some(was), input);
                    }
                    let mut staged = Staged::under(key, 0);
                    if let Some(r) = result.as_ref().or(streamed.as_deref()) {
                        stage_result(r, *kind, &shown.title, &mut staged, 0);
                    }
                    sink.splice(key, lead(sink.doc, key) + own, None, staged);
                }
                _ => {}
            }
        }
        for run in runs {
            self.update_run(&run, sink);
        }
    }
}

/// How many of two sequences' first and last elements match, by `same(i, j)`
/// on their indices, without overlapping.
fn trim(a: usize, b: usize, same: impl Fn(usize, usize) -> bool) -> (usize, usize) {
    let mut prefix = 0;
    while prefix < a && prefix < b && same(prefix, prefix) {
        prefix += 1;
    }
    let mut suffix = 0;
    while suffix < a - prefix && suffix < b - prefix && same(a - 1 - suffix, b - 1 - suffix) {
        suffix += 1;
    }
    (prefix, suffix)
}

/// What a hover action copies (#546).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CopyAction {
    /// A Turn's markdown as plain text: a Turn holds no reasoning or tool
    /// calls, which are Sections beside it.
    Message,
    /// A tool call's result, without its ANSI escapes.
    ToolOutput,
    /// A tool call's `command` argument.
    Command,
}

impl CopyAction {
    pub const ALL: [CopyAction; 3] = [CopyAction::Message, CopyAction::ToolOutput, CopyAction::Command];
}

/// A fence of backticks longer than any run in `text`.
fn fence(text: &str) -> String {
    let (mut longest, mut run) = (0, 0);
    for c in text.chars() {
        run = if c == '`' { run + 1 } else { 0 };
        longest = longest.max(run);
    }
    "`".repeat(longest.max(2) + 1)
}

fn fenced(info: &str, text: &str) -> String {
    let fence = fence(text);
    format!("{fence}{info}\n{text}\n{fence}")
}

fn output(r: &ToolResult) -> Option<String> {
    let media = r.media_type.as_str();
    if media == MARKDOWN {
        return Some(r.content.trim_end_matches('\n').to_owned());
    }
    if media == DIFF {
        let patch = r.structured.as_ref().and_then(|s| s.get("patch").or(Some(s))).and_then(Value::as_str);
        let text = if r.content.is_empty() { patch.unwrap_or("") } else { &r.content };
        return Some(text.trim_end_matches('\n').to_owned());
    }
    if media.starts_with("image/") {
        return None;
    }
    let mut log = Log::default();
    log.feed(&r.content);
    Some(log.chunks().into_iter().map(|chunk| chunk.text).collect::<Vec<_>>().join("\n"))
}

fn command(args: &str) -> Option<String> {
    let value: Value = serde_json::from_str(args).ok()?;
    value.get("command").and_then(Value::as_str).map(str::to_owned)
}

impl Conversation {
    /// What `action` copies of the item `key`; None if it doesn't apply.
    pub fn copy(&self, key: Key, action: CopyAction) -> Option<String> {
        match (&self.items.get(&key)?.body, action) {
            (Body::Text { md, .. }, CopyAction::Message) => Some(md.raw.trim_end_matches('\n').to_owned()),
            (Body::Tool { result: Some(r), .. }, CopyAction::ToolOutput) => output(r),
            (Body::Tool { args, .. }, CopyAction::Command) => command(args),
            _ => None,
        }
    }

    /// Whether [`Conversation::copy`] has something for `action`, without
    /// building it: a hover asks every frame.
    pub fn can_copy(&self, key: Key, action: CopyAction) -> bool {
        match (self.items.get(&key).map(|i| &i.body), action) {
            (Some(Body::Text { .. }), CopyAction::Message) => true,
            (Some(Body::Tool { result: Some(r), .. }), CopyAction::ToolOutput) => !r.media_type.starts_with("image/"),
            (Some(Body::Tool { args, .. }), CopyAction::Command) => command(args).is_some(),
            _ => false,
        }
    }

    /// The transcript as markdown, in the order shown: messages under their
    /// role, tool calls with their command and output, a subagent's thread
    /// after the call that started it. Reasoning is left out, as a
    /// message's copy leaves it out.
    pub fn transcript(&self) -> String {
        let mut parts = vec![];
        self.transcript_into(Key::ROOT, &mut parts);
        parts.join("\n\n")
    }

    fn transcript_into(&self, slot: Key, parts: &mut Vec<String>) {
        for &(_, key) in self.slots.get(&slot).into_iter().flatten() {
            let Some(item) = self.items.get(&key) else { continue };
            match &item.body {
                Body::Text { role, md, .. } => {
                    let label = match role {
                        Role::User => "User",
                        Role::Assistant => "Assistant",
                        Role::System => "System",
                    };
                    parts.push(format!("## {label}\n\n{}", md.raw.trim_end_matches('\n')));
                }
                Body::Reasoning { .. } => {}
                Body::Tool { name, title, args, result, .. } => {
                    let mut part = if title.is_empty() { format!("**{name}**") } else { format!("**{name}** {title}") };
                    if let Some(command) = command(args) {
                        part = format!("{part}\n\n{}", fenced("sh", &command));
                    }
                    if let Some(text) = result.as_ref().and_then(output).filter(|t| !t.is_empty()) {
                        part = format!("{part}\n\n{}", fenced("", &text));
                    }
                    parts.push(part);
                }
                Body::Subagent { .. } => parts.push(format!("**Subagent** {}", item.thread)),
                // Emphasis, not dashes: `--- x ---` pastes as a rule or a
                // setext heading.
                Body::Divider { text } => parts.push(format!("*— {} —*", text.replace('*', "\\*"))),
                Body::Notice { text } => parts.push(text.split('\n').map(|line| format!("> {line}")).collect::<Vec<_>>().join("\n")),
            }
            self.transcript_into(key, parts);
        }
    }
}
