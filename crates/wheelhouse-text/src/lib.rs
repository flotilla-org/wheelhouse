//! Native paragraph layout (#559): parley shapes, applies bidi, breaks lines
//! and places inline boxes, and fontique finds fonts and their fallbacks.
//! A layout is copied out into flat `repr(C)` arrays (lines, runs, glyphs,
//! clusters and boxes), which wheelhouse-ffi writes into C memory, and the
//! queries in [`query`] read. Native-only: it stays out of wheelhouse-core,
//! which builds for wasm.
//!
//! Positions are in pixels: sizes in points times the context's scale, x
//! from the paragraph's left and y down from its top.
mod fonts;
mod layout;
pub mod query;

pub use fonts::{Family, FontFace, FontSource, TextContext, DEFAULT_FALLBACKS, DEFAULT_FAMILIES};
pub use layout::{Error, Feature, InlineBox, Input, Style, StyleRun, ELLIPSIS};
pub use query::{caret_rect, offset_at, segments, selection_rects, Affinity, Caret, Position, Rect, RunId, Segment};

/// The cluster's text runs right to left.
pub const CLUSTER_RTL: u32 = 1;
/// The cluster is a space, a no-break space, or a hard line break.
pub const CLUSTER_WHITESPACE: u32 = 2;
/// A word starts at the cluster.
pub const CLUSTER_WORD_START: u32 = 4;
/// The cluster continues a ligature that an earlier cluster starts: it has
/// no glyphs of its own, and its advance is its share of the ligature's.
pub const CLUSTER_LIGATURE_CONTINUATION: u32 = 8;
/// The cluster is a hard line break.
pub const CLUSTER_HARD_BREAK: u32 = 16;
/// The cluster is the ellipsis a cut line ends with: it covers no bytes, and
/// its start is where the text was cut.
pub const CLUSTER_ELLIPSIS: u32 = 32;

/// A line, top to bottom.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Line {
    /// The line box's top.
    pub y: f32,
    pub height: f32,
    /// The baseline's y.
    pub baseline: f32,
    /// The content's extent above and below the baseline.
    pub ascent: f32,
    pub descent: f32,
    /// The advance of its content, without trailing whitespace.
    pub width: f32,
    /// The bytes it holds, with any trailing whitespace and line break.
    pub text_start: u32,
    pub text_end: u32,
    /// Its clusters, left to right.
    pub cluster_start: u32,
    pub cluster_count: u32,
    /// Its runs, left to right.
    pub run_start: u32,
    pub run_count: u32,
}

/// Glyphs of one face, one size and one style, on one line.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Run {
    /// The face: an id the context's font table resolves to a file path
    /// or registered bytes.
    pub font: u32,
    /// The face's index within its file.
    pub face_index: u32,
    /// In pixels.
    pub size: f32,
    /// Synthesise bold: the family has no face that heavy.
    pub embolden: u32,
    /// Synthesise italic: skew by this many degrees (0 for none).
    pub skew: f32,
    /// Normalized variation coordinates: an id the context's table
    /// resolves (0 for none).
    pub coords: u32,
    /// The input style it takes.
    pub style: u32,
    pub glyph_start: u32,
    pub glyph_count: u32,
    /// Its origin: the left of its first glyph, on its baseline.
    pub x: f32,
    pub y: f32,
    pub advance: f32,
    pub line: u32,
    pub rtl: u32,
}

/// A glyph, at its origin on the baseline.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Glyph {
    pub id: u32,
    pub x: f32,
    pub y: f32,
}

/// One extended grapheme cluster.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Cluster {
    pub start: u32,
    pub len: u32,
    /// Its left edge.
    pub x: f32,
    pub advance: f32,
    pub line: u32,
    /// The input style it takes.
    pub style: u32,
    /// CLUSTER_* bits (WH_TEXT_CLUSTER_* in C).
    pub flags: u32,
}

/// Where an inline box goes: its top left.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PlacedBox {
    pub id: u64,
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
    pub line: u32,
}

/// A laid-out paragraph's arrays.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Paragraph {
    pub lines: Vec<Line>,
    pub runs: Vec<Run>,
    pub glyphs: Vec<Glyph>,
    pub clusters: Vec<Cluster>,
    pub boxes: Vec<PlacedBox>,
    /// The widest line's width.
    pub width: f32,
    pub height: f32,
    /// The context's font epoch it was laid out at.
    pub epoch: u64,
}

impl Paragraph {
    pub fn view(&self) -> View<'_> {
        View { lines: &self.lines, runs: &self.runs, glyphs: &self.glyphs, clusters: &self.clusters, boxes: &self.boxes }
    }

    fn clear(&mut self) {
        self.lines.clear();
        self.runs.clear();
        self.glyphs.clear();
        self.clusters.clear();
        self.boxes.clear();
        self.width = 0.0;
        self.height = 0.0;
        self.epoch = 0;
    }
}

/// A layout's arrays, wherever they live.
#[derive(Clone, Copy, Debug)]
pub struct View<'a> {
    pub lines: &'a [Line],
    pub runs: &'a [Run],
    pub glyphs: &'a [Glyph],
    pub clusters: &'a [Cluster],
    pub boxes: &'a [PlacedBox],
}
