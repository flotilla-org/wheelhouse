//! Selection and copy (#546). An Embed is selected whole: an endpoint sits
//! just before it (0) or just after it (its length). A Rule's length is 1.
use std::cmp::Reverse;
use std::collections::BTreeMap;
use std::ptr;

use pulldown_cmark::html::push_html;
use pulldown_cmark::{Alignment, CodeBlockKind, Event, Parser, Tag, TagEnd};

use super::ingest::{options, Segment, SpanSource};
use super::tree::{Attr, Detail, Key, Kind, NodeId, Part, Span, Tree, NO_STRING};
use super::view::{View, Was};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Point {
    pub key: Key,
    pub byte: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Selection {
    pub anchor: Point,
    pub focus: Point,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    /// A blank line between Blocks, list markers kept, code without fences,
    /// tables as TSV.
    Plain,
    /// Copy as Markdown, made into HTML.
    Html,
    /// The raw source, sliced so that it stays valid markdown.
    Markdown,
}

pub(crate) fn extent(doc: &Tree, id: NodeId) -> u32 {
    match doc.kind(id) {
        Kind::Rule => 1,
        _ => doc.text(id).len() as u32,
    }
}

/// On a character boundary, in range, and before or after an Embed or Rule.
pub(crate) fn normalize(doc: &Tree, point: Point) -> Result<Point, String> {
    let id = doc.get(point.key).ok_or("no such node")?;
    if !doc.kind(id).is_block() || doc.hidden(id) {
        return Err("a selection's endpoints are in shown Blocks".into());
    }
    Ok(Point { key: point.key, byte: snap(doc, id, point.byte) })
}

fn snap(doc: &Tree, id: NodeId, byte: u32) -> u32 {
    let len = extent(doc, id);
    match doc.kind(id) {
        Kind::Embed | Kind::Rule => {
            if byte < len.div_ceil(2) {
                0
            } else {
                len
            }
        }
        _ => {
            let text = doc.text(id);
            let mut byte = (byte as usize).min(text.len());
            while !text.is_char_boundary(byte) {
                byte -= 1;
            }
            byte as u32
        }
    }
}

fn order(doc: &Tree, point: Point) -> (usize, u32) {
    (doc.entry_of(point.key, Part::Block).unwrap_or(0), point.byte)
}

impl Selection {
    /// In document order.
    pub fn ordered(&self, doc: &Tree) -> (Point, Point) {
        if order(doc, self.anchor) <= order(doc, self.focus) {
            (self.anchor, self.focus)
        } else {
            (self.focus, self.anchor)
        }
    }

    /// After a reload: an endpoint whose Block shrank clamps to its end; one
    /// whose Block went moves to the next Block that stayed, or the end of
    /// the one before.
    pub(crate) fn clamp(&mut self, doc: &Tree, before: &[Was]) -> bool {
        match (clamp(doc, self.anchor, before), clamp(doc, self.focus, before)) {
            (Some(anchor), Some(focus)) => {
                *self = Selection { anchor, focus };
                true
            }
            _ => false,
        }
    }
}

fn clamp(doc: &Tree, point: Point, before: &[Was]) -> Option<Point> {
    if let Ok(point) = normalize(doc, point) {
        return Some(point);
    }
    let shown = |key: Key| doc.get(key).filter(|&id| doc.kind(id).is_block() && !doc.hidden(id));
    let at = before.iter().position(|w| w.key == point.key && w.part == Part::Block)?;
    if let Some((key, _)) = before[at..].iter().find_map(|w| Some((w.key, shown(w.key)?))) {
        return Some(Point { key, byte: 0 });
    }
    before[..at].iter().rev().find_map(|w| shown(w.key).map(|id| Point { key: w.key, byte: extent(doc, id) }))
}

/// `start` and `end` are char boundaries of the Block's text, as
/// `normalize` leaves a selection's endpoints.
#[derive(Debug, Clone, Copy)]
struct Piece {
    id: NodeId,
    entry: usize,
    start: u32,
    end: u32,
}

impl View {
    pub fn selection(&self) -> Option<Selection> {
        self.selection
    }

    pub fn select(&mut self, doc: &Tree, anchor: Point, focus: Point) -> Result<(), String> {
        self.selection = Some(Selection { anchor: normalize(doc, anchor)?, focus: normalize(doc, focus)? });
        Ok(())
    }

    /// A caret at `focus` if there is no selection.
    pub fn extend(&mut self, doc: &Tree, focus: Point) -> Result<(), String> {
        let focus = normalize(doc, focus)?;
        let anchor = self.selection.map_or(focus, |s| s.anchor);
        self.selection = Some(Selection { anchor, focus });
        Ok(())
    }

    pub fn select_block(&mut self, doc: &Tree, key: Key) -> Result<(), String> {
        let id = doc.get(key).ok_or("no such node")?;
        self.select(doc, Point { key, byte: 0 }, Point { key, byte: extent(doc, id) })
    }

    pub fn select_all(&mut self, doc: &Tree) {
        let blocks: Vec<NodeId> = doc.entries().iter().filter(|e| e.part == Part::Block).map(|e| e.node).collect();
        self.selection = match (blocks.first(), blocks.last()) {
            (Some(&first), Some(&last)) => Some(Selection {
                anchor: Point { key: doc.key(first), byte: 0 },
                focus: Point { key: doc.key(last), byte: extent(doc, last) },
            }),
            _ => None,
        };
    }

    pub fn clear_selection(&mut self) {
        self.selection = None;
    }

    fn pieces(&self, doc: &Tree) -> Vec<Piece> {
        let Some(selection) = self.selection else { return vec![] };
        let (start, end) = selection.ordered(doc);
        let (Some(first), Some(last)) = (doc.entry_of(start.key, Part::Block), doc.entry_of(end.key, Part::Block)) else {
            return vec![];
        };
        let mut out = vec![];
        for entry in first..=last {
            let e = doc.entries()[entry];
            if e.part != Part::Block || self.heights.get(entry).hidden() {
                continue;
            }
            let a = if entry == first { start.byte } else { 0 };
            let b = if entry == last { end.byte } else { extent(doc, e.node) };
            // Part of a paragraph or heading that is only whitespace shows
            // nothing, so copies leave it out.
            let blank = matches!(doc.kind(e.node), Kind::Paragraph | Kind::Heading)
                && (a, b) != (0, extent(doc, e.node))
                && doc.text(e.node)[a as usize..b as usize].trim().is_empty();
            if a < b && !blank {
                out.push(Piece { id: e.node, entry, start: a, end: b });
            }
        }
        out
    }

    /// The plain copy's Blocks, without the markers it puts before them.
    #[cfg(test)]
    pub(crate) fn copied_blocks(&self, doc: &Tree) -> Vec<String> {
        self.pieces(doc).iter().map(|piece| block_plain(doc, piece)).collect()
    }

    /// What a collapsed Container hides is not copied.
    pub fn copy(&self, doc: &Tree, format: Format) -> Option<String> {
        let pieces = self.pieces(doc);
        if pieces.is_empty() {
            return None;
        }
        Some(match format {
            Format::Plain => plain(doc, &pieces),
            Format::Html => html(&markdown(doc, self, &pieces)),
            Format::Markdown => markdown(doc, self, &pieces),
        })
    }
}

/// Collapsed or not.
pub fn copy_block(doc: &Tree, key: Key, format: Format) -> Option<String> {
    let id = doc.get(key).filter(|&id| doc.kind(id).is_block())?;
    let piece = Piece { id, entry: doc.entry_of(key, Part::Block).unwrap_or(0), start: 0, end: extent(doc, id) };
    let markdown = || {
        let info = doc.source_info(id);
        doc.source().slice(info.block.0 as usize, info.block.1 as usize).trim_end_matches('\n').to_owned()
    };
    Some(match format {
        Format::Plain => block_plain(doc, &piece),
        Format::Html => html(&markdown()),
        Format::Markdown => markdown(),
    })
}

/// The list items and footnotes holding a Block, outermost first.
fn markers(doc: &Tree, id: NodeId) -> Vec<NodeId> {
    let mut out = vec![];
    let mut at = doc.parent(id);
    while let Some(container) = at {
        if matches!(doc.kind(container), Kind::ListItem | Kind::Footnote | Kind::Quote) {
            out.push(container);
        }
        at = doc.parent(container);
    }
    out.reverse();
    out
}

/// Whether `id` is the first Block of `container`.
fn leads(doc: &Tree, container: NodeId, id: NodeId) -> bool {
    let mut at = container;
    while !doc.kind(at).is_block() {
        match doc.children(at).next() {
            Some(child) => at = child,
            None => return false,
        }
    }
    at == id
}

fn number(doc: &Tree, item: NodeId) -> Option<u64> {
    let list = doc.parent(item)?;
    let Detail::List { ordered: true, start, .. } = doc.detail(list) else { return None };
    let index = doc.children(list).position(|c| c == item)? as u64;
    Some(start + index)
}

fn task(doc: &Tree, item: NodeId) -> &'static str {
    match doc.detail(item) {
        Detail::Item { task: Some(true) } => "[x] ",
        Detail::Item { task: Some(false) } => "[ ] ",
        _ => "",
    }
}

fn spaces(n: usize) -> String {
    " ".repeat(n)
}

/// A Block's plain prefixes: on its first line, and on the lines after. An
/// item's marker goes before its first Block, and before the first Block a
/// selection holds: a partly selected item keeps its structure.
fn plain_prefix(doc: &Tree, id: NodeId, first_selected: bool) -> (String, String) {
    let mut first = String::new();
    let mut rest = String::new();
    for container in markers(doc, id) {
        let marker = match doc.kind(container) {
            Kind::ListItem => match number(doc, container) {
                Some(n) => format!("{n}. "),
                None => "- ".to_owned(),
            },
            Kind::Footnote => match doc.detail(container) {
                Detail::Footnote { label } => format!("[^{label}]: "),
                _ => continue,
            },
            _ => continue,
        };
        let leading = leads(doc, container, id);
        let item = doc.kind(container) == Kind::ListItem;
        // A task's box is part of its first paragraph.
        let marker = if item && leading { marker + task(doc, container) } else { marker };
        let width = marker.chars().count();
        first += &if leading || (item && first_selected) { marker } else { spaces(width) };
        rest += &spaces(width);
    }
    (first, rest)
}

fn prefixed(body: &str, first: &str, rest: &str) -> String {
    let mut out = String::new();
    for (i, line) in body.split('\n').enumerate() {
        if i > 0 {
            out.push('\n');
            out.push_str(rest);
        } else {
            out.push_str(first);
        }
        out.push_str(line);
    }
    out
}

fn block_plain(doc: &Tree, piece: &Piece) -> String {
    match doc.kind(piece.id) {
        Kind::Rule => "---".to_owned(),
        _ => doc.text(piece.id)[piece.start as usize..piece.end as usize].to_owned(),
    }
}

fn plain(doc: &Tree, pieces: &[Piece]) -> String {
    let blocks: Vec<String> = pieces
        .iter()
        .enumerate()
        .map(|(i, piece)| {
            let (first, rest) = plain_prefix(doc, piece.id, i == 0);
            prefixed(&block_plain(doc, piece), &first, &rest)
        })
        .collect();
    blocks.join("\n\n")
}

/// The whole document's plain text, as shown.
pub fn rendered(doc: &Tree) -> String {
    let pieces: Vec<Piece> = doc
        .entries()
        .iter()
        .enumerate()
        .filter(|(_, e)| e.part == Part::Block)
        .map(|(entry, e)| Piece { id: e.node, entry, start: 0, end: extent(doc, e.node) })
        .filter(|p| p.start < p.end)
        .collect();
    plain(doc, &pieces)
}

/// HTML for Copy as Markdown's output, with no classes or styles (#546):
/// alerts, code languages, alignments and footnotes lose theirs, math is
/// code, and an image is a link.
fn html(markdown: &str) -> String {
    let events = Parser::new_ext(markdown, options()).flat_map(|event| match event {
        Event::FootnoteReference(label) => {
            vec![Event::Start(Tag::Superscript), Event::Text(format!("[^{label}]").into()), Event::End(TagEnd::Superscript)]
        }
        event => vec![event],
    });
    // A link is copied as its text unless its target is safe to paste.
    let mut kept = vec![];
    let events = events.filter_map(move |event| match event {
        Event::Start(Tag::Link { ref dest_url, .. } | Tag::Image { ref dest_url, .. }) if !safe_href(dest_url) => {
            kept.push(false);
            None
        }
        Event::Start(Tag::Link { .. } | Tag::Image { .. }) => {
            kept.push(true);
            Some(event)
        }
        Event::End(TagEnd::Link | TagEnd::Image) if !kept.pop().unwrap_or(true) => None,
        event => Some(event),
    });
    let events = events.map(|event| match event {
        Event::Start(Tag::BlockQuote(Some(_))) => Event::Start(Tag::BlockQuote(None)),
        Event::End(TagEnd::BlockQuote(Some(_))) => Event::End(TagEnd::BlockQuote(None)),
        Event::Start(Tag::CodeBlock(CodeBlockKind::Fenced(_))) => Event::Start(Tag::CodeBlock(CodeBlockKind::Indented)),
        Event::Start(Tag::Table(alignments)) => Event::Start(Tag::Table(vec![Alignment::None; alignments.len()])),
        Event::Start(Tag::Image { link_type, dest_url, title, id }) => Event::Start(Tag::Link { link_type, dest_url, title, id }),
        Event::End(TagEnd::Image) => Event::End(TagEnd::Link),
        Event::InlineMath(math) => Event::Code(math),
        Event::DisplayMath(math) => Event::Code(math.trim_matches('\n').to_owned().into()),
        Event::Start(Tag::FootnoteDefinition(_)) => Event::Html("<aside>\n".into()),
        Event::End(TagEnd::FootnoteDefinition) => Event::Html("</aside>\n".into()),
        // HTML is shown as text, so it is copied as text.
        Event::Start(Tag::HtmlBlock) => Event::Start(Tag::Paragraph),
        Event::End(TagEnd::HtmlBlock) => Event::End(TagEnd::Paragraph),
        Event::Html(text) | Event::InlineHtml(text) => Event::Text(text),
        event => event,
    });
    let mut out = String::new();
    push_html(&mut out, events);
    out
}

/// Relative, or http, https or mailto. Browsers drop whitespace and
/// control characters from a URL before reading its scheme, so this does too.
fn safe_href(url: &str) -> bool {
    let url: String = url.chars().filter(|c| !c.is_ascii_whitespace() && !c.is_ascii_control()).collect();
    match url.find(|c: char| !c.is_ascii_alphanumeric() && !"+-.".contains(c)) {
        Some(end) if url[end..].starts_with(':') => ["http", "https", "mailto"].iter().any(|s| url[..end].eq_ignore_ascii_case(s)),
        _ => true,
    }
}

/// A list item's marker as written, its number made the one shown, and as
/// wide.
fn item_marker(doc: &Tree, item: NodeId) -> String {
    let written = &doc.source_info(item).marker;
    let digits = written.bytes().take_while(u8::is_ascii_digit).count();
    let mut marker = match number(doc, item) {
        Some(n) if digits > 0 => {
            let rest = &written[digits..];
            let mut m = format!("{n}{rest}");
            while m.len() < written.len() && !m.ends_with("  ") {
                m.push(' ');
            }
            m
        }
        _ => written.clone(),
    };
    if !marker.ends_with(' ') {
        marker.push(' ');
    }
    marker
}

/// A list item's marker as written, at `column` and `width` chars wide, and
/// the marker to put there: a number can be wider than the one written.
struct MarkerEdit {
    column: usize,
    width: usize,
    marker: Vec<char>,
    fits: bool,
}

impl MarkerEdit {
    fn growth(&self) -> usize {
        self.marker.len().saturating_sub(self.width)
    }
}

/// A Block's markdown prefixes: for its first line, and for the lines of
/// a code Block made again. They are its first line's prefix as written, so
/// every container keeps its column, with each list item's marker put back
/// where it was: the first Block selected keeps its items' markers, as in
/// plain text.
fn markdown_prefix(doc: &Tree, id: NodeId) -> (String, String, Vec<(usize, usize)>) {
    let source = doc.source();
    let start = doc.source_info(id).block.0 as usize;
    let written: Vec<char> = source.slice(source.line_start(start), start).chars().collect();
    let mut lead = written.clone();
    let mut edits: Vec<MarkerEdit> = vec![];
    for item in markers(doc, id).into_iter().filter(|&c| doc.kind(c) == Kind::ListItem) {
        let at = doc.source_info(item).block.0 as usize;
        let at = at + source.slice(at, at + 64).len() - source.slice(at, at + 64).trim_start_matches(' ').len();
        let column = source.slice(source.line_start(at), at).chars().count();
        let width = doc.source_info(item).marker.chars().count().min(lead.len().saturating_sub(column));
        let marker: Vec<char> = item_marker(doc, item).chars().collect();
        let fits =
            lead.get(column..column + width).is_some_and(|p| p.iter().all(|c| *c == ' ' || c.is_ascii_digit() || ".)-+*".contains(*c)));
        edits.push(MarkerEdit { column, width, marker, fits });
    }
    // Rightmost first, so the columns to its left hold.
    edits.sort_by_key(|e| Reverse(e.column));
    let mut written_at = vec![];
    for e in &edits {
        if e.fits {
            lead.splice(e.column..e.column + e.width, e.marker.iter().copied());
        }
        let shift: usize = edits.iter().filter(|left| left.column < e.column && left.fits).map(MarkerEdit::growth).sum();
        written_at.push((e.column + shift, e.marker.len(), e.fits));
    }
    written_at.reverse();
    // The innermost container's content column.
    let quote = lead.iter().rposition(|&c| c == '>').map_or(0, |i| i + 1 + usize::from(lead.get(i + 1) == Some(&' ')));
    let content = written_at.iter().map(|&(column, len, _)| column + len).fold(quote, usize::max);
    let written_at: Vec<(usize, usize)> = written_at.into_iter().filter(|w| w.2).map(|(c, l, _)| (c, l)).collect();
    // Indentation past it would move the item's content column.
    if lead.get(content..).is_some_and(|after| after.iter().all(|&c| c == ' ')) {
        lead.truncate(content);
    }
    // A marker followed by more indentation than its own spaces would take
    // that indentation as its content column: it gets a line of its own,
    // and the line after keeps every column.
    let blank = |chars: &[char]| chars.iter().map(|&c| if c == '>' { '>' } else { ' ' }).collect::<String>();
    let mut out = String::new();
    for (column, len) in written_at {
        let after = column + len;
        if lead.get(after) == Some(&' ') && lead[after..].iter().any(|&c| c != ' ') {
            out += lead[..after].iter().collect::<String>().trim_end();
            out.push('\n');
            let kept: Vec<char> = blank(&lead[..after]).chars().chain(lead[after..].iter().copied()).collect();
            lead = kept;
        }
    }
    out.extend(lead);
    let rest = blank(&written[..content.min(written.len())]);
    // Where a marker grew, the item's content column moved right: the
    // lines after must move with it.
    let widened = edits.iter().filter(|e| e.fits && e.growth() > 0).map(|e| (e.column + e.width, e.growth())).collect();
    (out, rest, widened)
}

fn atomic(attr: Attr) -> bool {
    matches!(attr, Attr::Code | Attr::Math | Attr::DisplayMath | Attr::FootnoteReference { .. })
}

fn atom(attr: Attr, content: &str) -> String {
    let core = content.trim();
    if core.is_empty() {
        return content.to_owned();
    }
    let lead = &content[..content.len() - content.trim_start().len()];
    let trail = &content[content.trim_end().len()..];
    let marked = match attr {
        Attr::Code => {
            let run = longest_run(core, '`');
            let fence = "`".repeat(run + 1);
            let pad = if core.starts_with('`') || core.ends_with('`') { " " } else { "" };
            format!("{fence}{pad}{core}{pad}{fence}")
        }
        Attr::Math => format!("${core}$"),
        Attr::DisplayMath => format!("$${core}$$"),
        _ => core.to_owned(),
    };
    format!("{lead}{marked}{trail}")
}

fn longest_run(s: &str, c: char) -> usize {
    let mut best = 0;
    let mut run = 0;
    for x in s.chars() {
        run = if x == c { run + 1 } else { 0 };
        best = best.max(run);
    }
    best
}

/// Where text offset `at` is in the source: `start` for a selection's start
/// (the first piece of text at `at`), else its end.
fn locate(segments: &[Segment], at: u32, start: bool, content: (u32, u32)) -> u32 {
    let found = segments.iter().find(|s| if start { s.text <= at && at < s.text_end() } else { s.text < at && at <= s.text_end() });
    match found {
        Some(s) if s.exact => s.source + (at - s.text),
        Some(s) => {
            if start {
                s.source
            } else {
                s.source_end()
            }
        }
        None => {
            if start {
                segments.iter().find(|s| s.text >= at).map_or(content.1, |s| s.source)
            } else {
                segments.iter().rev().find(|s| s.text_end() <= at).map_or(content.0, |s| s.source_end())
            }
        }
    }
}

/// The markdown for `[a, b)` of an inline Block's text. Edges inside
/// formatting move out of its markers, and markers the edges cut are made
/// again; whitespace at a selection's edge is left out.
fn inline_markdown(doc: &Tree, id: NodeId, a: u32, b: u32, trim_start: bool, trim_end: bool) -> String {
    let text = doc.text(id);
    let (mut a, mut b) = (a as usize, b as usize);
    if trim_start {
        a = b.min(a + text[a..b].len() - text[a..b].trim_start().len());
    }
    if trim_end {
        b = a.max(b - (text[a..b].len() - text[a..b].trim_end().len()));
    }
    if a >= b {
        return String::new();
    }
    let (a, b) = (a as u32, b as u32);
    let info = doc.source_info(id);
    let source = doc.source();
    let spans = doc.spans(id);
    let marks: Vec<(&Span, &SpanSource)> = spans.iter().zip(&info.spans).filter(|(s, _)| s.len > 0).collect();
    let raw = |from: u32, to: u32| source.slice(from as usize, to as usize).into_owned();
    // An atomic span the start or end falls inside is made again.
    let inside = |at: u32| marks.iter().find(|(s, _)| atomic(s.attr) && s.start < at && at < s.end()).copied();
    let start_atom = inside(a);
    let end_atom = inside(b);
    if let (Some((x, _)), Some((y, _))) = (start_atom, end_atom) {
        if ptr::eq(x, y) {
            return atom(x.attr, &text[a as usize..b as usize]);
        }
    }
    let mut out = String::new();
    // Openers for the spans the start cuts, outermost first.
    let mut cut: Vec<&(&Span, &SpanSource)> = marks.iter().filter(|(s, _)| !atomic(s.attr) && s.start < a && a < s.end()).collect();
    cut.sort_by_key(|(s, _)| (s.start, Reverse(s.end())));
    for (span, src) in &cut {
        out.push_str(&opener(doc, id, span, src));
    }
    let mut from = locate(&info.segments, a, true, info.content);
    if let Some((span, src)) = start_atom {
        out.push_str(&atom(span.attr, &text[a as usize..span.end() as usize]));
        from = src.outer.1;
    } else if let Some(outer) = marks.iter().filter(|(s, _)| s.start == a).map(|(_, src)| src.outer.0).min() {
        from = from.min(outer);
    }
    let mut to = locate(&info.segments, b, false, info.content);
    let mut tail = String::new();
    if let Some((span, src)) = end_atom {
        tail = atom(span.attr, &text[span.start as usize..b as usize]);
        to = src.outer.0;
    } else if let Some(outer) = marks.iter().filter(|(s, _)| s.end() == b).map(|(_, src)| src.outer.1).max() {
        to = to.max(outer);
    }
    out.push_str(&raw(from, to.max(from)));
    out.push_str(&tail);
    let mut cut: Vec<&(&Span, &SpanSource)> = marks.iter().filter(|(s, _)| !atomic(s.attr) && s.start < b && b < s.end()).collect();
    cut.sort_by_key(|(s, _)| (Reverse(s.start), s.end()));
    for (_, src) in cut {
        out.push_str(&closer(doc, src));
    }
    out
}

fn opener(doc: &Tree, id: NodeId, span: &Span, src: &SpanSource) -> String {
    let written = doc.source().slice(src.outer.0 as usize, src.inner.0 as usize).into_owned();
    if written.starts_with('<') {
        // An autolink: its URL is its text, and a piece of it is plain text.
        return String::new();
    }
    if let Attr::Link { target, .. } = span.attr {
        if written == "[[" {
            // A wikilink without a label: keep its target.
            return format!("[[{}|", doc.strings(id).get(target as usize).map_or("", |s| s));
        }
    }
    written
}

fn closer(doc: &Tree, src: &SpanSource) -> String {
    let written = doc.source().slice(src.inner.1 as usize, src.outer.1 as usize).into_owned();
    if written.starts_with('>') {
        return String::new();
    }
    written
}

fn fenced(doc: &Tree, id: NodeId, a: u32, b: u32, rest: &str) -> String {
    let text = &doc.text(id)[a as usize..b as usize];
    let language = match doc.detail(id) {
        Detail::Code { language } => language.as_str(),
        _ => "",
    };
    let run = text.lines().map(|l| l.trim_start().chars().take_while(|&c| c == '`').count()).max().unwrap_or(0);
    let fence = "`".repeat(run.max(2) + 1);
    let mut out = format!("{fence}{language}\n");
    for line in text.split('\n') {
        out.push_str(rest);
        out.push_str(line);
        out.push('\n');
    }
    out.push_str(rest);
    out.push_str(&fence);
    out
}

fn escape_start(s: String) -> String {
    let line = s.lines().next().unwrap_or("");
    let bytes = line.as_bytes();
    let spaced = |at: usize| bytes.get(at).is_none_or(|&c| c == b' ' || c == b'\t');
    let hashes = bytes.iter().take_while(|&&b| b == b'#').count();
    let digits = bytes.iter().take_while(|b| b.is_ascii_digit()).count();
    let rule = |c: u8| bytes.iter().filter(|&&b| b == c).count() >= 3 && bytes.iter().all(|&b| b == c || b == b' ');
    let block = match bytes.first() {
        Some(b'#') => hashes <= 6 && spaced(hashes),
        Some(b'>') => true,
        Some(&c @ (b'-' | b'+' | b'*')) => spaced(1) || rule(c),
        Some(b'_') => rule(b'_'),
        Some(&c @ (b'`' | b'~')) => bytes.iter().take_while(|&&b| b == c).count() >= 3,
        _ => digits > 0 && digits <= 9 && matches!(bytes.get(digits), Some(b'.' | b')')) && spaced(digits + 1),
    };
    if !block {
        return s;
    }
    if digits > 0 {
        return format!("{}\\{}", &s[..digits], &s[digits..]);
    }
    format!("\\{s}")
}

/// The markdown for one selected Block. `first` and `last` say whether the
/// selection starts or ends in it.
fn block_markdown(doc: &Tree, piece: &Piece, first: bool, last: bool) -> String {
    let id = piece.id;
    let (lead, rest, _) = markdown_prefix(doc, id);
    let info = doc.source_info(id);
    let source = doc.source();
    // The first Block brings its line's prefix; a later one starts where the
    // raw source before it left off, except a code Block, whose fence is new
    // and starts its own line (see `markdown`).
    let prefix = match (first, doc.kind(id)) {
        (true, _) => lead,
        (false, Kind::Code) => rest.clone(),
        _ => String::new(),
    };
    let body = match doc.kind(id) {
        Kind::Code => fenced(doc, id, piece.start, piece.end, &rest),
        Kind::Embed | Kind::Rule => source.slice(info.block.0 as usize, info.block.1 as usize).trim_end_matches('\n').to_owned(),
        Kind::Heading => {
            let inline = inline_markdown(doc, id, piece.start, piece.end, first, last);
            format!("{} {inline}", "#".repeat(doc.level(id).clamp(1, 6) as usize))
        }
        _ => {
            let head = if first { String::new() } else { source.slice(info.block.0 as usize, info.content.0 as usize).into_owned() };
            let inline = inline_markdown(doc, id, piece.start, piece.end, first, last);
            if first {
                escape_start(inline)
            } else {
                head + &inline
            }
        }
    };
    prefix + &body
}

/// Copy as Markdown: the raw source between the selection's first and last
/// Blocks, with those two cut as the selection cuts them.
fn markdown(doc: &Tree, view: &View, pieces: &[Piece]) -> String {
    let (first, last) = (pieces[0], pieces[pieces.len() - 1]);
    let mut out = block_markdown(doc, &first, true, pieces.len() == 1);
    if pieces.len() > 1 {
        let from = block_end(doc, first.id);
        let to = doc.source_info(last.id).block.0;
        let mut between = gap(doc, view, first.entry, last.entry, from, to);
        if doc.kind(last.id) == Kind::Code {
            // Its fence is made again, after its line's prefix: the source's
            // may be an indented code Block's indentation.
            let line = between.rfind('\n').map_or(0, |i| i + 1);
            if between[line..].bytes().all(|b| b == b' ' || b == b'>') {
                between.truncate(line);
            }
        }
        out.push_str(&between);
        out.push_str(&block_markdown(doc, &last, false, true));
    }
    // Move the item's lines after the first right wherever its marker grew,
    // rightmost first, if only containers' prefixes come before. A line
    // less indented than the item's content leaves it, unless it is a
    // paragraph's lazy continuation; the lines after it stay.
    let mut widened = markdown_prefix(doc, first.id).2;
    widened.sort_by_key(|w| Reverse(w.0));
    if !widened.is_empty() {
        let mut lines: Vec<String> = out.split('\n').map(str::to_owned).collect();
        for &(column, by) in &widened {
            let mut after_blank = false;
            for line in lines.iter_mut().skip(1) {
                let chars: Vec<char> = line.chars().collect();
                let indent = chars.iter().take_while(|&&c| c == ' ' || c == '>').count();
                let rest: String = chars[indent..].iter().collect();
                if indent < column && !rest.trim().is_empty() && (after_blank || escape_start(rest.clone()) != rest) {
                    break;
                }
                after_blank = rest.trim().is_empty();
                if chars.len() >= column && indent >= column {
                    let at: usize = chars[..column].iter().map(|c| c.len_utf8()).sum();
                    line.insert_str(at, &spaces(by));
                }
            }
        }
        out = lines.join("\n");
    }
    // Reference links need their definitions.
    let mut definitions = BTreeMap::new();
    for piece in pieces {
        for span in doc.spans(piece.id) {
            if let Attr::Link { target, reference } = span.attr {
                if reference != NO_STRING && span.start < piece.end && span.end() > piece.start {
                    let strings = doc.strings(piece.id);
                    definitions.insert(strings[reference as usize].clone(), strings[target as usize].clone());
                }
            }
        }
    }
    for (label, url) in definitions {
        let mut escaped = String::new();
        for c in url.chars() {
            match c {
                '\\' | '<' | '>' => escaped.extend(['\\', c]),
                '\n' => escaped.push_str("%0A"),
                c => escaped.push(c),
            }
        }
        out.push_str(&format!("\n\n[{label}]: <{escaped}>"));
    }
    out
}

/// Where a Block's source ends, less the newline that ends its last line.
fn block_end(doc: &Tree, id: NodeId) -> u32 {
    let end = doc.source_info(id).block.1;
    if end > 0 && doc.source().slice(end as usize - 1, end as usize) == "\n" {
        end - 1
    } else {
        end
    }
}

/// The raw source from `from` to `to`, less what collapsed Containers
/// between entries `first` and `last` hide.
fn gap(doc: &Tree, view: &View, first: usize, last: usize, from: u32, to: u32) -> String {
    let mut out = String::new();
    let mut at = from;
    let mut entry = first + 1;
    while entry < last {
        let e = doc.entries()[entry];
        if e.part == Part::Header && view.is_collapsed(doc.key(e.node)) {
            let block = doc.source_info(e.node).block;
            // From its line's start, if only indentation and quote markers
            // come before it there.
            let before = doc.source().slice(at as usize, block.0 as usize);
            let line = before.rfind('\n').map_or(0, |i| i + 1);
            let cut = if before[line..].bytes().all(|b| b == b' ' || b == b'>') { line } else { before.len() };
            out.push_str(&before[..cut]);
            at = at.max(block.1);
            entry = doc.entry_range(e.node).1;
            continue;
        }
        entry += 1;
    }
    out.push_str(&doc.source().slice(at as usize, to as usize));
    out
}
