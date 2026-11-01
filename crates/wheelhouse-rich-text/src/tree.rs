//! The tree in index-based arrays (#538).
use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};

use wheelhouse_stream::list_ops;

use super::ingest::{SourceInfo, Staged};
use super::pages::{Pages, Source, TextRange, PAGE};
use super::ring::{Ring, What};

/// A markdown node's key is its parent's key and its ordinal among its
/// siblings.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Default)]
pub struct Key(pub u64);

impl Key {
    pub const ROOT: Key = Key(0x5249_4348_5445_5854);

    pub fn child(self, ordinal: u32) -> Key {
        Key(mix(self.0 ^ mix(u64::from(ordinal) + 1)))
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
    /// Reserved for transcripts (slice 2).
    Turn = 5,
    /// Reserved for transcripts (slice 2).
    Section = 6,
    Paragraph = 16,
    Heading = 17,
    Code = 18,
    Rule = 19,
    Embed = 20,
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
    pub kind: Kind,
    pub hidden: bool,
    pub level: u8,
    pub live: bool,
    pub key: Key,
    pub version: u64,
    /// The content hash a reload matches on.
    pub hash: u64,
    pub text: TextRange,
    /// The node's first index entry (its header, or its Block row).
    pub entry: u32,
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
            kind: Kind::Paragraph,
            hidden: false,
            level: 0,
            live: false,
            key: Key(0),
            version: 0,
            hash: 0,
            text: TextRange::default(),
            entry: NIL,
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
        self.text.get(self.node(id).text)
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
        let node = self.node(self.get(key)?);
        if node.entry == NIL {
            return None;
        }
        match (node.kind.is_block(), part) {
            (true, Part::Block) => Some(node.entry as usize),
            (false, Part::Header) => Some(node.entry as usize),
            (false, Part::Footer) => Some((node.entry + node.entries - 1) as usize),
            _ => None,
        }
    }

    /// The Blocks' first entry and one past their last, for a node.
    pub(crate) fn entry_range(&self, id: NodeId) -> (usize, usize) {
        let node = self.node(id);
        if node.parent == NIL {
            return (0, self.entries.len());
        }
        if node.entry == NIL {
            return (0, 0);
        }
        (node.entry as usize, (node.entry + node.entries) as usize)
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
        self.live_text -= node.text.len as usize;
        node.live = false;
        node.first_child = NIL;
        node.last_child = NIL;
        node.next_sibling = NIL;
        node.parent = NIL;
        self.spans[id as usize] = Vec::new();
        self.sources[id as usize] = SourceInfo::default();
        self.side.remove(&id);
        self.free.push(id);
    }

    fn set_content(&mut self, id: u32, staged: &Staged, s: usize) {
        let node = &staged.nodes[s];
        let text = self.text.push(staged.text(s));
        let record = &mut self.nodes[id as usize];
        self.live_text -= record.text.len as usize;
        self.live_text += text.len as usize;
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

    fn flatten(&mut self) {
        self.entries.clear();
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
                let first = self.nodes[id as usize].entry;
                let count = self.entries.len() as u32 - if is_root { 0 } else { first };
                self.nodes[id as usize].entries = count;
                continue;
            }
            if node.kind.is_block() {
                if node.hidden {
                    self.nodes[id as usize].entry = NIL;
                    self.nodes[id as usize].entries = 0;
                } else {
                    self.nodes[id as usize].entry = self.entries.len() as u32;
                    self.nodes[id as usize].entries = 1;
                    self.entries.push(Entry { node: NodeId(id), part: Part::Block });
                }
                continue;
            }
            if is_root {
                self.nodes[id as usize].entry = NIL;
            } else {
                self.nodes[id as usize].entry = self.entries.len() as u32;
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
        for node in self.nodes.iter_mut().filter(|n| n.live) {
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
