//! Native paragraph layout (wheelhouse-text, #559): a text context holds
//! the fonts and settings, and wh_text_layout lays out one paragraph into
//! arrays the host's allocator provides. The queries read those arrays
//! wherever they live. Calls on one context may come from several threads;
//! each thread gets its own layout state.
use super::*;
use std::{cell::RefCell, ffi::c_void, mem};
use wheelhouse_text::{
    query, Affinity, Cluster, Family, Feature, FontSource, Glyph, InlineBox, Input, Line, Paragraph, PlacedBox, Rect, Run, Segment, Style,
    StyleRun, TextContext, View,
};

/// A family slot: prose.
pub const WH_TEXT_FAMILY_MAIN: u32 = 0;
/// A family slot: code.
pub const WH_TEXT_FAMILY_CODE: u32 = 1;
/// A family slot: icons.
pub const WH_TEXT_FAMILY_ICONS: u32 = 2;

/// A caret with the text after its offset.
pub const WH_TEXT_DOWNSTREAM: u32 = 0;
/// A caret with the text before its offset.
pub const WH_TEXT_UPSTREAM: u32 = 1;

/// A face from bytes the host registered, by its bytes id.
pub const WH_TEXT_FONT_BYTES: u32 = 0;
/// A face from a file fontique found.
pub const WH_TEXT_FONT_PATH: u32 = 1;
/// A face known only by its bytes.
pub const WH_TEXT_FONT_UNKNOWN: u32 = 2;

/// The cluster's text runs right to left.
pub const WH_TEXT_CLUSTER_RTL: u32 = 1;
/// The cluster is a space, a no-break space or a hard line break.
pub const WH_TEXT_CLUSTER_WHITESPACE: u32 = 2;
/// A word starts at the cluster.
pub const WH_TEXT_CLUSTER_WORD_START: u32 = 4;
/// The cluster continues a ligature an earlier cluster starts: it has no
/// glyphs, and its advance is its share of the ligature's.
pub const WH_TEXT_CLUSTER_LIGATURE_CONTINUATION: u32 = 8;
/// The cluster is a hard line break.
pub const WH_TEXT_CLUSTER_HARD_BREAK: u32 = 16;
/// The cluster is the ellipsis a cut line ends with: it covers no bytes,
/// and its start is where the text was cut.
pub const WH_TEXT_CLUSTER_ELLIPSIS: u32 = 32;

const _: () = assert!(
    WH_TEXT_CLUSTER_RTL == wheelhouse_text::CLUSTER_RTL
        && WH_TEXT_CLUSTER_WHITESPACE == wheelhouse_text::CLUSTER_WHITESPACE
        && WH_TEXT_CLUSTER_WORD_START == wheelhouse_text::CLUSTER_WORD_START
        && WH_TEXT_CLUSTER_LIGATURE_CONTINUATION == wheelhouse_text::CLUSTER_LIGATURE_CONTINUATION
        && WH_TEXT_CLUSTER_HARD_BREAK == wheelhouse_text::CLUSTER_HARD_BREAK
        && WH_TEXT_CLUSTER_ELLIPSIS == wheelhouse_text::CLUSTER_ELLIPSIS
);

/// Fonts, family settings and the font epoch, shared by every thread that
/// lays out with it. Free it with wh_text_free.
pub struct WhText {
    ctx: TextContext,
}

/// Where output arrays go: alloc(user, size, align) returns size bytes
/// aligned to align, or NULL on failure. The library never frees them;
/// with an arena, free the arena.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextAllocator {
    pub alloc: Option<unsafe extern "C" fn(user: *mut c_void, size: usize, align: usize) -> *mut c_void>,
    pub user: *mut c_void,
}

/// An OpenType feature setting: tag "liga" value 0 turns ligatures off.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextFeature {
    pub tag: [u8; 4],
    pub value: u32,
}

/// One entry of a layout's style table. Sizes are in points; the
/// context's scale turns them into pixels.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextStyle {
    /// WH_TEXT_FAMILY_*.
    pub family: u32,
    pub size: f32,
    /// CSS weight: 400 regular, 700 bold.
    pub weight: f32,
    pub italic: u32,
    pub features: *const WhTextFeature,
    pub feature_count: usize,
    pub letter_spacing: f32,
}

/// Bytes start..start + len take style `style`. Runs are in order and
/// don't overlap; bytes no run covers take style 0.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextStyleRun {
    pub start: u32,
    pub len: u32,
    pub style: u32,
}

/// A box the host draws inside the text, before byte `offset`, in points.
/// Its baseline is that far down from its top: height sits it on the
/// text's baseline.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextInlineBox {
    pub id: u64,
    pub offset: u32,
    pub width: f32,
    pub height: f32,
    pub baseline: f32,
}

/// What to lay out, borrowed for the call.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextInput {
    /// UTF-8.
    pub text: Text,
    pub runs: *const WhTextStyleRun,
    pub run_count: usize,
    /// At least one.
    pub styles: *const WhTextStyle,
    pub style_count: usize,
    /// In order of offset.
    pub boxes: *const WhTextInlineBox,
    pub box_count: usize,
    /// At most this many lines (0 for no limit).
    pub max_lines: u32,
    /// When max_lines cuts the text, end the last line with "…".
    pub ellipsis: u32,
}

/// A laid-out paragraph: arrays in the host's memory (NULL when empty), in
/// pixels with y down from the paragraph's top. Lines run top to bottom;
/// each line's clusters, runs and glyphs run left to right.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextLayout {
    pub lines: *mut Line,
    pub line_count: usize,
    pub runs: *mut Run,
    pub run_count: usize,
    pub glyphs: *mut Glyph,
    pub glyph_count: usize,
    pub clusters: *mut Cluster,
    pub cluster_count: usize,
    pub boxes: *mut PlacedBox,
    pub box_count: usize,
    /// The widest line's width.
    pub width: f32,
    pub height: f32,
    /// The font epoch it was laid out at: stale when wh_text_epoch differs.
    pub epoch: u64,
}

/// A face a run names.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextFont {
    /// WH_TEXT_FONT_*.
    pub kind: u32,
    /// For WH_TEXT_FONT_BYTES, the id it was registered with.
    pub bytes_id: u64,
    /// For WH_TEXT_FONT_PATH, the file.
    pub path: Text,
    /// The face's index within its file.
    pub face_index: u32,
    /// The file's bytes, as fontique loaded them.
    pub data: *const u8,
    pub data_len: usize,
}

/// A caret position.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextPosition {
    pub offset: u32,
    /// WH_TEXT_DOWNSTREAM or WH_TEXT_UPSTREAM.
    pub affinity: u32,
}

/// Where a caret is drawn: x, from the line's top, its height.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextCaret {
    pub x: f32,
    pub y: f32,
    pub height: f32,
    pub line: u32,
    pub rtl: u32,
}

/// Rectangles of a selection, in the host's memory.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextRects {
    pub rects: *mut Rect,
    pub count: usize,
}

/// A layout's segments (what run ids name), in the host's memory.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextSegments {
    pub segments: *mut Segment,
    pub count: usize,
}

/// A text context with no fonts registered. With system_fonts, fontique
/// also finds the system's fonts (the app); without, only registered fonts
/// are used (tests). Free it with wh_text_free.
#[no_mangle]
pub extern "C" fn wh_text_create(system_fonts: u32) -> *mut WhText {
    catch_unwind(|| Box::into_raw(Box::new(WhText { ctx: TextContext::new(system_fonts != 0) }))).unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wh_text_free(ctx: *mut WhText) {
    if !ctx.is_null() {
        // SAFETY: a live handle from wh_text_create, freed once.
        drop(unsafe { Box::from_raw(ctx) });
    }
}

/// Registers every face in a font file's bytes (copied), which runs then
/// name by bytes_id. Returns how many faces it registered: 0 for bytes
/// that hold no font.
#[no_mangle]
pub unsafe extern "C" fn wh_text_register_font(ctx: *mut WhText, data: *const u8, len: usize, bytes_id: u64) -> usize {
    // SAFETY: the caller's contract.
    let (Some(t), Ok(bytes)) = (unsafe { ctx.as_ref() }, unsafe { slice(data, len) }) else { return 0 };
    catch_unwind(AssertUnwindSafe(|| t.ctx.register_fonts(bytes.to_vec(), bytes_id))).unwrap_or(0)
}

/// Sets the family a slot (WH_TEXT_FAMILY_*) names. Returns 0 for an
/// unknown slot or a name that is not UTF-8.
#[no_mangle]
pub unsafe extern "C" fn wh_text_set_family(ctx: *mut WhText, family: u32, name: Text) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(t), Some(family), Ok(name)) = (unsafe { ctx.as_ref() }, Family::from_slot(family), unsafe { name.read() }) else {
        return 0;
    };
    t.ctx.set_family(family, &name);
    1
}

/// Sets the families every slot falls back to, in order, before fontique's
/// own fallback. Returns 0, changing nothing, if a name is not UTF-8.
#[no_mangle]
pub unsafe extern "C" fn wh_text_set_fallbacks(ctx: *mut WhText, names: *const Text, count: usize) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(t), Ok(names)) = (unsafe { ctx.as_ref() }, unsafe { slice(names, count) }) else { return 0 };
    // SAFETY: the caller's contract.
    let Ok(names) = names.iter().map(|n| unsafe { n.read() }).collect::<Result<Vec<_>, _>>() else { return 0 };
    t.ctx.set_fallbacks(&names);
    1
}

/// Sets the display scale: layouts are in pixels, sizes in points.
/// Returns 0 for a scale that is not positive and finite.
#[no_mangle]
pub unsafe extern "C" fn wh_text_set_scale(ctx: *mut WhText, scale: f32) -> u32 {
    // SAFETY: the caller's contract.
    match unsafe { ctx.as_ref() } {
        Some(t) if scale.is_finite() && scale > 0.0 => {
            t.ctx.set_scale(scale);
            1
        }
        _ => 0,
    }
}

/// Installed fonts changed: finds the system's fonts again.
#[no_mangle]
pub unsafe extern "C" fn wh_text_fonts_changed(ctx: *mut WhText) {
    // SAFETY: the caller's contract.
    if let Some(t) = unsafe { ctx.as_ref() } {
        let _ = catch_unwind(AssertUnwindSafe(|| t.ctx.fonts_changed()));
    }
}

/// Increments when font settings, installed fonts or scale change: a
/// layout from another epoch is stale.
#[no_mangle]
pub unsafe extern "C" fn wh_text_epoch(ctx: *const WhText) -> u64 {
    // SAFETY: the caller's contract.
    unsafe { ctx.as_ref() }.map_or(0, |t| t.ctx.epoch())
}

/// The face a run's font id names, into out; its path and bytes live as
/// long as ctx. Returns 0 for an id no layout has named.
#[no_mangle]
pub unsafe extern "C" fn wh_text_font(ctx: *const WhText, font: u32, out: *mut WhTextFont) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(t), Some(out)) = (unsafe { ctx.as_ref() }, unsafe { out.as_mut() }) else { return 0 };
    let Some(face) = t.ctx.font(font) else { return 0 };
    let (kind, bytes_id, path) = match &face.source {
        FontSource::Bytes(id) => (WH_TEXT_FONT_BYTES, *id, Text::borrowed("")),
        // The context keeps the path for its life.
        FontSource::Path(path) => (WH_TEXT_FONT_PATH, 0, Text::borrowed(path)),
        FontSource::Unknown => (WH_TEXT_FONT_UNKNOWN, 0, Text::borrowed("")),
    };
    let data = face.data.as_ref();
    *out = WhTextFont { kind, bytes_id, path, face_index: face.index, data: data.as_ptr(), data_len: data.len() };
    1
}

/// The normalized variation coordinates a run's coords id names, into
/// out (live as long as ctx). Returns how many: 0 for id 0.
#[no_mangle]
pub unsafe extern "C" fn wh_text_coords(ctx: *const WhText, coords: u32, out: *mut *const i16) -> usize {
    // SAFETY: the caller's contract.
    let (Some(t), Some(out)) = (unsafe { ctx.as_ref() }, unsafe { out.as_mut() }) else { return 0 };
    // The context keeps every coordinates entry for its life.
    let coords = t.ctx.coords(coords);
    *out = coords.as_ptr();
    coords.len()
}

thread_local! {
    /// The arrays a layout is built in before they are copied out.
    static SCRATCH: RefCell<Paragraph> = RefCell::default();
}

/// Lays out a paragraph width pixels wide (INFINITY for no wrapping),
/// writing its arrays through alloc into out. No layout state survives the
/// call: a width change lays out again. Returns 1, or 0 with an error for
/// invalid input or a failed allocation (out is then untouched).
#[no_mangle]
pub unsafe extern "C" fn wh_text_layout(
    ctx: *const WhText,
    input: *const WhTextInput,
    width: f32,
    alloc: WhTextAllocator,
    out: *mut WhTextLayout,
    error_out: *mut *mut c_char,
) -> u32 {
    // SAFETY: the caller's contract.
    unsafe {
        pure(error_out, || {
            let t = ctx.as_ref().ok_or("null text context")?;
            let input = input.as_ref().ok_or("null input")?;
            let out = out.as_mut().ok_or("null output")?;
            let text = std::str::from_utf8(slice(input.text.data, input.text.len)?).map_err(|e| e.to_string())?;
            let runs = slice(input.runs, input.run_count)?
                .iter()
                .map(|r| StyleRun { start: r.start as usize, len: r.len as usize, style: r.style as usize })
                .collect::<Vec<_>>();
            let raw_styles = slice(input.styles, input.style_count)?;
            let features = raw_styles
                .iter()
                .map(|s| {
                    Ok(slice(s.features, s.feature_count)?
                        .iter()
                        .map(|f| Feature { tag: f.tag, value: f.value.min(u16::MAX as u32) as u16 })
                        .collect())
                })
                .collect::<Result<Vec<Vec<_>>, String>>()?;
            let styles = raw_styles
                .iter()
                .zip(&features)
                .map(|(s, features)| {
                    Ok(Style {
                        family: Family::from_slot(s.family).ok_or_else(|| format!("unknown family slot {}", s.family))?,
                        size: s.size,
                        weight: s.weight,
                        italic: s.italic != 0,
                        features,
                        letter_spacing: s.letter_spacing,
                    })
                })
                .collect::<Result<Vec<_>, String>>()?;
            let boxes = slice(input.boxes, input.box_count)?
                .iter()
                .map(|b| InlineBox { id: b.id, offset: b.offset as usize, width: b.width, height: b.height, baseline: b.baseline })
                .collect::<Vec<_>>();
            let input = Input {
                text,
                runs: &runs,
                styles: &styles,
                boxes: &boxes,
                max_lines: input.max_lines as usize,
                ellipsis: input.ellipsis != 0,
            };
            SCRATCH.with(|scratch| {
                let mut p = scratch.borrow_mut();
                t.ctx.layout_into(&input, width, &mut p).map_err(|e| e.to_string())?;
                *out = WhTextLayout {
                    lines: copy_out(&alloc, &p.lines)?,
                    line_count: p.lines.len(),
                    runs: copy_out(&alloc, &p.runs)?,
                    run_count: p.runs.len(),
                    glyphs: copy_out(&alloc, &p.glyphs)?,
                    glyph_count: p.glyphs.len(),
                    clusters: copy_out(&alloc, &p.clusters)?,
                    cluster_count: p.clusters.len(),
                    boxes: copy_out(&alloc, &p.boxes)?,
                    box_count: p.boxes.len(),
                    width: p.width,
                    height: p.height,
                    epoch: p.epoch,
                };
                Ok(())
            })
        })
        .map_or(0, |()| 1)
    }
}

/// Copies an array into memory from the host's allocator: NULL when empty.
unsafe fn copy_out<T: Copy>(alloc: &WhTextAllocator, items: &[T]) -> Result<*mut T, String> {
    if items.is_empty() {
        return Ok(ptr::null_mut());
    }
    let f = alloc.alloc.ok_or("null allocator")?;
    let size = mem::size_of_val(items);
    // SAFETY: the allocator's contract.
    let to = unsafe { f(alloc.user, size, mem::align_of::<T>()) }.cast::<T>();
    if to.is_null() {
        return Err(format!("allocation of {size} bytes failed"));
    }
    if !to.is_aligned() {
        return Err("allocation is not aligned".into());
    }
    // SAFETY: to holds size bytes the allocator just gave, apart from items.
    unsafe { ptr::copy_nonoverlapping(items.as_ptr(), to, items.len()) };
    Ok(to)
}

/// The layout's arrays as slices.
unsafe fn view<'a>(layout: *const WhTextLayout) -> Option<View<'a>> {
    // SAFETY: the caller's contract.
    unsafe {
        let l = layout.as_ref()?;
        Some(View {
            lines: slice(l.lines, l.line_count).ok()?,
            runs: slice(l.runs, l.run_count).ok()?,
            glyphs: slice(l.glyphs, l.glyph_count).ok()?,
            clusters: slice(l.clusters, l.cluster_count).ok()?,
            boxes: slice(l.boxes, l.box_count).ok()?,
        })
    }
}

/// Whether the layout's line and cluster ranges index within its arrays,
/// so the queries can't read past them.
fn well_formed(v: &View<'_>) -> bool {
    v.lines.iter().all(|l| {
        (l.cluster_start as usize).checked_add(l.cluster_count as usize).is_some_and(|end| end <= v.clusters.len())
            && (l.run_start as usize).checked_add(l.run_count as usize).is_some_and(|end| end <= v.runs.len())
    })
}

fn affinity(a: Affinity) -> u32 {
    match a {
        Affinity::Downstream => WH_TEXT_DOWNSTREAM,
        Affinity::Upstream => WH_TEXT_UPSTREAM,
    }
}

/// The caret position nearest the point (x, y), into out. Returns 0 for a
/// malformed layout.
#[no_mangle]
pub unsafe extern "C" fn wh_text_offset_at(layout: *const WhTextLayout, x: f32, y: f32, out: *mut WhTextPosition) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(v), Some(out)) = (unsafe { view(layout) }, unsafe { out.as_mut() }) else { return 0 };
    if !well_formed(&v) {
        return 0;
    }
    let p = query::offset_at(&v, x, y);
    *out = WhTextPosition { offset: p.offset as u32, affinity: affinity(p.affinity) };
    1
}

/// Where the caret for offset and affinity (WH_TEXT_DOWNSTREAM or
/// WH_TEXT_UPSTREAM) is drawn, into out. An offset inside a cluster counts
/// as its start. Returns 0 for a malformed layout.
#[no_mangle]
pub unsafe extern "C" fn wh_text_caret_rect(layout: *const WhTextLayout, offset: u32, affinity: u32, out: *mut WhTextCaret) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(v), Some(out)) = (unsafe { view(layout) }, unsafe { out.as_mut() }) else { return 0 };
    if !well_formed(&v) {
        return 0;
    }
    let affinity = if affinity == WH_TEXT_UPSTREAM { Affinity::Upstream } else { Affinity::Downstream };
    let c = query::caret_rect(&v, offset as usize, affinity);
    *out = WhTextCaret { x: c.x, y: c.y, height: c.height, line: c.line as u32, rtl: c.rtl as u32 };
    1
}

/// The selection of bytes start..end as rectangles per line, through alloc
/// into out: visually adjacent clusters merge, so bidi text can give
/// several on one line. Returns 0 for a malformed layout or a failed
/// allocation.
#[no_mangle]
pub unsafe extern "C" fn wh_text_selection_rects(
    layout: *const WhTextLayout,
    start: u32,
    end: u32,
    alloc: WhTextAllocator,
    out: *mut WhTextRects,
) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(v), Some(out)) = (unsafe { view(layout) }, unsafe { out.as_mut() }) else { return 0 };
    if !well_formed(&v) {
        return 0;
    }
    let rects = query::selection_rects(&v, start as usize, end as usize);
    // SAFETY: the allocator's contract.
    let Ok(to) = (unsafe { copy_out(&alloc, &rects) }) else { return 0 };
    *out = WhTextRects { rects: to, count: rects.len() };
    1
}

/// The layout's segments, through alloc into out: each a stretch of one
/// direction and one style on a line. A run id is the Block's key, the
/// segment's line and index, and the layout's generation. Returns 0 for a
/// malformed layout or a failed allocation.
#[no_mangle]
pub unsafe extern "C" fn wh_text_segments(layout: *const WhTextLayout, alloc: WhTextAllocator, out: *mut WhTextSegments) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(v), Some(out)) = (unsafe { view(layout) }, unsafe { out.as_mut() }) else { return 0 };
    if !well_formed(&v) {
        return 0;
    }
    let segments = query::segments(&v);
    // SAFETY: the allocator's contract.
    let Ok(to) = (unsafe { copy_out(&alloc, &segments) }) else { return 0 };
    *out = WhTextSegments { segments: to, count: segments.len() };
    1
}
