//! The tree in index-based arrays (#538).
use std::collections::hash_map::DefaultHasher;
use std::collections::{HashMap, HashSet};
use std::hash::{Hash, Hasher};

use wheelhouse_stream::{list_ops, ListOp};

use super::ingest::{SourceInfo, Staged};
use super::pages::{Pages, Source, TextRange, PAGE};
use super::ring::{Ring, What};
use super::view::Was;

/// A markdown node's key is its parent's key and its ordinal among its
/// siblings.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Default)]
pub struct Key(pub u64);

impl Key {
    pub const ROOT: Key = Key(0x5249_4348_5445_5854);

    pub fn child(self, ordinal: u32) -> Key {
        Key(mix(self.0 ^ mix(u64::from(ordinal) + 1)))
    }

    /// A key from a source's natural ids, such as a thread and a message:
    /// the same parts give the same key in every build.
    pub fn named(parts: &[&str]) -> Key {
        // FNV-1a, with a separator so parts can't run together.
        let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
        for part in parts {
            for &b in part.as_bytes().iter().chain(&[0xff]) {
                hash = (hash ^ u64::from(b)).wrapping_mul(0x0100_0000_01b3);
            }
        }
        Key(mix(hash))
    }

    pub(crate) fn rehash(self) -> Key {
        Key(mix(self.0.wrapping_add(0x9e37_79b9_7f4a_7c15)))
    }
}

fn mix(mut z: u64) -> u64 {
    // splitmix64's finaliser: stable across builds and platforms.
    z = z.wrapping_add(0x9e37_79b9_7f4a_7c15);
    z = (z ^ (z >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
    z ^ (z >> 31)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct NodeId(pub u32);

pub(crate) const NIL: u32 = u32::MAX;

/// What a node is. Containers are below 16, Blocks from 16.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Kind {
    Root = 0,
    List = 1,
    ListItem = 2,
    Quote = 3,
    /// A footnote's definition, which holds Blocks like a Quote.
    Footnote = 4,
    /// A conversation's message, with its role.
    Turn = 5,
    /// A tool call, reasoning, or a subagent's thread.
    Section = 6,
    Paragraph = 16,
    Heading = 17,
    Code = 18,
    Rule = 19,
    Embed = 20,
    /// Lines of tool output, with literal colours.
    Log = 21,
    /// A break in a conversation; its text says why.
    Divider = 22,
    /// A note standing in for content, such as output that was cut.
    Notice = 23,
}

impl Kind {
    pub fn is_block(self) -> bool {
        self as u8 >= 16
    }

    pub fn from_u8(value: u8) -> Option<Kind> {
        Some(match value {
            0 => Kind::Root,
            1 => Kind::List,
            2 => Kind::ListItem,
            3 => Kind::Quote,
            4 => Kind::Footnote,
            5 => Kind::Turn,
            6 => Kind::Section,
            16 => Kind::Paragraph,
            17 => Kind::Heading,
            18 => Kind::Code,
            19 => Kind::Rule,
            20 => Kind::Embed,
            21 => Kind::Log,
            22 => Kind::Divider,
            23 => Kind::Notice,
            _ => return None,
        })
    }
}

/// An index entry: a Block, or a Container's header or footer row.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(u8)]
pub enum Part {
    Block = 0,
    Header = 1,
    Footer = 2,
}

/// The theme owns the palette.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Color {
    Palette(u8),
    Rgb(u8, u8, u8),
    /// The theme's default foreground or background, which inverse video
    /// swaps in.
    Foreground,
    Background,
}

/// Strings and inline objects a span names are in its Block's
/// [`NodeContent::strings`] and [`NodeContent::objects`].
pub const NO_STRING: u32 = u32::MAX;

/// Semantic attributes are styled by each frontend's theme; colours are
/// literal.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Attr {
    Emphasis,
    Strong,
    Code,
    Strikethrough,
    /// Inline math, drawn as code until a math Embed exists.
    Math,
    /// Display math: a paragraph's `$$…$$`.
    DisplayMath,
    /// `reference` is the label of the reference definition it used, or
    /// NO_STRING.
    Link {
        target: u32,
        reference: u32,
    },
    /// A footnote reference; its text is `[^label]`.
    FootnoteReference {
        label: u32,
    },
    Color(Color),
    Background(Color),
    Underline,
    /// U+FFFC standing for an inline object.
    InlineObject {
        object: u32,
    },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Span {
    pub start: u32,
    pub len: u32,
    pub attr: Attr,
}

impl Span {
    pub fn end(&self) -> u32 {
        self.start + self.len
    }
}

/// An object laid out inline, as a box: its text is U+FFFC. Modelled, but
/// nothing makes one yet.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct InlineObject {
    pub media_type: String,
    pub source: String,
    pub alt: String,
    /// Size hints in the frontend's units, or 0.
    pub width: u32,
    pub height: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Alert {
    Note,
    Tip,
    Important,
    Warning,
    Caution,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Align {
    None,
    Left,
    Center,
    Right,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Cell {
    pub start: u32,
    pub len: u32,
}

/// Its Embed's text is the cells as TSV; cells' inline styles are the
/// Embed's spans.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Default)]
pub struct Table {
    pub alignments: Vec<Align>,
    /// The header row first.
    pub rows: Vec<Vec<Cell>>,
}

pub const TABLE_MEDIA_TYPE: &str = "application/vnd.flotilla.table";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Role {
    User,
    Assistant,
    System,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SectionKind {
    Tool,
    Reasoning,
    /// A subagent's thread, inside the tool call that started it.
    Subagent,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ToolKind {
    Shell,
    Edit,
    Read,
    Search,
    Agent,
    Web,
    Mcp,
    Other,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ToolStatus {
    /// Started, with no result yet.
    Pending,
    Ok,
    Failed,
    Interrupted,
    AwaitingApproval,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Default)]
pub enum Detail {
    #[default]
    None,
    List {
        ordered: bool,
        start: u64,
        tight: bool,
    },
    Item {
        task: Option<bool>,
    },
    Quote {
        alert: Option<Alert>,
    },
    Footnote {
        label: String,
    },
    Code {
        /// The info string's first word; empty if none. Front matter is a
        /// hidden Code Block whose language is "yaml" or "toml".
        language: String,
    },
    Embed {
        media_type: String,
        table: Option<Table>,
    },
    Turn {
        role: Role,
    },
    /// What a frontend draws a Section's header from. A subagent's title is
    /// its thread's id.
    Section {
        section: SectionKind,
        name: String,
        title: String,
        tool: ToolKind,
        status: ToolStatus,
    },
}

/// A node's own content, without its children.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NodeContent {
    pub key: Key,
    pub kind: Kind,
    pub version: u64,
    pub level: u8,
    /// Front matter: kept, but not shown.
    pub hidden: bool,
    pub text: String,
    pub spans: Vec<Span>,
    pub strings: Vec<String>,
    pub objects: Vec<InlineObject>,
    pub detail: Detail,
}

#[derive(Debug, Clone, Copy)]
pub(crate) struct Node {
    pub parent: u32,
    pub first_child: u32,
    pub last_child: u32,
    pub next_sibling: u32,
    /// How many children it has.
    pub count: u32,
    pub kind: Kind,
    pub hidden: bool,
    pub level: u8,
    pub live: bool,
    pub key: Key,
    pub version: u64,
    /// The content hash a reload matches on.
    pub hash: u64,
    pub text: TextRange,
    /// The node's first index entry (its header, or its Block row), less the
    /// tree's `base`; meaningless unless `row`.
    pub entry: u32,
    /// It has an index entry: the root and a hidden Block don't.
    pub row: bool,
    /// How many index entries it and its descendants have.
    pub entries: u32,
}

impl Node {
    fn empty() -> Node {
        Node {
            parent: NIL,
            first_child: NIL,
            last_child: NIL,
            next_sibling: NIL,
            count: 0,
            kind: Kind::Paragraph,
            hidden: false,
            level: 0,
            live: false,
            key: Key(0),
            version: 0,
            hash: 0,
            text: TextRange::default(),
            entry: NIL,
            row: false,
            entries: 0,
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Hash)]
pub(crate) struct Side {
    pub detail: Detail,
    pub strings: Vec<String>,
    pub objects: Vec<InlineObject>,
}

impl Side {
    fn is_empty(&self) -> bool {
        self.detail == Detail::None && self.strings.is_empty() && self.objects.is_empty()
    }
}

/// What a splice did to the entries, for each View to follow.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct EntryEdit {
    pub at: usize,
    pub removed: usize,
    pub inserted: usize,
    /// Entries kept, as they are numbered after the edit, whose content
    /// changed.
    pub stale: Vec<usize>,
    /// The removed entries, for a selection to clamp against.
    pub was: Vec<Was>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Entry {
    pub(crate) node: NodeId,
    pub part: Part,
}

static NO_SIDE: Side = Side { detail: Detail::None, strings: Vec::new(), objects: Vec::new() };

#[derive(Debug, Default)]
pub struct Tree {
    pub(crate) nodes: Vec<Node>,
    free: Vec<u32>,
    pub(crate) text: Pages,
    pub(crate) spans: Vec<Vec<Span>>,
    side: HashMap<u32, Side>,
    pub(crate) sources: Vec<SourceInfo>,
    keys: HashMap<Key, u32>,
    pub(crate) source: Source,
    pub(crate) entries: Vec<Entry>,
    next_version: u64,
    live_text: usize,
    /// Streaming tails' text, one string per message, rewritten on each
    /// re-parse (#540, decision 2).
    scratch: Vec<String>,
    free_scratch: Vec<u32>,
    /// Added to every node's `entry`: a splice shifts whichever side of it
    /// is shorter, so an insert at the front costs what it inserts.
    base: u32,
}

/// A text range whose page has this bit is in a scratch string.
const SCRATCH: u32 = 1 << 31;

fn in_pages(range: TextRange) -> usize {
    if range.page & SCRATCH == 0 {
        range.len as usize
    } else {
        0
    }
}

impl Tree {
    pub fn root(&self) -> Option<NodeId> {
        self.get(Key::ROOT)
    }

    pub fn get(&self, key: Key) -> Option<NodeId> {
        self.keys.get(&key).map(|&id| NodeId(id))
    }

    pub(crate) fn node(&self, id: NodeId) -> &Node {
        &self.nodes[id.0 as usize]
    }

    pub fn key(&self, id: NodeId) -> Key {
        self.node(id).key
    }

    pub fn kind(&self, id: NodeId) -> Kind {
        self.node(id).kind
    }

    pub fn version(&self, id: NodeId) -> u64 {
        self.node(id).version
    }

    pub fn level(&self, id: NodeId) -> u8 {
        self.node(id).level
    }

    pub fn hidden(&self, id: NodeId) -> bool {
        self.node(id).hidden
    }

    pub fn parent(&self, id: NodeId) -> Option<NodeId> {
        let parent = self.node(id).parent;
        (parent != NIL).then_some(NodeId(parent))
    }

    pub fn text(&self, id: NodeId) -> &str {
        let range = self.node(id).text;
        if range.page & SCRATCH == 0 {
            self.text.get(range)
        } else {
            &self.scratch[(range.page & !SCRATCH) as usize][range.start as usize..(range.start + range.len) as usize]
        }
    }

    /// A scratch string for a streaming message's tail.
    pub(crate) fn new_scratch(&mut self) -> u32 {
        self.free_scratch.pop().unwrap_or_else(|| {
            self.scratch.push(String::new());
            (self.scratch.len() - 1) as u32
        })
    }

    /// No node may use it.
    pub(crate) fn release_scratch(&mut self, slot: u32) {
        self.scratch[slot as usize] = String::new();
        self.free_scratch.push(slot);
    }

    fn write(&mut self, text: &str, scratch: Option<u32>) -> TextRange {
        let Some(slot) = scratch else { return self.text.push(text) };
        let string = &mut self.scratch[slot as usize];
        let start = string.len() as u32;
        string.push_str(text);
        TextRange { page: slot | SCRATCH, start, len: text.len() as u32 }
    }

    pub fn spans(&self, id: NodeId) -> &[Span] {
        &self.spans[id.0 as usize]
    }

    fn side(&self, id: NodeId) -> &Side {
        self.side.get(&id.0).unwrap_or(&NO_SIDE)
    }

    pub fn detail(&self, id: NodeId) -> &Detail {
        &self.side(id).detail
    }

    pub fn strings(&self, id: NodeId) -> &[String] {
        &self.side(id).strings
    }

    pub fn objects(&self, id: NodeId) -> &[InlineObject] {
        &self.side(id).objects
    }

    pub fn children(&self, id: NodeId) -> Children<'_> {
        Children { doc: self, next: self.node(id).first_child }
    }

    pub fn source(&self) -> &Source {
        &self.source
    }

    pub(crate) fn source_info(&self, id: NodeId) -> &SourceInfo {
        &self.sources[id.0 as usize]
    }

    pub fn content(&self, id: NodeId) -> NodeContent {
        let node = self.node(id);
        let side = self.side(id);
        NodeContent {
            key: node.key,
            kind: node.kind,
            version: node.version,
            level: node.level,
            hidden: node.hidden,
            text: self.text(id).to_owned(),
            spans: self.spans(id).to_vec(),
            strings: side.strings.clone(),
            objects: side.objects.clone(),
            detail: side.detail.clone(),
        }
    }

    pub fn preorder(&self) -> Vec<NodeId> {
        let mut out = Vec::with_capacity(self.keys.len());
        let Some(root) = self.root() else { return out };
        let mut stack = vec![root];
        while let Some(id) = stack.pop() {
            out.push(id);
            let at = stack.len();
            stack.extend(self.children(id));
            stack[at..].reverse();
        }
        out
    }

    pub fn len(&self) -> usize {
        self.keys.len()
    }

    pub fn is_empty(&self) -> bool {
        self.keys.is_empty()
    }

    pub fn entries(&self) -> &[Entry] {
        &self.entries
    }

    pub fn entry_key(&self, entry: usize) -> Option<(Key, Part)> {
        self.entries.get(entry).map(|e| (self.key(e.node), e.part))
    }

    /// The entry of a node's row, or None for the root, a hidden Block, or a
    /// Block asked for a header.
    pub fn entry_of(&self, key: Key, part: Part) -> Option<usize> {
        let id = self.get(key)?;
        let row = self.row(id)?;
        match (self.kind(id).is_block(), part) {
            (true, Part::Block) | (false, Part::Header) => Some(row),
            (false, Part::Footer) => Some(row + self.node(id).entries as usize - 1),
            _ => None,
        }
    }

    /// The node's first entry.
    pub(crate) fn row(&self, id: NodeId) -> Option<usize> {
        let node = self.node(id);
        node.row.then(|| node.entry.wrapping_add(self.base) as usize)
    }

    fn set_row(&mut self, id: u32, row: Option<usize>) {
        let base = self.base;
        let node = &mut self.nodes[id as usize];
        node.row = row.is_some();
        node.entry = row.map_or(NIL, |row| (row as u32).wrapping_sub(base));
    }

    /// The Blocks' first entry and one past their last, for a node.
    pub(crate) fn entry_range(&self, id: NodeId) -> (usize, usize) {
        let node = self.node(id);
        if node.parent == NIL {
            return (0, self.entries.len());
        }
        match self.row(id) {
            Some(row) => (row, row + node.entries as usize),
            None => (0, 0),
        }
    }

    /// Whether `ancestor` is `id` or one of its ancestors.
    pub fn contains(&self, ancestor: NodeId, mut id: NodeId) -> bool {
        loop {
            if id == ancestor {
                return true;
            }
            match self.parent(id) {
                Some(parent) => id = parent,
                None => return false,
            }
        }
    }

    fn alloc(&mut self) -> u32 {
        if let Some(id) = self.free.pop() {
            self.nodes[id as usize] = Node::empty();
            return id;
        }
        self.nodes.push(Node::empty());
        self.spans.push(Vec::new());
        self.sources.push(SourceInfo::default());
        (self.nodes.len() - 1) as u32
    }

    fn release(&mut self, id: u32) {
        let node = &mut self.nodes[id as usize];
        self.live_text -= in_pages(node.text);
        node.live = false;
        node.first_child = NIL;
        node.last_child = NIL;
        node.next_sibling = NIL;
        node.count = 0;
        node.parent = NIL;
        self.spans[id as usize] = Vec::new();
        self.sources[id as usize] = SourceInfo::default();
        self.side.remove(&id);
        self.free.push(id);
    }

    fn set_content(&mut self, id: u32, staged: &Staged, s: usize) {
        let node = &staged.nodes[s];
        let text = self.write(staged.text(s), node.scratch);
        let record = &mut self.nodes[id as usize];
        self.live_text -= in_pages(record.text);
        self.live_text += in_pages(text);
        record.kind = node.kind;
        record.level = node.level;
        record.hidden = node.hidden;
        record.hash = node.hash;
        record.key = node.key;
        record.text = text;
        record.live = true;
        self.next_version += 1;
        record.version = self.next_version;
        self.spans[id as usize] = node.spans.clone();
        if node.side.is_empty() {
            self.side.remove(&id);
        } else {
            self.side.insert(id, node.side.clone());
        }
    }

    /// Keeps each node whose key and content hash match. Returns whether
    /// anything changed.
    pub(crate) fn reconcile(&mut self, staged: Staged, source: &str, ring: &mut Ring, generation: u64) -> bool {
        let mut changed = false;
        let mut ids = Vec::with_capacity(staged.nodes.len());
        let mut seen = vec![false; self.nodes.len()];
        // Content first, leaving the old links in place.
        for (s, node) in staged.nodes.iter().enumerate() {
            let id = match self.keys.get(&node.key) {
                Some(&id) => {
                    seen[id as usize] = true;
                    let old = &self.nodes[id as usize];
                    if old.hash != node.hash || old.kind != node.kind {
                        self.set_content(id, &staged, s);
                        ring.push(generation, NodeId(id), node.key, What::Content);
                        changed = true;
                    }
                    id
                }
                None => {
                    let id = self.alloc();
                    if let Some(seen) = seen.get_mut(id as usize) {
                        // A freed slot, reused.
                        *seen = true;
                    }
                    self.set_content(id, &staged, s);
                    self.keys.insert(node.key, id);
                    ring.push(generation, NodeId(id), node.key, What::Created);
                    changed = true;
                    id
                }
            };
            ids.push(id);
        }
        // Child lists that changed, from the old links.
        for (s, node) in staged.nodes.iter().enumerate() {
            let id = ids[s];
            let old: Vec<Key> = self.children(NodeId(id)).map(|c| self.key(c)).collect();
            let new: Vec<Key> = node.children.iter().map(|&c| staged.nodes[c as usize].key).collect();
            if old != new {
                ring.push(generation, NodeId(id), node.key, What::Children(list_ops(&old, &new).into()));
                changed = true;
            }
        }
        let gone: Vec<u32> = (0..seen.len() as u32).filter(|&id| self.nodes[id as usize].live && !seen[id as usize]).collect();
        for &id in &gone {
            let key = self.nodes[id as usize].key;
            ring.push(generation, NodeId(id), key, What::Removed);
            self.keys.remove(&key);
            self.release(id);
            changed = true;
        }
        // The new links, and the source as now parsed.
        for (s, node) in staged.nodes.iter().enumerate() {
            let id = ids[s];
            let record = &mut self.nodes[id as usize];
            record.parent = if node.parent == NIL { NIL } else { ids[node.parent as usize] };
            record.first_child = node.children.first().map_or(NIL, |&c| ids[c as usize]);
            record.last_child = node.children.last().map_or(NIL, |&c| ids[c as usize]);
            record.count = node.children.len() as u32;
            for pair in node.children.windows(2) {
                self.nodes[ids[pair[0] as usize] as usize].next_sibling = ids[pair[1] as usize];
            }
            if let Some(&last) = node.children.last() {
                self.nodes[ids[last as usize] as usize].next_sibling = NIL;
            }
        }
        if let Some(&root) = ids.first() {
            self.nodes[root as usize].next_sibling = NIL;
        }
        for (s, node) in staged.nodes.into_iter().enumerate() {
            self.sources[ids[s] as usize] = node.source;
        }
        // Offsets into the source move even when no node changed.
        self.source = Source::new(source);
        if changed {
            self.flatten();
        }
        if self.text.written() > 2 * self.live_text + PAGE {
            self.compact();
        }
        changed
    }

    /// The child `at` of `parent` and the one before it, or NIL.
    fn around(&self, parent: u32, at: usize) -> (u32, u32) {
        let node = &self.nodes[parent as usize];
        if at == 0 {
            return (NIL, node.first_child);
        }
        if at == node.count as usize {
            return (node.last_child, NIL);
        }
        let prev = self.children(NodeId(parent)).nth(at - 1).expect("in range").0;
        (prev, self.nodes[prev as usize].next_sibling)
    }

    /// Where the entries of `parent`'s child `at` start.
    fn entries_before(&self, parent: u32, at: usize, prev: u32) -> usize {
        if prev != NIL {
            if let Some(row) = self.row(NodeId(prev)) {
                return row + self.nodes[prev as usize].entries as usize;
            }
        }
        let base = self.row(NodeId(parent)).map_or(0, |row| row + 1);
        base + self.children(NodeId(parent)).take(at).map(|c| self.nodes[c.0 as usize].entries as usize).sum::<usize>()
    }

    /// Replaces `parent`'s children `from..to` with the staged root's
    /// children, keeping each node whose key is in the range and whose
    /// content hash matches. Only the entries in the range are rebuilt, so a
    /// splice costs what it changes, plus a shift of the entries on its
    /// shorter side.
    pub(crate) fn splice(
        &mut self,
        parent: NodeId,
        from: usize,
        to: usize,
        staged: Staged,
        ring: &mut Ring,
        generation: u64,
    ) -> Option<EntryEdit> {
        let p = parent.0;
        assert!(from <= to && to <= self.nodes[p as usize].count as usize, "a splice's range is in its parent's children");
        let (prev, mut next) = self.around(p, from);
        let mut old_children = Vec::with_capacity(to - from);
        for _ in from..to {
            old_children.push(next);
            next = self.nodes[next as usize].next_sibling;
        }
        let mut old = HashSet::new();
        let mut stack = old_children.clone();
        while let Some(id) = stack.pop() {
            old.insert(id);
            stack.extend(self.children(NodeId(id)).map(|c| c.0));
        }
        let a = self.entries_before(p, from, prev);
        let old_len: usize = old_children.iter().map(|&c| self.nodes[c as usize].entries as usize).sum();
        let was: Vec<(u32, Was)> = self.entries[a..a + old_len]
            .iter()
            .map(|e| (e.node.0, Was { key: self.key(e.node), part: e.part, version: self.version(e.node) }))
            .collect();

        // A message's tail is all in one splice, so its scratch starts over.
        for node in &staged.nodes {
            if let Some(slot) = node.scratch {
                self.scratch[slot as usize].clear();
            }
        }
        let mut changed = false;
        let mut ids = Vec::with_capacity(staged.nodes.len());
        ids.push(p);
        let mut kept = HashSet::new();
        for (s, node) in staged.nodes.iter().enumerate().skip(1) {
            let id = match self.keys.get(&node.key) {
                Some(&id) => {
                    assert!(old.contains(&id), "{:?} is already elsewhere in the tree", node.key);
                    kept.insert(id);
                    let record = &self.nodes[id as usize];
                    if record.hash != node.hash || record.kind != node.kind {
                        self.set_content(id, &staged, s);
                        ring.push(generation, NodeId(id), node.key, What::Content);
                        changed = true;
                    } else if node.scratch.is_some() || record.text.page & SCRATCH != 0 {
                        // Unchanged, but rewritten into the fresh scratch, or
                        // frozen into the pages.
                        let text = self.write(staged.text(s), node.scratch);
                        self.live_text = self.live_text - in_pages(self.nodes[id as usize].text) + in_pages(text);
                        self.nodes[id as usize].text = text;
                    }
                    id
                }
                None => {
                    let id = self.alloc();
                    self.set_content(id, &staged, s);
                    self.keys.insert(node.key, id);
                    ring.push(generation, NodeId(id), node.key, What::Created);
                    changed = true;
                    id
                }
            };
            ids.push(id);
        }
        let shift = |ops: Vec<ListOp<Key>>| -> Box<[ListOp<Key>]> {
            ops.into_iter()
                .map(|op| match op {
                    ListOp::Insert { at, key } => ListOp::Insert { at: at + from, key },
                    ListOp::Remove { at } => ListOp::Remove { at: at + from },
                    ListOp::Move { from: f, to: t } => ListOp::Move { from: f + from, to: t + from },
                })
                .collect()
        };
        let before: Vec<Key> = old_children.iter().map(|&c| self.nodes[c as usize].key).collect();
        let after: Vec<Key> = staged.roots().iter().map(|&c| staged.nodes[c as usize].key).collect();
        if before != after {
            ring.push(generation, parent, self.nodes[p as usize].key, What::Children(shift(list_ops(&before, &after))));
            changed = true;
        }
        for (s, node) in staged.nodes.iter().enumerate().skip(1) {
            let id = ids[s];
            let before: Vec<Key> = if kept.contains(&id) { self.children(NodeId(id)).map(|c| self.key(c)).collect() } else { vec![] };
            let after: Vec<Key> = node.children.iter().map(|&c| staged.nodes[c as usize].key).collect();
            if before != after {
                ring.push(generation, NodeId(id), node.key, What::Children(list_ops(&before, &after).into()));
                changed = true;
            }
        }
        let mut gone: Vec<u32> = old.iter().copied().filter(|id| !kept.contains(id)).collect();
        gone.sort_unstable();
        for &id in &gone {
            let key = self.nodes[id as usize].key;
            ring.push(generation, NodeId(id), key, What::Removed);
            self.keys.remove(&key);
            self.release(id);
            changed = true;
        }
        if !changed {
            return None;
        }

        for (s, node) in staged.nodes.iter().enumerate().skip(1) {
            let record = &mut self.nodes[ids[s] as usize];
            record.parent = ids[node.parent as usize];
            record.first_child = node.children.first().map_or(NIL, |&c| ids[c as usize]);
            record.last_child = node.children.last().map_or(NIL, |&c| ids[c as usize]);
            record.count = node.children.len() as u32;
            for pair in node.children.windows(2) {
                self.nodes[ids[pair[0] as usize] as usize].next_sibling = ids[pair[1] as usize];
            }
            if let Some(&last) = node.children.last() {
                self.nodes[ids[last as usize] as usize].next_sibling = NIL;
            }
        }
        let roots: Vec<u32> = staged.roots().iter().map(|&r| ids[r as usize]).collect();
        for pair in roots.windows(2) {
            self.nodes[pair[0] as usize].next_sibling = pair[1];
        }
        let (head, tail) = match (roots.first(), roots.last()) {
            (Some(&first), Some(&last)) => {
                self.nodes[last as usize].next_sibling = next;
                (first, last)
            }
            _ => (next, prev),
        };
        if prev == NIL {
            self.nodes[p as usize].first_child = head;
        } else {
            self.nodes[prev as usize].next_sibling = head;
        }
        if next == NIL {
            self.nodes[p as usize].last_child = tail;
        }
        let record = &mut self.nodes[p as usize];
        record.count = record.count + roots.len() as u32 - (to - from) as u32;
        for (s, node) in staged.nodes.into_iter().enumerate().skip(1) {
            self.sources[ids[s] as usize] = node.source;
        }

        let mut entries = vec![];
        for &root in &roots {
            self.flatten_into(root, a, &mut entries);
        }
        let new_len = entries.len();
        let same = |w: &(u32, Was), e: &Entry| w.0 == e.node.0 && w.1.part == e.part;
        let mut prefix = 0;
        while prefix < old_len && prefix < new_len && same(&was[prefix], &entries[prefix]) {
            prefix += 1;
        }
        let mut suffix = 0;
        while suffix < old_len - prefix && suffix < new_len - prefix && same(&was[old_len - 1 - suffix], &entries[new_len - 1 - suffix]) {
            suffix += 1;
        }
        let mut stale = vec![];
        for i in (0..prefix).chain(new_len - suffix..new_len) {
            let w = &was[if i < prefix { i } else { old_len - (new_len - i) }];
            if w.1.version != self.nodes[entries[i].node.0 as usize].version {
                stale.push(a + i);
            }
        }
        self.entries.splice(a..a + old_len, entries);
        let delta = new_len as i64 - old_len as i64;
        if delta != 0 {
            let after = a + new_len;
            // Shift the entries after the splice, or move the base and shift
            // back the ones before it and the new ones, whichever is fewer.
            let (range, by) = if after <= self.entries.len() - after {
                self.base = self.base.wrapping_add(delta as u32);
                (0..after, (-delta) as u32)
            } else {
                (after..self.entries.len(), delta as u32)
            };
            for e in &self.entries[range] {
                if e.part != Part::Footer {
                    let node = &mut self.nodes[e.node.0 as usize];
                    node.entry = node.entry.wrapping_add(by);
                }
            }
            let mut at = p;
            while at != NIL {
                let node = &mut self.nodes[at as usize];
                node.entries = (i64::from(node.entries) + delta) as u32;
                at = node.parent;
            }
        }
        if self.text.written() > 2 * self.live_text + PAGE {
            self.compact();
        }
        Some(EntryEdit {
            at: a + prefix,
            removed: old_len - prefix - suffix,
            inserted: new_len - prefix - suffix,
            stale,
            was: was[prefix..old_len - suffix].iter().map(|w| w.1).collect(),
        })
    }

    /// A node's own content from `staged`, its children untouched.
    pub(crate) fn update(&mut self, id: NodeId, staged: &Staged, s: usize, ring: &mut Ring, generation: u64) -> Option<EntryEdit> {
        let (record, node) = (&self.nodes[id.0 as usize], &staged.nodes[s]);
        assert_eq!(record.key, node.key, "an update keeps the node's key");
        if record.hash == node.hash && record.kind == node.kind {
            return None;
        }
        if let Some(slot) = node.scratch {
            self.scratch[slot as usize].clear();
        }
        self.set_content(id.0, staged, s);
        ring.push(generation, id, node.key, What::Content);
        let stale = self.row(id).into_iter().collect();
        Some(EntryEdit { at: 0, removed: 0, inserted: 0, stale, was: vec![] })
    }

    /// A subtree's entries, the first at `base + out.len()`.
    fn flatten_into(&mut self, root: u32, base: usize, out: &mut Vec<Entry>) {
        let mut stack: Vec<(u32, bool)> = vec![(root, false)];
        while let Some((id, exiting)) = stack.pop() {
            let node = self.nodes[id as usize];
            if exiting {
                out.push(Entry { node: NodeId(id), part: Part::Footer });
                self.nodes[id as usize].entries = (base + out.len() - self.row(NodeId(id)).expect("a Container's row")) as u32;
                continue;
            }
            if node.kind.is_block() {
                if node.hidden {
                    self.set_row(id, None);
                    self.nodes[id as usize].entries = 0;
                } else {
                    self.set_row(id, Some(base + out.len()));
                    self.nodes[id as usize].entries = 1;
                    out.push(Entry { node: NodeId(id), part: Part::Block });
                }
                continue;
            }
            self.set_row(id, Some(base + out.len()));
            out.push(Entry { node: NodeId(id), part: Part::Header });
            stack.push((id, true));
            let at = stack.len();
            stack.extend(self.children(NodeId(id)).map(|c| (c.0, false)));
            stack[at..].reverse();
        }
    }

    /// Every row as a full flatten would number it.
    #[cfg(test)]
    pub(crate) fn assert_rows(&self) {
        for (i, e) in self.entries.iter().enumerate() {
            if e.part != Part::Footer {
                assert_eq!(self.row(e.node), Some(i), "{:?}'s row", self.key(e.node));
            }
            let (first, end) = self.entry_range(e.node);
            assert!(first <= i && i < end, "entry {i} is outside its node's range {first}..{end}");
        }
        let root = self.root().map_or(0, |r| self.node(r).entries as usize);
        assert_eq!(root, self.entries.len(), "the root counts every entry");
    }

    fn flatten(&mut self) {
        self.entries.clear();
        self.base = 0;
        let Some(root) = self.root() else { return };
        // Iterative preorder with explicit exits, so footers follow subtrees.
        let mut stack: Vec<(u32, bool)> = vec![(root.0, false)];
        while let Some((id, exiting)) = stack.pop() {
            let node = self.nodes[id as usize];
            let is_root = NodeId(id) == root;
            if exiting {
                if !is_root {
                    self.entries.push(Entry { node: NodeId(id), part: Part::Footer });
                }
                let first = if is_root { 0 } else { self.nodes[id as usize].entry };
                self.nodes[id as usize].entries = self.entries.len() as u32 - first;
                continue;
            }
            if node.kind.is_block() {
                if node.hidden {
                    self.set_row(id, None);
                    self.nodes[id as usize].entries = 0;
                } else {
                    self.set_row(id, Some(self.entries.len()));
                    self.nodes[id as usize].entries = 1;
                    self.entries.push(Entry { node: NodeId(id), part: Part::Block });
                }
                continue;
            }
            if is_root {
                self.set_row(id, None);
            } else {
                self.set_row(id, Some(self.entries.len()));
                self.entries.push(Entry { node: NodeId(id), part: Part::Header });
            }
            stack.push((id, true));
            let at = stack.len();
            stack.extend(self.children(NodeId(id)).map(|c| (c.0, false)));
            stack[at..].reverse();
        }
    }

    fn compact(&mut self) {
        let mut pages = Pages::default();
        for node in self.nodes.iter_mut().filter(|n| n.live && n.text.page & SCRATCH == 0) {
            node.text = pages.push(self.text.get(node.text));
        }
        self.text = pages;
    }
}

pub struct Children<'a> {
    doc: &'a Tree,
    next: u32,
}

impl Iterator for Children<'_> {
    type Item = NodeId;

    fn next(&mut self) -> Option<NodeId> {
        if self.next == NIL {
            return None;
        }
        let id = NodeId(self.next);
        self.next = self.doc.nodes[self.next as usize].next_sibling;
        Some(id)
    }
}

pub(crate) fn content_hash(kind: Kind, level: u8, hidden: bool, text: &str, spans: &[Span], side: &Side) -> u64 {
    let mut hasher = DefaultHasher::new();
    (kind as u8, level, hidden, text, spans, side).hash(&mut hasher);
    hasher.finish()
}
