//! A subscriber's View state (#541). Heights are `f32` in the frontend's
//! units, distances `f64`.
use std::collections::{HashMap, HashSet};

use super::find::Find;
use super::heights::{Entry as Row, HeightIndex};
use super::select::Selection;
use super::tree::{Detail, Entry, EntryEdit, Key, Kind, NodeId, Part, Tree};

/// For estimates.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Metrics {
    pub width: f32,
    pub line_height: f32,
    /// About how many characters fit on a line at that width.
    pub chars_per_line: f32,
}

impl Default for Metrics {
    fn default() -> Self {
        Metrics { width: 0.0, line_height: 1.0, chars_per_line: 80.0 }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Position {
    /// `byte` is where the anchored line starts.
    Text { byte: u32, line_offset: f32 },
    /// Of an Embed's height, or a Container row's.
    Fraction(f32),
}

/// The scroll position is worked out from it, so heights changing above it
/// never move what is shown.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Anchor {
    pub key: Key,
    pub part: Part,
    pub position: Position,
    /// How far below the viewport's top the anchor point sits.
    pub viewport_offset: f64,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct WindowEntry {
    pub entry: usize,
    pub key: Key,
    pub part: Part,
    pub top: f64,
    pub height: f32,
    pub measured: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Window {
    pub top: f64,
    pub viewport: f64,
    pub total: f64,
    pub at_end: bool,
    pub entries: Vec<WindowEntry>,
}

/// Fractions of the document's height.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Thumb {
    pub start: f64,
    pub size: f64,
    pub estimated: f64,
}

/// On each side, as a share of the viewport.
pub const OVERSCAN: f64 = 0.5;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Was {
    pub key: Key,
    pub part: Part,
    pub version: u64,
}

pub(crate) fn was(doc: &Tree) -> Vec<Was> {
    doc.entries().iter().map(|e| Was { key: doc.key(e.node), part: e.part, version: doc.version(e.node) }).collect()
}

pub(crate) struct AnchorWas {
    hash: u64,
    entry: usize,
}

#[derive(Debug)]
pub struct View {
    pub(crate) heights: HeightIndex,
    metrics: Metrics,
    /// Width tags by width's bits; tag 0 is "never measured".
    tags: HashMap<u32, u16>,
    tag: u16,
    viewport: f64,
    anchor: Option<Anchor>,
    /// The anchored line's top within its entry, as last known.
    line_top: f32,
    at_end: bool,
    collapsed: HashSet<Key>,
    pub(crate) selection: Option<Selection>,
    pub(crate) find: Option<Find>,
}

pub(crate) fn estimate(doc: &Tree, entry: Entry, metrics: &Metrics) -> f32 {
    if entry.part != Part::Block {
        return 0.0;
    }
    let line = metrics.line_height;
    match doc.kind(entry.node) {
        Kind::Rule => line,
        Kind::Embed => match doc.detail(entry.node) {
            Detail::Embed { table: Some(table), .. } => table.rows.len() as f32 * line,
            _ => 4.0 * line,
        },
        _ => {
            let per_line = metrics.chars_per_line.max(1.0);
            let lines: f32 = doc.text(entry.node).split('\n').map(|l| (l.len() as f32 / per_line).ceil().max(1.0)).sum();
            lines * line
        }
    }
}

impl View {
    pub(crate) fn new(doc: &Tree) -> View {
        let mut view = View {
            heights: HeightIndex::default(),
            metrics: Metrics::default(),
            tags: HashMap::new(),
            tag: 0,
            viewport: 0.0,
            anchor: None,
            line_top: 0.0,
            at_end: false,
            collapsed: HashSet::new(),
            selection: None,
            find: None,
        };
        let rows: Vec<Row> = doc.entries().iter().map(|&e| Row::estimated(estimate(doc, e, &view.metrics), 0)).collect();
        view.heights.insert(0, &rows);
        view
    }

    pub fn metrics(&self) -> Metrics {
        self.metrics
    }

    pub fn viewport(&self) -> f64 {
        self.viewport
    }

    pub fn at_end(&self) -> bool {
        self.at_end
    }

    pub fn anchor(&self) -> Option<Anchor> {
        self.anchor
    }

    pub fn heights(&self) -> &HeightIndex {
        &self.heights
    }

    pub fn is_collapsed(&self, key: Key) -> bool {
        self.collapsed.contains(&key)
    }

    fn tag_for(&mut self, width: f32) -> u16 {
        if let Some(&tag) = self.tags.get(&width.to_bits()) {
            return tag;
        }
        if self.tags.len() == usize::from(u16::MAX) {
            // The tags will be reused, so no row may keep one.
            self.tags.clear();
            self.heights.update_all(|_, row| Row::estimated(row.height, 0).with_hidden(row.hidden()));
        }
        let tag = self.tags.len() as u16 + 1;
        self.tags.insert(width.to_bits(), tag);
        tag
    }

    /// A collapsed Container keeps its header row.
    fn hidden(&self, doc: &Tree, entry: Entry) -> bool {
        if self.collapsed.is_empty() {
            return false;
        }
        if entry.part == Part::Footer && self.collapsed.contains(&doc.key(entry.node)) {
            return true;
        }
        let mut at = doc.parent(entry.node);
        while let Some(id) = at {
            if self.collapsed.contains(&doc.key(id)) {
                return true;
            }
            at = doc.parent(id);
        }
        false
    }

    fn row(&self, doc: &Tree, entry: Entry) -> Row {
        Row::estimated(estimate(doc, entry, &self.metrics), 0).with_hidden(self.hidden(doc, entry))
    }

    /// Entries before and after the changed run keep their heights (a
    /// changed Block's becomes an estimate), and the run is replaced.
    pub(crate) fn sync(&mut self, doc: &Tree, before: &[Was], anchor: Option<AnchorWas>) {
        self.collapsed.retain(|&key| doc.get(key).is_some_and(|id| !doc.kind(id).is_block()));
        let after = doc.entries();
        let same = |w: &Was, e: &Entry| w.key == doc.key(e.node) && w.part == e.part;
        let mut prefix = 0;
        while prefix < before.len() && prefix < after.len() && same(&before[prefix], &after[prefix]) {
            prefix += 1;
        }
        let mut suffix = 0;
        while suffix < before.len() - prefix
            && suffix < after.len() - prefix
            && same(&before[before.len() - 1 - suffix], &after[after.len() - 1 - suffix])
        {
            suffix += 1;
        }
        let kept = (0..prefix).map(|i| (i, i)).chain((0..suffix).map(|i| (before.len() - 1 - i, after.len() - 1 - i)));
        for (old, new) in kept {
            if before[old].version != doc.version(after[new].node) {
                let row = self.heights.get(old);
                self.heights.set(old, row.stale());
            }
        }
        self.heights.remove(prefix..before.len() - suffix);
        let rows: Vec<Row> = after[prefix..after.len() - suffix].iter().map(|&e| self.row(doc, e)).collect();
        self.heights.insert(prefix, &rows);
        debug_assert_eq!(self.heights.len(), after.len());
        self.restore_anchor(doc, anchor);
        if let Some(selection) = &mut self.selection {
            selection.clamp(doc, before);
        }
        if let Some(find) = &mut self.find {
            find.restart();
        }
    }

    /// One splice's entries, as it left the tree.
    pub(crate) fn apply_edit(&mut self, doc: &Tree, edit: &EntryEdit) {
        self.heights.remove(edit.at..edit.at + edit.removed);
        let rows: Vec<Row> = doc.entries()[edit.at..edit.at + edit.inserted].iter().map(|&e| self.row(doc, e)).collect();
        self.heights.insert(edit.at, &rows);
        for &i in &edit.stale {
            let row = self.heights.get(i);
            self.heights.set(i, row.stale());
        }
        debug_assert_eq!(self.heights.len(), doc.entries().len());
        if let Some(selection) = &mut self.selection {
            if !selection.clamp(doc, &edit.was) {
                self.selection = None;
            }
        }
    }

    /// After a batch of edits.
    pub(crate) fn settle(&mut self, doc: &Tree, anchor: Option<AnchorWas>) {
        self.collapsed.retain(|&key| doc.get(key).is_some_and(|id| !doc.kind(id).is_block()));
        self.restore_anchor(doc, anchor);
        if let Some(find) = &mut self.find {
            find.restart();
        }
    }

    pub(crate) fn anchor_was(&self, doc: &Tree) -> Option<AnchorWas> {
        let anchor = self.anchor?;
        let id = doc.get(anchor.key)?;
        Some(AnchorWas { hash: doc.node(id).hash, entry: doc.entry_of(anchor.key, anchor.part)? })
    }

    /// Keys are parent plus ordinal, so an edit above the anchor can give its
    /// key to another Block: find the anchored Block by its content hash,
    /// the nearest one if several Blocks are the same.
    fn restore_anchor(&mut self, doc: &Tree, was: Option<AnchorWas>) {
        let (Some(anchor), Some(was)) = (self.anchor.as_mut(), was) else { return };
        if doc.get(anchor.key).is_some_and(|id| doc.node(id).hash == was.hash) {
            return;
        }
        let entries = doc.entries();
        let matches = |i: usize| entries[i].part == anchor.part && doc.node(entries[i].node).hash == was.hash;
        let found = (0..entries.len().max(was.entry + 1))
            .flat_map(|d| [was.entry.checked_sub(d), Some(was.entry + d)])
            .flatten()
            .find(|&i| i < entries.len() && matches(i));
        match found {
            Some(i) => anchor.key = doc.key(entries[i].node),
            None if entries.is_empty() => self.anchor = None,
            None => {
                let i = was.entry.min(entries.len() - 1);
                anchor.key = doc.key(entries[i].node);
                anchor.part = entries[i].part;
                if let Position::Text { byte, .. } = &mut anchor.position {
                    *byte = (*byte).min(doc.text(entries[i].node).len() as u32);
                }
            }
        }
    }

    fn max_top(&self) -> f64 {
        (self.heights.total() - self.viewport).max(0.0)
    }

    fn anchor_depth(&self, entry: usize, anchor: &Anchor) -> f64 {
        let size = self.heights.get(entry).size();
        let depth = match anchor.position {
            Position::Text { line_offset, .. } => f64::from(self.line_top) + f64::from(line_offset),
            Position::Fraction(f) => f64::from(f) * size,
        };
        depth.clamp(0.0, size)
    }

    pub fn top(&self, doc: &Tree) -> f64 {
        if self.at_end {
            return self.max_top();
        }
        let Some(anchor) = &self.anchor else { return 0.0 };
        let Some(entry) = doc.entry_of(anchor.key, anchor.part) else { return 0.0 };
        let top = self.heights.offset_of(entry) + self.anchor_depth(entry, anchor) - anchor.viewport_offset;
        top.clamp(0.0, self.max_top())
    }

    fn anchor_at(&mut self, doc: &Tree, top: f64) {
        let Some((entry, depth)) = self.heights.entry_at(top).or_else(|| self.last_visible()) else {
            self.anchor = None;
            return;
        };
        let e = doc.entries()[entry];
        let size = self.heights.get(entry).size();
        let text_block = e.part == Part::Block && doc.kind(e.node) != Kind::Embed;
        let position = if text_block {
            let text = doc.text(e.node);
            let mut byte = if size > 0.0 { ((depth / size) * text.len() as f64) as usize } else { 0 }.min(text.len());
            while !text.is_char_boundary(byte) {
                byte -= 1;
            }
            self.line_top = depth as f32;
            Position::Text { byte: byte as u32, line_offset: 0.0 }
        } else {
            Position::Fraction(if size > 0.0 { (depth / size) as f32 } else { 0.0 })
        };
        self.anchor = Some(Anchor { key: doc.key(e.node), part: e.part, position, viewport_offset: 0.0 });
    }

    fn last_visible(&self) -> Option<(usize, f64)> {
        let total = self.heights.total();
        if total <= 0.0 {
            return None;
        }
        self.heights.entry_at(total - f64::EPSILON * total.max(1.0)).map(|(i, _)| (i, f64::from(self.heights.get(i).height)))
    }

    pub fn set_viewport(&mut self, height: f64) {
        self.viewport = height.max(0.0);
    }

    /// Scrolling up breaks follow-tail, and reaching the bottom re-arms it.
    pub fn scroll_by(&mut self, doc: &Tree, delta: f64) {
        let top = self.top(doc);
        let next = (top + delta).clamp(0.0, self.max_top());
        if delta < 0.0 {
            self.at_end = false;
        }
        self.anchor_at(doc, next);
        if delta > 0.0 && self.max_top() - next <= f64::from(self.metrics.line_height) / 2.0 {
            self.at_end = true;
        }
    }

    /// Breaks follow-tail.
    pub fn scroll_to(&mut self, doc: &Tree, anchor: Anchor) -> Result<(), String> {
        let entry = doc.entry_of(anchor.key, anchor.part).ok_or("no such entry")?;
        self.at_end = false;
        self.anchor = Some(anchor);
        if let Position::Text { byte, .. } = anchor.position {
            let e = doc.entries()[entry];
            let len = doc.text(e.node).len().max(1) as f32;
            self.line_top = (byte as f32 / len).min(1.0) * self.heights.get(entry).height;
        }
        Ok(())
    }

    pub fn scroll_to_end(&mut self) {
        self.at_end = true;
    }

    /// The frontend found where the viewport's top falls in the anchor's
    /// Block: in the line starting at `byte`, whose top is `line_top` into
    /// the Block. Nothing moves.
    pub fn set_anchor_text(&mut self, byte: u32, line_top: f32) {
        let Some(anchor) = &mut self.anchor else { return };
        let Position::Text { byte: b, line_offset } = &mut anchor.position else { return };
        let depth = self.line_top + *line_offset;
        *b = byte;
        *line_offset = depth - line_top;
        self.line_top = line_top;
    }

    /// The anchor's Block was laid out again, and its anchored line's top
    /// is now `line_top` into it: keep that line where it was.
    pub fn relayout_anchor(&mut self, line_top: f32) {
        self.line_top = line_top;
    }

    /// On a width change every measured height becomes an estimate, scaled
    /// by the width ratio; its width tag says which width it was measured at.
    pub fn set_metrics(&mut self, doc: &Tree, metrics: Metrics) {
        let old = self.metrics;
        self.metrics = metrics;
        self.tag = self.tag_for(metrics.width);
        let scale = if old.width > 0.0 && metrics.width > 0.0 { old.width / metrics.width } else { 1.0 };
        let (entries, tag) = (doc.entries(), self.tag);
        self.heights.update_all(|i, row| {
            let next = if row.width == 0 {
                Row::estimated(estimate(doc, entries[i], &metrics), 0)
            } else if old.width != metrics.width {
                let mut scaled = row.stale();
                scaled.height *= scale;
                if scaled.width == tag {
                    // Back at the width it was measured at.
                    scaled = Row::measured(scaled.height, scaled.width);
                }
                scaled
            } else {
                row
            };
            next.with_hidden(row.hidden())
        });
        if old.width != metrics.width {
            self.line_top *= scale;
        }
    }

    /// A height measured at another width than the current one is kept as a
    /// scaled estimate.
    pub fn set_height(&mut self, doc: &Tree, key: Key, part: Part, height: f32, width: f32) -> Result<(), String> {
        let entry = doc.entry_of(key, part).ok_or("no such entry")?;
        let tag = self.tag_for(width);
        let old = self.heights.get(entry);
        let row = if width == self.metrics.width {
            Row::measured(height, tag)
        } else {
            let scale = if self.metrics.width > 0.0 { width / self.metrics.width } else { 1.0 };
            Row::estimated(height * scale, tag)
        };
        if self.anchor.is_some_and(|a| a.key == key && a.part == part) && old.height > 0.0 {
            // Until the frontend says where the anchored line went.
            self.line_top *= row.height / old.height;
        }
        self.heights.set(entry, row.with_hidden(old.hidden()));
        Ok(())
    }

    /// A collapsed Container's rows after its header count as zero.
    /// Expanding one in view turns follow-tail off and anchors on it.
    pub fn set_collapsed(&mut self, doc: &Tree, key: Key, collapsed: bool) -> Result<(), String> {
        let id = doc.get(key).ok_or("no such node")?;
        if doc.kind(id).is_block() || doc.parent(id).is_none() {
            return Err("only a Container below the root collapses".into());
        }
        let changed = if collapsed { self.collapsed.insert(key) } else { self.collapsed.remove(&key) };
        if !changed {
            return Ok(());
        }
        let header = doc.entry_of(key, Part::Header).ok_or("no such entry")?;
        let top = self.top(doc);
        let at = self.heights.offset_of(header);
        let in_view = at >= top && at < top + self.viewport;
        self.rehide(doc, id);
        if !collapsed && in_view {
            self.at_end = false;
            let anchor = Anchor { key, part: Part::Header, position: Position::Fraction(0.0), viewport_offset: at - top };
            self.anchor = Some(anchor);
        }
        Ok(())
    }

    fn rehide(&mut self, doc: &Tree, id: NodeId) {
        let (first, end) = doc.entry_range(id);
        for i in first..end {
            let e = doc.entries()[i];
            let row = self.heights.get(i);
            let hidden = self.hidden(doc, e);
            if row.hidden() != hidden {
                self.heights.set(i, row.with_hidden(hidden));
            }
        }
    }

    pub(crate) fn expand_around(&mut self, doc: &Tree, key: Key) {
        let Some(mut id) = doc.get(key) else { return };
        let mut outermost = None;
        while let Some(parent) = doc.parent(id) {
            if self.collapsed.remove(&doc.key(parent)) {
                outermost = Some(parent);
            }
            id = parent;
        }
        if let Some(outer) = outermost {
            self.rehide(doc, outer);
        }
    }

    /// Expand what hides it, and anchor it a third of the way down the
    /// viewport. Breaks follow-tail.
    pub fn reveal(&mut self, doc: &Tree, key: Key, byte: u32) -> Result<(), String> {
        let id = doc.get(key).ok_or("no such node")?;
        self.expand_around(doc, key);
        let part = if doc.kind(id).is_block() { Part::Block } else { Part::Header };
        let position = if part == Part::Block && doc.kind(id) != Kind::Embed {
            Position::Text { byte: byte.min(doc.text(id).len() as u32), line_offset: 0.0 }
        } else {
            Position::Fraction(0.0)
        };
        self.scroll_to(doc, Anchor { key, part, position, viewport_offset: self.viewport / 3.0 })
    }

    pub fn window(&self, doc: &Tree) -> Window {
        let top = self.top(doc);
        let total = self.heights.total();
        let overscan = self.viewport * OVERSCAN;
        let start = (top - overscan).max(0.0);
        let end = top + self.viewport + overscan;
        let mut entries = vec![];
        if let Some((first, _)) = self.heights.entry_at(start) {
            let mut at = self.heights.offset_of(first);
            for i in first..self.heights.len() {
                if at >= end {
                    break;
                }
                let row = self.heights.get(i);
                if !row.hidden() {
                    let e = doc.entries()[i];
                    entries.push(WindowEntry {
                        entry: i,
                        key: doc.key(e.node),
                        part: e.part,
                        top: at,
                        height: row.height,
                        measured: row.is_measured(),
                    });
                }
                at += row.size();
            }
        }
        Window { top, viewport: self.viewport, total, at_end: self.at_end, entries }
    }

    pub fn thumb(&self, doc: &Tree) -> Thumb {
        let total = self.heights.total();
        let estimated = if total > 0.0 { self.heights.estimated_total() / total } else { 0.0 };
        if total <= self.viewport || total <= 0.0 {
            return Thumb { start: 0.0, size: 1.0, estimated };
        }
        Thumb { start: self.top(doc) / total, size: self.viewport / total, estimated }
    }
}
