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
use std::time::{Duration, Instant};

use conversation_events::{
    parse_line, Event, Lines, Message, Parent, Role as EventRole, ToolCallResult, ToolKind as EventToolKind, ToolStatus as EventToolStatus,
    Truncated, COMPACTION, COMPLETED, CONTRACT, DIFF, MARKDOWN,
};
use serde_json::Value;

use super::ingest::{parse_into, Staged};
use super::log::Log;
use super::ring::Ring;
use super::tail::{repair, Fence};
use super::tree::{Detail, Key, Kind, Role, SectionKind, ToolKind, ToolStatus, Tree};
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
}

#[derive(Debug)]
enum Body {
    Text { role: Role, md: Markdown },
    Reasoning { md: Markdown },
    Tool { name: String, title: String, kind: ToolKind, args: String, result: Option<ToolResult> },
    Subagent { status: ToolStatus },
    Divider { text: String },
    Notice { text: String },
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
        let section = |section, name: &str, title: &str, tool, status| Detail::Section {
            section,
            name: name.to_owned(),
            title: title.to_owned(),
            tool,
            status,
        };
        let streaming = |md: &Markdown| if md.ended { ToolStatus::Ok } else { ToolStatus::Pending };
        match &self.body {
            Body::Text { role, .. } => (Kind::Turn, "", Detail::Turn { role: *role }),
            Body::Reasoning { md } => (Kind::Section, "", section(SectionKind::Reasoning, "", "", ToolKind::Other, streaming(md))),
            Body::Tool { name, title, kind, result, .. } => {
                let status = result.as_ref().map_or(ToolStatus::Pending, |r| r.status);
                (Kind::Section, "", section(SectionKind::Tool, name, title, *kind, status))
            }
            Body::Subagent { status } => (Kind::Section, "", section(SectionKind::Subagent, "", &self.thread, ToolKind::Agent, *status)),
            Body::Divider { text } => (Kind::Divider, text, Detail::None),
            Body::Notice { text } => (Kind::Notice, text, Detail::None),
        }
    }
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
        Some(EventToolStatus::Interrupted) => ToolStatus::Interrupted,
        Some(EventToolStatus::AwaitingApproval) => ToolStatus::AwaitingApproval,
        _ => ToolStatus::Ok,
    }
}

fn result(e: &ToolCallResult) -> ToolResult {
    ToolResult {
        status: tool_status(e.status),
        media_type: e.media_type.clone().unwrap_or_default(),
        content: e.content.clone(),
        structured: e.structured.clone(),
        truncated: e.truncated.clone(),
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

/// The result's Blocks under `parent`, by media type.
fn stage_result(r: &ToolResult, staged: &mut Staged, parent: u32) {
    let media = r.media_type.as_str();
    if media == MARKDOWN {
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
    items: HashMap<Key, Item>,
    threads: HashMap<String, Thread>,
    /// Each slot's placed items, in order.
    slots: HashMap<Key, Vec<(Order, Key)>>,
    /// Events for items whose first line backfill hasn't reached yet.
    orphans: HashMap<Key, Vec<(u64, Event)>>,
    /// The subagent threads each tool call started, by the call's key.
    subagents: HashMap<Key, Vec<String>>,
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
                if hello.contract != CONTRACT {
                    self.unsupported = true;
                    let text = format!("Conversation contract {} isn't supported (this reads {CONTRACT})", hello.contract);
                    self.create(Key::named(&["", "hello"]), order, "", || Body::Notice { text });
                }
            }
            Event::RunStarted(e) => {
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
                if let Some(outcome) = e.outcome.as_deref().filter(|&o| o != COMPLETED) {
                    let text = outcome.to_owned();
                    self.create(seq_key(&thread, e.seq, at), order, &thread, || Body::Divider { text });
                }
                let status = if e.outcome.as_deref().is_none_or(|o| o == COMPLETED) { ToolStatus::Ok } else { ToolStatus::Interrupted };
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
                    self.create(message_key(&thread, &e.message_id), order, &thread, || Body::Text { role: r, md: Markdown::default() })
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
                self.tool(at, order, &thread, &e.tool_call_id, n, t, k, None);
            }
            Event::ToolCallArgs(e) => {
                if let Some(Body::Tool { args, .. }) = self.items.get_mut(&tool_key(&thread, &e.tool_call_id)).map(|i| &mut i.body) {
                    args.push_str(&e.delta);
                }
            }
            Event::ToolCallResult(e) => {
                if let Some(Body::Tool { result: r, .. }) = self.items.get_mut(&tool_key(&thread, &e.tool_call_id)).map(|i| &mut i.body) {
                    *r = Some(result(&e));
                }
            }
            Event::MessagesSnapshot(e) => self.snapshot(at, &thread, e.messages),
            Event::Custom(e) if e.name == COMPACTION => {
                self.create(seq_key(&thread, e.seq, at), order, &thread, || Body::Divider { text: COMPACTION.into() });
            }
            _ => {}
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

    #[allow(clippy::too_many_arguments)]
    fn tool(&mut self, at: u64, order: Order, thread: &str, id: &str, name: String, title: String, kind: ToolKind, snapshot: Option<&str>) {
        let key = tool_key(thread, id);
        let fresh = Body::Tool { name: name.clone(), title: title.clone(), kind, args: String::new(), result: None };
        if let Some(item) = self.create(key, order, thread, || fresh) {
            if let Body::Tool { name: n, title: t, kind: k, args, .. } = &mut item.body {
                (*n, *t, *k) = (name, title, kind);
                if let Some(arguments) = snapshot {
                    *args = arguments.to_owned();
                    item.snapshot = Some(at);
                }
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
                        let body = Body::Text { role: role(r), md };
                        if let Some(item) = self.create(message_key(thread, &m.id), order, thread, || Body::Notice { text: String::new() })
                        {
                            item.body = body;
                            item.snapshot = Some(at);
                        }
                    }
                    for call in m.tool_calls {
                        let title = call.title.unwrap_or_default();
                        let order = next();
                        self.tool(
                            at,
                            order,
                            thread,
                            &call.id,
                            call.function.name,
                            title,
                            tool_kind(call.kind),
                            Some(&call.function.arguments),
                        );
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

    /// An item with its own Blocks, under the staged root.
    fn stage(&mut self, key: Key, staged: &mut Staged, doc: &mut Tree) {
        let item = self.items.get_mut(&key).expect("a staged item exists");
        let (kind, text, detail) = item.head();
        let node = staged.push(0, kind, Some(key), text, vec![], detail);
        match &mut item.body {
            Body::Text { md, .. } | Body::Reasoning { md } => md.stage(staged, node, doc),
            Body::Tool { result: Some(r), .. } => stage_result(r, staged, node),
            _ => {}
        }
        item.dirty = false;
    }

    /// Takes an item out of its slot, and every item placed inside it.
    fn unplace(&mut self, key: Key, sink: &mut Sink, again: &mut Vec<Key>) {
        let Some((slot, order)) = self.items.get_mut(&key).and_then(|i| i.placed.take()) else { return };
        let list = self.slots.get_mut(&slot).expect("a placed item's slot");
        let at = list.binary_search(&(order, key)).expect("a placed item is in its slot");
        list.remove(at);
        sink.splice(slot, at, Some(at + 1), Staged::under(slot, 0));
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
        let touched = std::mem::take(&mut self.touched);
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
                let list = self.slots.entry(slot).or_default();
                // Each new item's place in the merged list, then runs of
                // neighbours.
                let mut places = Vec::with_capacity(new.len());
                for (i, entry) in new.iter().enumerate() {
                    places.push(list.partition_point(|e| e < entry) + i);
                }
                let merged: Vec<(Order, Key)> = {
                    let mut merged = Vec::with_capacity(list.len() + new.len());
                    let (mut a, mut b) = (0, 0);
                    while a < list.len() || b < new.len() {
                        if b < new.len() && (a == list.len() || new[b] < list[a]) {
                            merged.push(new[b]);
                            b += 1;
                        } else {
                            merged.push(list[a]);
                            a += 1;
                        }
                    }
                    merged
                };
                *list = merged;
                let mut run = 0;
                while run < new.len() {
                    let mut end = run + 1;
                    while end < new.len() && places[end] == places[end - 1] + 1 {
                        end += 1;
                    }
                    let mut staged = Staged::under(slot, 0);
                    for &(order, key) in &new[run..end] {
                        self.stage(key, &mut staged, sink.doc);
                        self.items.get_mut(&key).unwrap().placed = Some((slot, order));
                        inserted.insert(key);
                    }
                    sink.splice(slot, places[run], Some(places[run]), staged);
                    run = end;
                }
            }
        }
        for key in touched {
            if inserted.contains(&key) {
                continue;
            }
            let own = self.slots.get(&key).map_or(0, Vec::len);
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
                Body::Tool { result, .. } => {
                    let mut staged = Staged::under(key, 0);
                    if let Some(r) = result {
                        stage_result(r, &mut staged, 0);
                    }
                    sink.splice(key, own, None, staged);
                }
                _ => {}
            }
        }
    }
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
                Body::Text { role, md } => {
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
                Body::Divider { text } => parts.push(format!("--- {text} ---")),
                Body::Notice { text } => parts.push(format!("> {text}")),
            }
            self.transcript_into(key, parts);
        }
    }
}
