//! pulldown-cmark's events straight into staging arrays, which a reload
//! reconciles into the tree (#540).
use std::collections::HashSet;
use std::ops::Range;

use pulldown_cmark::{Alignment, BlockQuoteKind, CodeBlockKind, Event, LinkType, MetadataBlockKind, Options, Parser, Tag, TagEnd};

use super::tree::{content_hash, Alert, Align, Attr, Cell, Detail, Key, Kind, Side, Span, Table, NIL, NO_STRING, TABLE_MEDIA_TYPE};

pub(crate) fn options() -> Options {
    Options::ENABLE_TABLES
        | Options::ENABLE_FOOTNOTES
        | Options::ENABLE_STRIKETHROUGH
        | Options::ENABLE_TASKLISTS
        | Options::ENABLE_GFM
        | Options::ENABLE_YAML_STYLE_METADATA_BLOCKS
        | Options::ENABLE_PLUSES_DELIMITED_METADATA_BLOCKS
        | Options::ENABLE_WIKILINKS
        | Options::ENABLE_MATH
}

/// A run of a Block's text and the source it came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Segment {
    pub text: u32,
    pub text_len: u32,
    pub source: u32,
    pub source_len: u32,
    /// The text is the source, byte for byte.
    pub exact: bool,
    /// Code or math: its markers are the source's ends, and a copy that cuts
    /// it makes new ones.
    pub atomic: bool,
}

impl Segment {
    pub fn text_end(&self) -> u32 {
        self.text + self.text_len
    }

    pub fn source_end(&self) -> u32 {
        self.source + self.source_len
    }
}

/// Where a span's markup is: `outer` from its opening marker to its closing
/// one, `inner` its content.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(crate) struct SpanSource {
    pub outer: (u32, u32),
    pub inner: (u32, u32),
}

/// Not part of a node's content: a reload refreshes it without a new
/// version.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub(crate) struct SourceInfo {
    /// The whole node, markers included.
    pub block: (u32, u32),
    pub content: (u32, u32),
    pub segments: Vec<Segment>,
    pub spans: Vec<SpanSource>,
    /// A list item's marker as written, with the spaces after it.
    pub marker: String,
}

pub(crate) struct StagedNode {
    pub key: Key,
    pub kind: Kind,
    pub level: u8,
    pub hidden: bool,
    pub parent: u32,
    pub children: Vec<u32>,
    text: (u32, u32),
    pub spans: Vec<Span>,
    pub side: Side,
    pub hash: u64,
    pub source: SourceInfo,
    /// Its text goes to this scratch string, not the pages.
    pub scratch: Option<u32>,
}

#[derive(Default)]
pub(crate) struct Staged {
    /// In document order: a parent before its children.
    pub nodes: Vec<StagedNode>,
    texts: String,
    /// The first ordinal for the root's children: a streaming tail's Blocks
    /// follow the frozen ones.
    first: u32,
}

impl Staged {
    pub fn text(&self, s: usize) -> &str {
        let (start, len) = self.nodes[s].text;
        &self.texts[start as usize..(start + len) as usize]
    }

    /// A root standing in for the node whose children a splice replaces.
    pub fn under(key: Key, first: u32) -> Staged {
        let mut staged = Staged { nodes: vec![], texts: String::new(), first };
        staged.nodes.push(StagedNode {
            key,
            kind: Kind::Root,
            level: 0,
            hidden: false,
            parent: NIL,
            children: vec![],
            text: (0, 0),
            spans: vec![],
            side: Side::default(),
            hash: 0,
            source: SourceInfo::default(),
            scratch: None,
        });
        staged
    }

    /// `root` and its descendants are a streaming tail.
    pub fn scratch(&mut self, root: u32, slot: u32) {
        let mut stack = vec![root];
        while let Some(at) = stack.pop() {
            self.nodes[at as usize].scratch = Some(slot);
            stack.extend(&self.nodes[at as usize].children);
        }
    }

    pub fn roots(&self) -> &[u32] {
        &self.nodes[0].children
    }

    pub fn child_key(&self, parent: u32) -> Key {
        let node = &self.nodes[parent as usize];
        let first = if parent == 0 { self.first } else { 0 };
        node.key.child(first + node.children.len() as u32)
    }

    /// A node under `parent`, keyed by `key` or else by its ordinal.
    pub fn push(&mut self, parent: u32, kind: Kind, key: Option<Key>, text: &str, spans: Vec<Span>, detail: Detail) -> u32 {
        let key = key.unwrap_or_else(|| self.child_key(parent));
        let start = self.texts.len() as u32;
        self.texts.push_str(text);
        let side = Side { detail, ..Side::default() };
        let hash = content_hash(kind, 0, false, text, &spans, &side);
        let id = self.nodes.len() as u32;
        self.nodes.push(StagedNode {
            key,
            kind,
            level: 0,
            hidden: false,
            parent,
            children: vec![],
            text: (start, text.len() as u32),
            spans,
            side,
            hash,
            source: SourceInfo::default(),
            scratch: None,
        });
        self.nodes[parent as usize].children.push(id);
        id
    }

    /// Like [`Staged::push`], with strings for the spans to name.
    pub fn push_with_strings(&mut self, parent: u32, kind: Kind, text: &str, spans: Vec<Span>, strings: Vec<String>) -> u32 {
        let id = self.push(parent, kind, None, text, spans, Detail::None);
        let node = &mut self.nodes[id as usize];
        node.side.strings = strings;
        node.hash = content_hash(kind, 0, false, text, &node.spans, &node.side);
        id
    }
}

struct OpenSpan {
    index: usize,
    outer: Range<usize>,
    inner: Option<(u32, u32)>,
}

struct Leaf {
    id: u32,
    implicit: bool,
    /// Drop the text's last newline.
    trim: bool,
    open: Vec<OpenSpan>,
    content: Option<(u32, u32)>,
}

struct Builder<'s> {
    src: &'s str,
    staged: Staged,
    stack: Vec<u32>,
    leaf: Option<Leaf>,
    keys: HashSet<Key>,
}

fn union(a: Option<(u32, u32)>, r: &Range<usize>) -> Option<(u32, u32)> {
    let (s, e) = (r.start as u32, r.end as u32);
    Some(match a {
        Some((a0, a1)) => (a0.min(s), a1.max(e)),
        None => (s, e),
    })
}

impl<'s> Builder<'s> {
    fn add(&mut self, kind: Kind, range: &Range<usize>) -> u32 {
        let parent = self.stack.last().copied().unwrap_or(NIL);
        let mut key = if parent == NIL { Key::ROOT } else { self.staged.child_key(parent) };
        while !self.keys.insert(key) {
            key = key.rehash();
        }
        let id = self.staged.nodes.len() as u32;
        let start = self.staged.texts.len() as u32;
        self.staged.nodes.push(StagedNode {
            key,
            kind,
            level: 0,
            hidden: false,
            parent,
            children: vec![],
            text: (start, 0),
            spans: vec![],
            side: Side::default(),
            hash: 0,
            source: SourceInfo { block: (range.start as u32, range.end as u32), ..SourceInfo::default() },
            scratch: None,
        });
        if parent != NIL {
            self.staged.nodes[parent as usize].children.push(id);
        }
        id
    }

    fn open_container(&mut self, kind: Kind, detail: Detail, range: &Range<usize>) -> u32 {
        self.close_implicit();
        let id = self.add(kind, range);
        self.staged.nodes[id as usize].side.detail = detail;
        self.stack.push(id);
        id
    }

    fn close_container(&mut self) {
        self.close_implicit();
        if let Some(id) = self.stack.pop() {
            self.seal(id);
        }
    }

    fn open_leaf(&mut self, kind: Kind, detail: Detail, range: &Range<usize>, implicit: bool, trim: bool) -> u32 {
        self.close_implicit();
        let id = self.add(kind, range);
        self.staged.nodes[id as usize].side.detail = detail;
        let trim = matches!(kind, Kind::Code) || trim;
        self.leaf = Some(Leaf { id, implicit, trim, open: vec![], content: None });
        id
    }

    fn close_leaf(&mut self) {
        let Some(leaf) = self.leaf.take() else { return };
        let id = leaf.id as usize;
        let start = self.staged.nodes[id].text.0 as usize;
        // Code and HTML end with the newline before their closing line.
        if leaf.trim && self.staged.texts.len() > start && self.staged.texts.ends_with('\n') {
            self.staged.texts.pop();
            if let Some(last) = self.staged.nodes[id].source.segments.last_mut() {
                last.text_len -= 1;
                if last.exact {
                    last.source_len -= 1;
                }
            }
        }
        let node = &mut self.staged.nodes[id];
        node.text.1 = (self.staged.texts.len() - start) as u32;
        node.source.content = leaf.content.unwrap_or((node.source.block.0, node.source.block.0));
        if leaf.implicit {
            node.source.block = node.source.content;
        }
        self.seal(leaf.id);
    }

    fn close_implicit(&mut self) {
        if self.leaf.as_ref().is_some_and(|leaf| leaf.implicit) {
            self.close_leaf();
        }
    }

    fn seal(&mut self, id: u32) {
        let hash = {
            let node = &self.staged.nodes[id as usize];
            let (start, len) = node.text;
            let text = &self.staged.texts[start as usize..(start + len) as usize];
            content_hash(node.kind, node.level, node.hidden, text, &node.spans, &node.side)
        };
        self.staged.nodes[id as usize].hash = hash;
    }

    /// The open Block, opening an implicit paragraph for a tight list
    /// item's inline content.
    fn leaf(&mut self, range: &Range<usize>) -> &mut Leaf {
        if self.leaf.is_none() {
            self.open_leaf(Kind::Paragraph, Detail::None, range, true, false);
        }
        self.leaf.as_mut().unwrap()
    }

    fn position(&self) -> u32 {
        let leaf = self.leaf.as_ref().unwrap();
        self.staged.texts.len() as u32 - self.staged.nodes[leaf.id as usize].text.0
    }

    fn text(&mut self, text: &str, range: &Range<usize>, atomic: bool) {
        let leaf = self.leaf(range);
        leaf.content = union(leaf.content, range);
        for span in &mut leaf.open {
            span.inner = union(span.inner, range);
        }
        let id = leaf.id as usize;
        let at = self.position();
        self.staged.texts.push_str(text);
        let exact = self.src.get(range.clone()) == Some(text);
        self.staged.nodes[id].source.segments.push(Segment {
            text: at,
            text_len: text.len() as u32,
            source: range.start as u32,
            source_len: range.len() as u32,
            exact,
            atomic,
        });
    }

    fn string(&mut self, s: &str) -> u32 {
        let id = self.leaf.as_ref().unwrap().id as usize;
        let strings = &mut self.staged.nodes[id].side.strings;
        strings.push(s.to_owned());
        (strings.len() - 1) as u32
    }

    fn atom(&mut self, attr: impl FnOnce(&mut Self) -> Attr, text: &str, range: &Range<usize>) {
        self.leaf(range);
        let attr = attr(self);
        let start = self.position();
        self.text(text, range, true);
        let id = self.leaf.as_ref().unwrap().id as usize;
        let node = &mut self.staged.nodes[id];
        node.spans.push(Span { start, len: text.len() as u32, attr });
        let r = (range.start as u32, range.end as u32);
        node.source.spans.push(SpanSource { outer: r, inner: r });
    }

    fn open_span(&mut self, attr: impl FnOnce(&mut Self) -> Attr, range: &Range<usize>) {
        let leaf = self.leaf(range);
        leaf.content = union(leaf.content, range);
        for span in &mut leaf.open {
            span.inner = union(span.inner, range);
        }
        let attr = attr(self);
        let start = self.position();
        let leaf = self.leaf.as_mut().unwrap();
        let node = &mut self.staged.nodes[leaf.id as usize];
        node.spans.push(Span { start, len: 0, attr });
        node.source.spans.push(SpanSource::default());
        leaf.open.push(OpenSpan { index: node.spans.len() - 1, outer: range.clone(), inner: None });
    }

    fn close_span(&mut self) {
        let end = self.position();
        let leaf = self.leaf.as_mut().unwrap();
        let Some(open) = leaf.open.pop() else { return };
        let node = &mut self.staged.nodes[leaf.id as usize];
        let span = &mut node.spans[open.index];
        span.len = end - span.start;
        let outer = (open.outer.start as u32, open.outer.end as u32);
        let inner = open.inner.unwrap_or((outer.1, outer.1));
        node.source.spans[open.index] = SpanSource { outer, inner };
    }

    /// Text that came from no source.
    fn separator(&mut self, s: &str) {
        self.staged.texts.push_str(s);
    }

    fn table(&mut self) -> &mut Table {
        let id = self.leaf.as_ref().unwrap().id as usize;
        match &mut self.staged.nodes[id].side.detail {
            Detail::Embed { table: Some(table), .. } => table,
            _ => unreachable!("a table's cells are in its Embed"),
        }
    }

    fn event(&mut self, event: Event<'_>, range: Range<usize>) {
        match event {
            Event::Start(tag) => self.start(tag, range),
            Event::End(tag) => self.end(tag),
            Event::Text(text) => self.text(&text, &range, false),
            Event::Code(code) => self.atom(|_| Attr::Code, &code, &range),
            Event::InlineMath(math) => self.atom(|_| Attr::Math, &math, &range),
            Event::DisplayMath(math) => self.atom(|_| Attr::DisplayMath, math.trim_matches('\n'), &range),
            Event::Html(html) | Event::InlineHtml(html) => self.text(&html, &range, false),
            Event::FootnoteReference(label) => {
                let text = format!("[^{label}]");
                self.atom(|b| Attr::FootnoteReference { label: b.string(&label) }, &text, &range)
            }
            Event::SoftBreak => self.text(" ", &range, false),
            Event::HardBreak => self.text("\n", &range, false),
            Event::Rule => {
                self.close_implicit();
                self.open_leaf(Kind::Rule, Detail::None, &range, false, false);
                self.close_leaf();
            }
            Event::TaskListMarker(checked) => {
                if let Some(&item) = self.stack.last() {
                    if let Detail::Item { task } = &mut self.staged.nodes[item as usize].side.detail {
                        *task = Some(checked);
                    }
                }
            }
        }
    }

    fn start(&mut self, tag: Tag<'_>, range: Range<usize>) {
        match tag {
            Tag::Paragraph => {
                if let Some(&item) = self.stack.last() {
                    if self.staged.nodes[item as usize].kind == Kind::ListItem {
                        let list = self.staged.nodes[item as usize].parent as usize;
                        if let Detail::List { tight, .. } = &mut self.staged.nodes[list].side.detail {
                            *tight = false;
                        }
                    }
                }
                self.open_leaf(Kind::Paragraph, Detail::None, &range, false, false);
            }
            Tag::Heading { level, .. } => {
                let id = self.open_leaf(Kind::Heading, Detail::None, &range, false, false);
                self.staged.nodes[id as usize].level = level as u8;
            }
            Tag::BlockQuote(kind) => {
                let alert = kind.map(|kind| match kind {
                    BlockQuoteKind::Note => Alert::Note,
                    BlockQuoteKind::Tip => Alert::Tip,
                    BlockQuoteKind::Important => Alert::Important,
                    BlockQuoteKind::Warning => Alert::Warning,
                    BlockQuoteKind::Caution => Alert::Caution,
                });
                self.open_container(Kind::Quote, Detail::Quote { alert }, &range);
            }
            Tag::CodeBlock(kind) => {
                let language = match kind {
                    CodeBlockKind::Fenced(info) => info.split_whitespace().next().unwrap_or("").to_owned(),
                    CodeBlockKind::Indented => String::new(),
                };
                self.open_leaf(Kind::Code, Detail::Code { language }, &range, false, false);
            }
            Tag::HtmlBlock => {
                self.open_leaf(Kind::Paragraph, Detail::None, &range, false, true);
            }
            Tag::List(start) => {
                let detail = Detail::List { ordered: start.is_some(), start: start.unwrap_or(1), tight: true };
                self.open_container(Kind::List, detail, &range);
            }
            Tag::Item => {
                let id = self.open_container(Kind::ListItem, Detail::Item { task: None }, &range);
                self.staged.nodes[id as usize].source.marker = marker(self.src, range.start);
            }
            Tag::FootnoteDefinition(label) => {
                self.open_container(Kind::Footnote, Detail::Footnote { label: label.to_string() }, &range);
            }
            Tag::Table(alignments) => {
                let alignments = alignments
                    .into_iter()
                    .map(|a| match a {
                        Alignment::None => Align::None,
                        Alignment::Left => Align::Left,
                        Alignment::Center => Align::Center,
                        Alignment::Right => Align::Right,
                    })
                    .collect();
                let detail = Detail::Embed { media_type: TABLE_MEDIA_TYPE.into(), table: Some(Table { alignments, rows: vec![] }) };
                self.open_leaf(Kind::Embed, detail, &range, false, false);
            }
            Tag::TableHead | Tag::TableRow => {
                if !self.table().rows.is_empty() {
                    self.separator("\n");
                }
                self.table().rows.push(vec![]);
            }
            Tag::TableCell => {
                if !self.table().rows.last().unwrap().is_empty() {
                    self.separator("\t");
                }
                let start = self.position();
                self.table().rows.last_mut().unwrap().push(Cell { start, len: 0 });
            }
            Tag::Emphasis => self.open_span(|_| Attr::Emphasis, &range),
            Tag::Strong => self.open_span(|_| Attr::Strong, &range),
            Tag::Strikethrough => self.open_span(|_| Attr::Strikethrough, &range),
            Tag::Link { link_type, dest_url, id, .. } | Tag::Image { link_type, dest_url, id, .. } => {
                let reference = matches!(link_type, LinkType::Reference | LinkType::Collapsed | LinkType::Shortcut);
                self.open_span(
                    |b| {
                        let target = b.string(&dest_url);
                        let reference = if reference { b.string(&id) } else { NO_STRING };
                        Attr::Link { target, reference }
                    },
                    &range,
                )
            }
            Tag::MetadataBlock(kind) => {
                let language = match kind {
                    MetadataBlockKind::YamlStyle => "yaml",
                    MetadataBlockKind::PlusesStyle => "toml",
                };
                let id = self.open_leaf(Kind::Code, Detail::Code { language: language.into() }, &range, false, false);
                self.staged.nodes[id as usize].hidden = true;
            }
            // Superscript, subscript and definition lists are off.
            Tag::Superscript | Tag::Subscript | Tag::DefinitionList | Tag::DefinitionListTitle | Tag::DefinitionListDefinition => {}
        }
    }

    fn end(&mut self, tag: TagEnd) {
        match tag {
            TagEnd::Paragraph | TagEnd::Heading(_) | TagEnd::CodeBlock | TagEnd::HtmlBlock | TagEnd::Table | TagEnd::MetadataBlock(_) => {
                self.close_leaf()
            }
            TagEnd::BlockQuote(_) | TagEnd::List(_) | TagEnd::Item | TagEnd::FootnoteDefinition => self.close_container(),
            TagEnd::TableCell => {
                let end = self.position();
                let cell = self.table().rows.last_mut().unwrap().last_mut().unwrap();
                cell.len = end - cell.start;
            }
            TagEnd::TableHead | TagEnd::TableRow => {}
            TagEnd::Emphasis | TagEnd::Strong | TagEnd::Strikethrough | TagEnd::Link | TagEnd::Image => self.close_span(),
            TagEnd::Superscript
            | TagEnd::Subscript
            | TagEnd::DefinitionList
            | TagEnd::DefinitionListTitle
            | TagEnd::DefinitionListDefinition => {}
        }
    }
}

/// A list item's marker at `at`, with the spaces after it that belong to it.
fn marker(src: &str, at: usize) -> String {
    let rest = src[at..].trim_start_matches([' ', '\t']);
    let bytes = rest.as_bytes();
    let mut end = 0;
    while end < bytes.len() && bytes[end].is_ascii_digit() {
        end += 1;
    }
    if end < bytes.len() && matches!(bytes[end], b'.' | b')' | b'-' | b'+' | b'*') {
        end += 1;
    }
    let mut spaces = 0;
    while end + spaces < bytes.len() && bytes[end + spaces] == b' ' {
        spaces += 1;
    }
    // Five or more spaces start indented code: the marker takes one.
    if spaces >= 5 || end + spaces >= bytes.len() || bytes[end + spaces] == b'\n' {
        spaces = spaces.min(1);
    }
    rest[..end + spaces].to_owned()
}

pub(crate) fn parse(source: &str) -> Staged {
    let mut builder = Builder {
        src: source,
        staged: Staged { nodes: vec![], texts: String::new(), first: 0 },
        stack: vec![],
        leaf: None,
        keys: HashSet::new(),
    };
    let all = 0..source.len();
    builder.open_container(Kind::Root, Detail::None, &all);
    builder.run();
    builder.staged
}

/// Markdown's Blocks as children of `parent`, a Container already staged.
/// Source offsets are moved by `offset`: `source` is a message's tail.
pub(crate) fn parse_into(staged: &mut Staged, parent: u32, source: &str, offset: u32) {
    let first = staged.nodes.len();
    let keys = staged.nodes.iter().map(|n| n.key).collect();
    let mut builder = Builder { src: source, staged: std::mem::take(staged), stack: vec![parent], leaf: None, keys };
    builder.run();
    *staged = builder.staged;
    if offset > 0 {
        for node in &mut staged.nodes[first..] {
            node.source.shift(offset);
        }
    }
}

impl Builder<'_> {
    fn run(&mut self) {
        for (event, range) in Parser::new_ext(self.src, options()).into_offset_iter() {
            self.event(event, range);
        }
        self.close_leaf();
        while !self.stack.is_empty() {
            self.close_container();
        }
    }
}

impl SourceInfo {
    fn shift(&mut self, by: u32) {
        let pair = |p: &mut (u32, u32)| {
            p.0 += by;
            p.1 += by;
        };
        pair(&mut self.block);
        pair(&mut self.content);
        for segment in &mut self.segments {
            segment.source += by;
        }
        for span in &mut self.spans {
            pair(&mut span.outer);
            pair(&mut span.inner);
        }
    }
}
