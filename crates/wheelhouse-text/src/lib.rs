//! Native-only: wheelhouse-core builds for wasm, so it can't hold this.
//! Positions are in pixels, x from the paragraph's left and y down from its top.
mod fonts;
mod layout;
pub mod query;

pub use fonts::{Family, FontFace, FontSource, TextContext, DEFAULT_FALLBACKS, DEFAULT_FAMILIES};
pub use layout::{Error, Feature, InlineBox, Input, Style, StyleRun, ELLIPSIS};
pub use query::{caret_rect, offset_at, segments, selection_rects, Affinity, Caret, Position, Rect, RunId, Segment};

pub const CLUSTER_RTL: u32 = 1;
/// A space, a no-break space, or a hard line break.
pub const CLUSTER_WHITESPACE: u32 = 2;
pub const CLUSTER_WORD_START: u32 = 4;
/// No glyphs of its own; its advance is its share of the ligature's.
pub const CLUSTER_LIGATURE_CONTINUATION: u32 = 8;
pub const CLUSTER_HARD_BREAK: u32 = 16;
/// Covers no bytes; its start is where the text was cut.
pub const CLUSTER_ELLIPSIS: u32 = 32;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Line {
    /// The line box's top.
    pub y: f32,
    pub height: f32,
    pub baseline: f32,
    pub ascent: f32,
    pub descent: f32,
    /// Without trailing whitespace.
    pub width: f32,
    /// With any trailing whitespace and line break.
    pub text_start: u32,
    pub text_end: u32,
    /// Left to right.
    pub cluster_start: u32,
    pub cluster_count: u32,
    /// Left to right.
    pub run_start: u32,
    pub run_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Run {
    /// Resolved by wh_text_font.
    pub font: u32,
    pub face_index: u32,
    pub size: f32,
    /// Synthesise bold.
    pub embolden: u32,
    /// Synthetic italic skew in degrees.
    pub skew: f32,
    /// Resolved by wh_text_coords; 0 for none.
    pub coords: u32,
    pub style: u32,
    pub glyph_start: u32,
    pub glyph_count: u32,
    /// On the baseline.
    pub x: f32,
    pub y: f32,
    pub advance: f32,
    pub line: u32,
    pub rtl: u32,
}

/// At its origin on the baseline.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Glyph {
    pub id: u32,
    pub x: f32,
    pub y: f32,
}

/// An extended grapheme cluster.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Cluster {
    pub start: u32,
    pub len: u32,
    /// Left edge.
    pub x: f32,
    pub advance: f32,
    pub line: u32,
    pub style: u32,
    /// WH_TEXT_CLUSTER_* bits.
    pub flags: u32,
}

/// x and y are the top left.
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

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Paragraph {
    pub lines: Vec<Line>,
    pub runs: Vec<Run>,
    pub glyphs: Vec<Glyph>,
    pub clusters: Vec<Cluster>,
    pub boxes: Vec<PlacedBox>,
    pub width: f32,
    pub height: f32,
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

#[derive(Clone, Copy, Debug)]
pub struct View<'a> {
    pub lines: &'a [Line],
    pub runs: &'a [Run],
    pub glyphs: &'a [Glyph],
    pub clusters: &'a [Cluster],
    pub boxes: &'a [PlacedBox],
}
