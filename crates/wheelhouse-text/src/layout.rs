use std::{
    borrow::Cow,
    collections::HashMap,
    error,
    fmt::{self, Display, Formatter},
    ops::Range,
    sync::MutexGuard,
};

use parley::{
    setting::Tag, Alignment, AlignmentOptions, BaseDirection, Cluster as ParleyCluster, FontContext, FontFamily, FontFeature, FontFeatures,
    FontStyle, FontWeight, InlineBox as ParleyBox, InlineBoxKind, Layout, LayoutContext, OverflowWrap, PositionedLayoutItem, TextStyle,
    VerticalAlign,
};

use crate::{
    fonts::{Family, Local, State, TextContext},
    Cluster, Glyph, Line, Paragraph, PlacedBox, Run, CLUSTER_ELLIPSIS, CLUSTER_HARD_BREAK, CLUSTER_LIGATURE_CONTINUATION, CLUSTER_RTL,
    CLUSTER_TAB, CLUSTER_WHITESPACE, CLUSTER_WORD_START,
};

pub const ELLIPSIS: &str = "\u{2026}";

#[derive(Clone, Debug, PartialEq)]
pub struct Style<'a> {
    pub family: Family,
    /// In points; the context's scale makes them pixels.
    pub size: f32,
    /// CSS weight.
    pub weight: f32,
    pub italic: bool,
    pub features: &'a [Feature],
    /// In points.
    pub letter_spacing: f32,
}

impl Default for Style<'_> {
    fn default() -> Self {
        Self { family: Family::Main, size: 16.0, weight: 400.0, italic: false, features: &[], letter_spacing: 0.0 }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Feature {
    pub tag: [u8; 4],
    pub value: u16,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StyleRun {
    pub start: usize,
    pub len: usize,
    pub style: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct InlineBox {
    pub id: u64,
    pub offset: usize,
    pub width: f32,
    pub height: f32,
    /// The box's baseline, down from its top; `height` sits it on the
    /// text's baseline.
    pub baseline: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Align {
    #[default]
    Start,
    End,
    Center,
    /// The last line, and lines that end at a hard break, align to the start.
    Justify,
}

/// Bytes no run covers take style 0.
#[derive(Clone, Copy, Debug)]
pub struct Input<'a> {
    pub text: &'a str,
    /// In order and not overlapping.
    pub runs: &'a [StyleRun],
    pub styles: &'a [Style<'a>],
    /// In order of offset.
    pub boxes: &'a [InlineBox],
    /// 0 for no limit.
    pub max_lines: usize,
    pub ellipsis: bool,
    pub align: Align,
    /// Tab stops every this many spaces of the tab's own style; 0 for 4.
    pub tab_size: u32,
}

impl<'a> Input<'a> {
    pub fn plain(text: &'a str, styles: &'a [Style<'a>]) -> Self {
        Self { text, runs: &[], styles, boxes: &[], max_lines: 0, ellipsis: false, align: Align::Start, tab_size: 0 }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Error {
    NoStyles,
    UnknownStyle(usize),
    RunOutOfBounds(usize),
    RunsOverlap(usize),
    BoxOutOfBounds(usize),
    BadSize(usize),
    BadStyle(usize),
    BadBox(usize),
    BadWidth,
    TextTooLong,
    TooManyStyles,
}

impl Display for Error {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        match self {
            Error::NoStyles => write!(f, "the style table is empty"),
            Error::UnknownStyle(i) => write!(f, "run {i} names a style past the table"),
            Error::RunOutOfBounds(i) => write!(f, "run {i} ends past the text or inside a character"),
            Error::RunsOverlap(i) => write!(f, "run {i} starts before the run before it ends"),
            Error::BoxOutOfBounds(i) => write!(f, "inline box {i} is past the text, inside a character, or out of order"),
            Error::BadSize(i) => write!(f, "style {i} has a size that is not positive and finite"),
            Error::BadStyle(i) => {
                write!(f, "style {i} has a weight or letter spacing that is not finite, or a weight that is not positive")
            }
            Error::BadBox(i) => write!(f, "inline box {i} has a size or baseline that is not finite, or a negative size"),
            Error::BadWidth => write!(f, "the width is NaN"),
            Error::TextTooLong => write!(f, "the text is longer than u32::MAX bytes"),
            Error::TooManyStyles => write!(f, "the style table has more than 65536 styles"),
        }
    }
}

impl error::Error for Error {}

impl TextContext {
    pub fn layout(&self, input: &Input<'_>, width: f32) -> Result<Paragraph, Error> {
        let mut out = Paragraph::default();
        self.layout_into(input, width, &mut out)?;
        Ok(out)
    }

    /// Reuses `out`'s arrays, and leaves it empty on error. Pass `f32::INFINITY`
    /// for no wrapping: a large finite width aligns RTL or centred lines
    /// against that width, far from 0, where f32 loses precision.
    pub fn layout_into(&self, input: &Input<'_>, width: f32, out: &mut Paragraph) -> Result<(), Error> {
        out.clear();
        let mut runs = validate(input, width)?;
        out.epoch = self.epoch();
        let (stacks, scale) = {
            let state = self.lock();
            (Family::ALL.map(|f| state.family_stack(f)), state.scale)
        };
        let mut styles = input.styles.iter().map(|s| text_style(s, &stacks[s.family as usize])).collect::<Vec<_>>();
        let mut origin = (0..styles.len() as u32).collect::<Vec<_>>();
        let unbounded = !width.is_finite();
        let width = if unbounded { f32::MAX } else { width.max(0.0) };
        // Tabs shape as spaces (one byte each, so offsets hold), widened below.
        let tabs = input.text.contains('\t');
        let text = if tabs { Cow::Owned(input.text.replace('\t', " ")) } else { Cow::Borrowed(input.text) };
        let text = text.as_ref();
        let align = match input.align {
            Align::Start => Alignment::Start,
            Align::End => Alignment::End,
            Align::Center => Alignment::Center,
            Align::Justify => Alignment::Justify,
        };
        self.with_local(|local| {
            let Local { fonts, layout: lcx, paragraph, ellipsis, .. } = local;
            if tabs {
                let shape = Shape { text, runs: &runs, styles: &styles, boxes: input.boxes, base: BaseDirection::Auto, scale };
                shape.build(lcx, fonts, paragraph);
                paragraph.break_all_lines(None);
                let tab_size = if input.tab_size == 0 { 4 } else { input.tab_size } as f32;
                let tabs = tab_widths(paragraph, input.text, input.boxes, tab_size, scale);
                widen_tabs(&tabs, scale, &mut runs, &mut styles, &mut origin);
            }
            let shape = Shape { text, runs: &runs, styles: &styles, boxes: input.boxes, base: BaseDirection::Auto, scale };
            shape.build(lcx, fonts, paragraph);
            // With a limit, one line past it says whether anything was cut.
            break_lines(paragraph, width, if input.max_lines > 0 { input.max_lines + 1 } else { usize::MAX });
            paragraph.align(align, AlignmentOptions::default());
            out.rtl = paragraph.is_rtl();
            let count = paragraph.len();
            let kept = if input.max_lines > 0 && input.max_lines < count { input.max_lines } else { count };
            // Lines dropped hold text or boxes (a line of boxes alone has no
            // text range), not just the empty line after a break.
            let cut = paragraph.lines().skip(kept).any(|l| l.text_range().start != l.text_range().end);
            let ellipsis_cut = cut && input.ellipsis;
            let full = if ellipsis_cut { kept - 1 } else { kept };
            let cut_at = ellipsis_cut.then(|| {
                let start = text_end(paragraph, full);
                let base = if paragraph.is_rtl() { BaseDirection::Rtl } else { BaseDirection::Ltr };
                let shape = Shaper { lcx: &mut *lcx, fonts: &mut *fonts, styles: &styles, base, scale };
                (start, cut_line(shape, paragraph, ellipsis, text, &runs, input.boxes, start, full, width))
            });
            // Unbounded, parley aligns against the widest line it broke, which
            // may be one that was dropped: align against the kept lines instead.
            let (mut dx, mut dx_ellipsis) = (0.0, 0.0);
            if unbounded && kept < count {
                let kept_width =
                    paragraph.lines().take(full).map(|l| l.metrics().advance - l.metrics().hanging_advance).fold(0.0, f32::max);
                let ellipsis_width = if cut_at.is_some() { ellipsis.width() } else { 0.0 };
                let target = kept_width.max(ellipsis_width);
                let k = match (input.align, paragraph.is_rtl()) {
                    (Align::Center, _) => 0.5,
                    (Align::End, false) | (Align::Start | Align::Justify, true) => 1.0,
                    _ => 0.0,
                };
                dx = k * (target - paragraph.width());
                dx_ellipsis = k * (target - ellipsis_width);
            }
            let mut emit =
                Emit { state: self.lock(), fonts, out, source: input.text, origin: &origin, shift: 0, real: usize::MAX, dx, y: 0.0 };
            emit.lines(paragraph, 0..full);
            if let Some((start, real)) = cut_at {
                let y = paragraph.get(full).expect("a kept line").metrics().block_min_coord;
                emit.shift = start;
                emit.real = real;
                emit.dx = dx_ellipsis;
                emit.y = y;
                emit.lines(ellipsis, 0..1);
            }
        });
        out.height = out.lines.last().map_or(0.0, |l| l.y + l.height);
        out.width = out.lines.iter().fold(0.0, |w, l| w.max(l.width));
        Ok(())
    }
}

/// Breaks at most `limit` lines.
fn break_lines(layout: &mut Layout<()>, width: f32, limit: usize) {
    let mut breaker = layout.break_lines();
    breaker.state_mut().set_layout_max_advance(width);
    breaker.state_mut().set_line_max_advance(width);
    let mut lines = 0;
    while lines < limit && breaker.break_next().is_some() {
        lines += 1;
    }
    breaker.finish();
}

/// Where the text of the layout's first `lines` lines ends; a line of boxes
/// alone has no text range of its own.
fn text_end(layout: &Layout<()>, lines: usize) -> usize {
    layout.lines().take(lines).map(|l| l.text_range()).rfind(|r| r.start <= r.end).map_or(0, |r| r.end.min(layout.text_len()))
}

struct Shaper<'s, 'a> {
    lcx: &'s mut LayoutContext<()>,
    fonts: &'s mut FontContext,
    styles: &'s [TextStyle<'static, 'a, ()>],
    base: BaseDirection,
    scale: f32,
}

/// Lays out line `line`, cut to fit with an ellipsis, into `into`, starting at
/// byte `start`. Returns where its text ends (the ellipsis starts) relative to `start`.
#[allow(clippy::too_many_arguments)]
fn cut_line(
    s: Shaper<'_, '_>,
    layout: &Layout<()>,
    into: &mut Layout<()>,
    text: &str,
    runs: &[StyleRun],
    boxes: &[InlineBox],
    start: usize,
    line: usize,
    width: f32,
) -> usize {
    let candidates = cut_candidates(layout, line, boxes, s.scale);
    let ellipsis_advance = {
        let style = style_at(runs, candidates.last().map_or(start, |c| c.0).saturating_sub(1).max(start));
        let e_runs = [StyleRun { start: 0, len: ELLIPSIS.len(), style }];
        Shape { text: ELLIPSIS, runs: &e_runs, styles: s.styles, boxes: &[], base: s.base, scale: s.scale }.build(s.lcx, s.fonts, into);
        into.break_all_lines(None);
        into.get(0).map_or(0.0, |l| l.metrics().advance)
    };
    // The longest prefix that fits with the ellipsis, then shorter ones while
    // reshaping says it doesn't.
    let mut k = candidates.iter().rposition(|c| c.1 + ellipsis_advance <= width).map_or(0, |i| i + 1);
    let mut cut_text = String::new();
    let mut cut_runs = vec![];
    let mut cut_boxes = vec![];
    loop {
        let cut = trim_trailing_space(text, start, if k == 0 { start } else { candidates[k - 1].0 });
        cut_text.clear();
        cut_text.push_str(&text[start..cut]);
        cut_text.push_str(ELLIPSIS);
        cut_runs.clear();
        clip_runs(runs, start, cut, &mut cut_runs);
        cut_boxes.clear();
        cut_boxes
            .extend(boxes.iter().filter(|b| b.offset >= start && b.offset < cut).map(|b| InlineBox { offset: b.offset - start, ..*b }));
        let shape = Shape { text: &cut_text, runs: &cut_runs, styles: s.styles, boxes: &cut_boxes, base: s.base, scale: s.scale };
        shape.build(s.lcx, s.fonts, into);
        // Broken at the width, so it aligns as the lines above do.
        into.break_all_lines(Some(width));
        into.align(layout.alignment().unwrap_or_default(), AlignmentOptions::default());
        let fits = into.len() == 1 && into.get(0).is_some_and(|l| l.metrics().advance - l.metrics().hanging_advance <= width);
        if fits || k == 0 {
            return cut - start;
        }
        k -= 1;
    }
}

/// Each tab's byte and the width it takes, from a layout broken only at hard
/// breaks: stops are measured from the start of the text's line, so a soft
/// wrap before a tab doesn't move its stop.
fn tab_widths(layout: &Layout<()>, text: &str, boxes: &[InlineBox], tab_size: f32, scale: f32) -> Vec<(usize, f32, f32)> {
    let mut tabs = vec![];
    for line in layout.lines() {
        let mut clusters =
            line.runs().flat_map(|run| run.clusters().map(|c| (c.text_range().start, c.advance())).collect::<Vec<_>>()).collect::<Vec<_>>();
        clusters.sort_by_key(|c| c.0);
        let mut boxes = boxes.iter().filter(|b| line.text_range().contains(&b.offset)).peekable();
        let mut x = 0.0;
        for (start, advance) in clusters {
            while let Some(b) = boxes.next_if(|b| b.offset <= start) {
                x += b.width * scale;
            }
            if text.as_bytes().get(start) != Some(&b'\t') || advance <= 0.0 {
                x += advance;
                continue;
            }
            // A stop closer than half a space moves to the next (CSS Text 3 § 4.2).
            let stop = tab_size * advance;
            let mut w = stop - x % stop;
            if w < advance / 2.0 {
                w += stop;
            }
            tabs.push((start, advance, w));
            x += w;
        }
    }
    tabs
}

/// Gives each tab a run of its own, in a copy of its style whose word spacing
/// takes it to its width. `origin` maps the styles added back to the input's.
fn widen_tabs(
    tabs: &[(usize, f32, f32)],
    scale: f32,
    runs: &mut Vec<StyleRun>,
    styles: &mut Vec<TextStyle<'static, '_, ()>>,
    origin: &mut Vec<u32>,
) {
    let mut added = HashMap::new();
    let mut split = Vec::with_capacity(runs.len() + 2 * tabs.len());
    let mut tabs = tabs.iter().peekable();
    for &run in runs.iter() {
        let end = run.start + run.len;
        let mut at = run.start;
        while let Some(&(byte, advance, w)) = tabs.next_if(|t| t.0 < end) {
            // parley takes style indices as u16; past that, a tab stays one space wide.
            let key = (run.style, ((w - advance) * 64.0).round() as i64);
            let style = match added.get(&key) {
                Some(&style) => style,
                None if styles.len() <= u16::MAX as usize => {
                    let mut widened = styles[run.style].clone();
                    widened.word_spacing += (w - advance) / scale;
                    styles.push(widened);
                    origin.push(origin[run.style]);
                    added.insert(key, styles.len() - 1);
                    styles.len() - 1
                }
                None => run.style,
            };
            if byte > at {
                split.push(StyleRun { start: at, len: byte - at, style: run.style });
            }
            split.push(StyleRun { start: byte, len: 1, style });
            at = byte + 1;
        }
        if end > at {
            split.push(StyleRun { start: at, len: end - at, style: run.style });
        }
    }
    *runs = split;
}

/// In logical order up to any hard break: each cluster's end byte, and the
/// line's advance up to it.
fn cut_candidates(layout: &Layout<()>, line: usize, boxes: &[InlineBox], scale: f32) -> Vec<(usize, f32)> {
    let line = layout.get(line).expect("a line");
    let mut ends = line
        .runs()
        .flat_map(|run| run.clusters().map(|c| (c.text_range(), c.advance(), c.is_hard_line_break())).collect::<Vec<_>>())
        .collect::<Vec<_>>();
    ends.sort_by_key(|(range, ..)| range.start);
    let mut boxes = boxes.iter().filter(|b| line.text_range().contains(&b.offset)).peekable();
    let mut advance = 0.0;
    ends.into_iter()
        .take_while(|&(.., hard_break)| !hard_break)
        .map(|(range, a, _)| {
            while let Some(b) = boxes.next_if(|b| b.offset <= range.start) {
                advance += b.width * scale;
            }
            advance += a;
            (range.end, advance)
        })
        .collect()
}

/// Some fonts (the icon font among them) give a thickness of 0: use parley's
/// default for a font that gives none, HarfBuzz's size / 18.
fn thickness(size: f32, font_size: f32) -> f32 {
    if size > 0.0 {
        size
    } else {
        font_size / 18.0
    }
}

fn trim_trailing_space(text: &str, start: usize, cut: usize) -> usize {
    start + text[start..cut].trim_end().len()
}

fn style_at(runs: &[StyleRun], byte: usize) -> usize {
    runs.iter().find(|r| byte < r.start + r.len).or(runs.last()).map_or(0, |r| r.style)
}

fn clip_runs(runs: &[StyleRun], start: usize, cut: usize, out: &mut Vec<StyleRun>) {
    for r in runs {
        let (s, e) = (r.start.max(start), (r.start + r.len).min(cut));
        if s < e {
            out.push(StyleRun { start: s - start, len: e - s, style: r.style });
        }
    }
    let style = style_at(runs, cut.saturating_sub(1).max(start));
    out.push(StyleRun { start: cut - start, len: ELLIPSIS.len(), style });
}

/// Returns runs that cover the text exactly.
fn validate(input: &Input<'_>, width: f32) -> Result<Vec<StyleRun>, Error> {
    if width.is_nan() {
        return Err(Error::BadWidth);
    }
    if input.text.len() > u32::MAX as usize {
        return Err(Error::TextTooLong);
    }
    if input.styles.is_empty() {
        return Err(Error::NoStyles);
    }
    // parley takes style indices as u16.
    if input.styles.len() > u16::MAX as usize + 1 {
        return Err(Error::TooManyStyles);
    }
    if let Some(i) = input.styles.iter().position(|s| !(s.size.is_finite() && s.size > 0.0)) {
        return Err(Error::BadSize(i));
    }
    if let Some(i) = input.styles.iter().position(|s| !(s.weight.is_finite() && s.weight > 0.0 && s.letter_spacing.is_finite())) {
        return Err(Error::BadStyle(i));
    }
    if let Some(i) = input
        .boxes
        .iter()
        .position(|b| !(b.width.is_finite() && b.height.is_finite() && b.baseline.is_finite() && b.width >= 0.0 && b.height >= 0.0))
    {
        return Err(Error::BadBox(i));
    }
    let text = input.text;
    let mut runs = Vec::with_capacity(input.runs.len() + 1);
    let mut at = 0;
    for (i, r) in input.runs.iter().enumerate() {
        let end = r.start.checked_add(r.len).ok_or(Error::RunOutOfBounds(i))?;
        if end > text.len() || !text.is_char_boundary(r.start) || !text.is_char_boundary(end) {
            return Err(Error::RunOutOfBounds(i));
        }
        if r.start < at {
            return Err(Error::RunsOverlap(i));
        }
        if r.style >= input.styles.len() {
            return Err(Error::UnknownStyle(i));
        }
        if r.len == 0 {
            continue;
        }
        if r.start > at {
            runs.push(StyleRun { start: at, len: r.start - at, style: 0 });
        }
        runs.push(*r);
        at = end;
    }
    if at < text.len() || runs.is_empty() {
        runs.push(StyleRun { start: at, len: text.len() - at, style: 0 });
    }
    let mut last = 0;
    for (i, b) in input.boxes.iter().enumerate() {
        if b.offset > text.len() || !text.is_char_boundary(b.offset) || b.offset < last {
            return Err(Error::BoxOutOfBounds(i));
        }
        last = b.offset;
    }
    Ok(runs)
}

fn text_style<'a>(style: &Style<'a>, family: &FontFamily<'static>) -> TextStyle<'static, 'a, ()> {
    TextStyle {
        font_family: family.clone(),
        font_size: style.size,
        font_weight: FontWeight::new(style.weight),
        font_style: if style.italic { FontStyle::Italic } else { FontStyle::Normal },
        font_features: FontFeatures::List(Cow::Owned(
            style.features.iter().map(|f| FontFeature { tag: Tag::new(&f.tag), value: f.value }).collect(),
        )),
        letter_spacing: style.letter_spacing,
        // A word wider than the line breaks between graphemes.
        overflow_wrap: OverflowWrap::Anywhere,
        ..TextStyle::default()
    }
}

struct Shape<'s, 'a> {
    text: &'s str,
    runs: &'s [StyleRun],
    styles: &'s [TextStyle<'static, 'a, ()>],
    boxes: &'s [InlineBox],
    base: BaseDirection,
    scale: f32,
}

impl Shape<'_, '_> {
    fn build(&self, lcx: &mut LayoutContext<()>, fonts: &mut FontContext, into: &mut Layout<()>) {
        let mut builder = lcx.style_run_builder(fonts, self.text, self.scale, true);
        builder.reserve(self.styles.len(), self.runs.len());
        for style in self.styles {
            builder.push_style(style.clone());
        }
        for run in self.runs {
            builder.push_style_run(run.style as u16, run.start..run.start + run.len);
        }
        for b in self.boxes {
            builder.push_inline_box(ParleyBox {
                id: b.id,
                kind: InlineBoxKind::InFlow,
                index: b.offset,
                width: b.width * self.scale,
                height: b.height * self.scale,
                baseline: Some(b.baseline * self.scale),
                vertical_align: VerticalAlign::BASELINE,
            });
        }
        builder.set_base_direction(self.base);
        builder.build_into(into, self.text);
    }
}

/// Bytes of the layout's text map to `shift` more in `source`; bytes from
/// `real` on are the ellipsis. `origin` maps style indices to the input's.
struct Emit<'s> {
    state: MutexGuard<'s, State>,
    fonts: &'s mut FontContext,
    out: &'s mut Paragraph,
    source: &'s str,
    origin: &'s [u32],
    shift: usize,
    real: usize,
    dx: f32,
    y: f32,
}

impl Emit<'_> {
    fn lines(&mut self, layout: &Layout<()>, lines: Range<usize>) {
        let mut last_font = None;
        for index in lines {
            let line = layout.get(index).expect("a line in range");
            let m = line.metrics();
            let out_line = self.out.lines.len() as u32;
            let (cluster_start, run_start) = (self.out.clusters.len() as u32, self.out.runs.len() as u32);
            let boxes_start = self.out.boxes.len();
            // Glyph runs follow the clusters' visual order, so each cluster's
            // glyphs come next.
            let mut glyph = self.out.glyphs.len() as u32;
            let dx = self.dx;
            // Clusters come from every run, glyphs or not; the items between
            // runs are the inline boxes.
            let mut boxes = line.items().filter_map(|item| match item {
                PositionedLayoutItem::InlineBox(b) => Some(b),
                PositionedLayoutItem::GlyphRun(_) => None,
            });
            let line_x = m.offset + m.inline_min_coord + dx;
            let mut x = line_x;
            let mut item = 0;
            let mut dropped = 0.0;
            // Unlike the line's metrics, this includes justification.
            let mut advance = 0.0;
            for run in line.runs().map(Some).chain([None]) {
                let next = run.as_ref().map_or(usize::MAX, |r| r.index());
                while item < next {
                    let Some(b) = boxes.next() else { break };
                    self.out.boxes.push(PlacedBox {
                        id: b.id,
                        x: b.x + dx,
                        y: b.y + self.y,
                        width: b.width,
                        height: b.height,
                        line: out_line,
                    });
                    x = b.x + dx + b.width;
                    advance += b.width;
                    item += 1;
                }
                let Some(run) = run else { break };
                item = next + 1;
                // parley draws an atom (a grapheme, or a ligature's) with no
                // glyphs as nothing, whatever its advance.
                let mut glyphless = false;
                for c in run.visual_clusters() {
                    if !c.is_ligature_continuation() {
                        glyphless = c.glyphs().next().is_none();
                    }
                    if glyphless {
                        dropped += c.advance();
                    }
                    advance += c.advance();
                    x += self.cluster(&c, x, glyphless, out_line, &mut glyph);
                }
            }
            for item in line.items() {
                if let PositionedLayoutItem::GlyphRun(glyph_run) = item {
                    let run = glyph_run.run();
                    let font = run.font();
                    let key = (font.data.id(), font.index);
                    let font_id = match last_font {
                        Some((k, id)) if k == key => id,
                        _ => {
                            let id = self.state.face_id(font, self.fonts);
                            last_font = Some((key, id));
                            id
                        }
                    };
                    let synthesis = run.synthesis();
                    let glyph_start = self.out.glyphs.len() as u32;
                    let y = self.y;
                    self.out.glyphs.extend(glyph_run.positioned_glyphs().map(|g| Glyph { id: g.id, x: g.x + dx, y: g.y + y }));
                    let metrics = run.font_metrics();
                    self.out.runs.push(Run {
                        font: font_id,
                        face_index: font.index,
                        size: run.font_size(),
                        embolden: synthesis.embolden() as u32,
                        skew: synthesis.skew().unwrap_or(0.0),
                        coords: match run.normalized_coords() {
                            [] => 0,
                            coords => self.state.coords_id(coords.iter().map(|c| c.to_bits()).collect()),
                        },
                        style: self.origin[glyph_run.style_index() as usize],
                        glyph_start,
                        glyph_count: self.out.glyphs.len() as u32 - glyph_start,
                        x: glyph_run.offset() + dx,
                        y: glyph_run.baseline() + y,
                        advance: glyph_run.advance(),
                        line: out_line,
                        rtl: run.is_rtl() as u32,
                        // parley's offsets are up from the baseline.
                        underline_offset: -metrics.underline_offset,
                        underline_size: thickness(metrics.underline_size, run.font_size()),
                        strikethrough_offset: -metrics.strikethrough_offset,
                        strikethrough_size: thickness(metrics.strikethrough_size, run.font_size()),
                    });
                }
            }
            // parley lays out empty text as one space: the line holds nothing.
            let limit = layout.text_len().min(self.real);
            let map = |b: usize| (b.min(limit) + self.shift) as u32;
            // A line of inline boxes alone holds no text: it sits where the
            // line before it ends.
            let (text_start, text_end) = match line.text_range() {
                text if text.start <= text.end => (map(text.start), map(text.end)),
                _ => {
                    let end = self.out.lines.last().map_or(self.shift as u32, |l| l.text_end);
                    (end, end)
                }
            };
            self.out.lines.push(Line {
                x: line_x,
                y: m.block_min_coord + self.y,
                height: m.block_max_coord - m.block_min_coord,
                baseline: m.baseline + self.y,
                ascent: m.baseline - m.content_block_min_coord,
                descent: m.content_block_max_coord - m.baseline,
                width: if self.out.clusters.len() as u32 == cluster_start && self.out.boxes.len() == boxes_start {
                    0.0
                } else {
                    (advance - m.hanging_advance - dropped).max(0.0)
                },
                text_start,
                text_end,
                cluster_start,
                cluster_count: self.out.clusters.len() as u32 - cluster_start,
                run_start,
                run_count: self.out.runs.len() as u32 - run_start,
            });
        }
    }

    fn cluster(&mut self, c: &ParleyCluster<'_, ()>, x: f32, glyphless: bool, line: u32, glyph: &mut u32) -> f32 {
        let range = c.text_range();
        let mut flags = 0;
        if c.is_rtl() {
            flags |= CLUSTER_RTL;
        }
        if c.is_space_or_nbsp() || c.is_hard_line_break() {
            flags |= CLUSTER_WHITESPACE;
        }
        if c.is_hard_line_break() {
            flags |= CLUSTER_HARD_BREAK;
        }
        if c.is_word_boundary() {
            flags |= CLUSTER_WORD_START;
        }
        if c.is_ligature_continuation() {
            flags |= CLUSTER_LIGATURE_CONTINUATION;
        }
        if self.source.as_bytes().get(range.start + self.shift) == Some(&b'\t') && range.start < self.real {
            flags |= CLUSTER_TAB;
        }
        let (start, len) = if range.start >= self.real {
            flags |= CLUSTER_ELLIPSIS;
            (self.real + self.shift, 0)
        } else {
            (range.start + self.shift, range.len())
        };
        let advance = if glyphless { 0.0 } else { c.advance() };
        let glyph_count = c.glyphs().count() as u32;
        self.out.clusters.push(Cluster {
            start: start as u32,
            len: len as u32,
            x,
            advance,
            line,
            style: self.origin[c.style_index() as usize],
            flags,
            glyph_start: *glyph,
            glyph_count,
        });
        *glyph += glyph_count;
        advance
    }
}
