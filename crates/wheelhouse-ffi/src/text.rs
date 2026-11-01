use super::*;
use std::{cell::RefCell, ffi::c_void, mem, str};
use wheelhouse_text::{
    query, Affinity, Align, Cluster, Family, Feature, FontSource, Glyph, InlineBox, Input, Line, Paragraph, PlacedBox, Rect, Run, Segment,
    Style, StyleRun, TextContext, View, CLUSTER_ELLIPSIS, CLUSTER_HARD_BREAK, CLUSTER_LIGATURE_CONTINUATION, CLUSTER_RTL, CLUSTER_TAB,
    CLUSTER_WHITESPACE, CLUSTER_WORD_START,
};

pub const WH_TEXT_FAMILY_MAIN: u32 = 0;
pub const WH_TEXT_FAMILY_CODE: u32 = 1;
pub const WH_TEXT_FAMILY_ICONS: u32 = 2;

pub const WH_TEXT_ALIGN_START: u32 = 0;
pub const WH_TEXT_ALIGN_END: u32 = 1;
pub const WH_TEXT_ALIGN_CENTER: u32 = 2;
/// The last line, and lines that end at a hard break, align to the start.
pub const WH_TEXT_ALIGN_JUSTIFY: u32 = 3;

/// With the text after the offset.
pub const WH_TEXT_DOWNSTREAM: u32 = 0;
/// With the text before the offset.
pub const WH_TEXT_UPSTREAM: u32 = 1;

pub const WH_TEXT_FONT_BYTES: u32 = 0;
pub const WH_TEXT_FONT_PATH: u32 = 1;
/// Known only by its bytes.
pub const WH_TEXT_FONT_UNKNOWN: u32 = 2;

// Literals, as cbindgen can't evaluate another crate's constants.
pub const WH_TEXT_CLUSTER_RTL: u32 = 1;
/// A space, a no-break space, a tab or a hard line break.
pub const WH_TEXT_CLUSTER_WHITESPACE: u32 = 2;
pub const WH_TEXT_CLUSTER_WORD_START: u32 = 4;
/// No glyphs of its own; its advance is its share of the ligature's.
pub const WH_TEXT_CLUSTER_LIGATURE_CONTINUATION: u32 = 8;
pub const WH_TEXT_CLUSTER_HARD_BREAK: u32 = 16;
/// Covers no bytes; its start is where the text was cut.
pub const WH_TEXT_CLUSTER_ELLIPSIS: u32 = 32;
/// Drawn as a space, advancing to the next tab stop.
pub const WH_TEXT_CLUSTER_TAB: u32 = 64;

const _: () = assert!(
    WH_TEXT_CLUSTER_RTL == CLUSTER_RTL
        && WH_TEXT_CLUSTER_WHITESPACE == CLUSTER_WHITESPACE
        && WH_TEXT_CLUSTER_WORD_START == CLUSTER_WORD_START
        && WH_TEXT_CLUSTER_LIGATURE_CONTINUATION == CLUSTER_LIGATURE_CONTINUATION
        && WH_TEXT_CLUSTER_HARD_BREAK == CLUSTER_HARD_BREAK
        && WH_TEXT_CLUSTER_ELLIPSIS == CLUSTER_ELLIPSIS
        && WH_TEXT_CLUSTER_TAB == CLUSTER_TAB
);

/// Safe to use from several threads at once.
pub struct WhText {
    ctx: TextContext,
}

/// alloc returns size bytes aligned to align, or NULL. The library never
/// frees them, even when a call fails part way. alloc must not call back
/// into the library.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextAllocator {
    pub alloc: Option<unsafe extern "C" fn(user: *mut c_void, size: usize, align: usize) -> *mut c_void>,
    pub user: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextFeature {
    pub tag: [u8; 4],
    /// At most 65535.
    pub value: u32,
}

/// Sizes in points; the context's scale makes them pixels.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextStyle {
    /// WH_TEXT_FAMILY_*.
    pub family: u32,
    pub size: f32,
    /// CSS weight.
    pub weight: f32,
    pub italic: u32,
    pub features: *const WhTextFeature,
    pub feature_count: usize,
    pub letter_spacing: f32,
}

/// In order and not overlapping; bytes no run covers take style 0.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextStyleRun {
    pub start: u32,
    pub len: u32,
    pub style: u32,
}

/// In points, placed before byte offset. baseline is down from its top.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextInlineBox {
    pub id: u64,
    pub offset: u32,
    pub width: f32,
    pub height: f32,
    pub baseline: f32,
}

/// Borrowed for the call.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextInput {
    /// UTF-8.
    pub text: Text,
    pub runs: *const WhTextStyleRun,
    pub run_count: usize,
    pub styles: *const WhTextStyle,
    pub style_count: usize,
    /// In order of offset.
    pub boxes: *const WhTextInlineBox,
    pub box_count: usize,
    /// 0 for no limit.
    pub max_lines: u32,
    /// End a cut last line with "…".
    pub ellipsis: u32,
    /// WH_TEXT_ALIGN_*.
    pub align: u32,
    /// Tab stops every this many spaces of the tab's own style; 0 for 4. Text
    /// with tabs is shaped twice: once to measure where each tab starts. Past
    /// 65536 styles, counting one per distinct tab width, tabs are one space wide.
    pub tab_size: u32,
}

/// Arrays are NULL when empty. In pixels, y down from the paragraph's top.
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
    pub width: f32,
    pub height: f32,
    /// Stale when wh_text_epoch differs.
    pub epoch: u64,
    /// The resolved base direction.
    pub rtl: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextFont {
    /// WH_TEXT_FONT_*.
    pub kind: u32,
    /// For WH_TEXT_FONT_BYTES.
    pub bytes_id: u64,
    /// For WH_TEXT_FONT_PATH.
    pub path: Text,
    pub face_index: u32,
    pub data: *const u8,
    pub data_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextPosition {
    pub offset: u32,
    /// WH_TEXT_DOWNSTREAM or WH_TEXT_UPSTREAM.
    pub affinity: u32,
}

/// y is the line's top.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextCaret {
    pub x: f32,
    pub y: f32,
    pub height: f32,
    pub line: u32,
    pub rtl: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextRects {
    pub rects: *mut Rect,
    pub count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WhTextSegments {
    pub segments: *mut Segment,
    pub count: usize,
}

/// Without system_fonts, only registered fonts are used. Free it with
/// wh_text_free.
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

/// Copies the bytes. Returns how many faces it registered.
#[no_mangle]
pub unsafe extern "C" fn wh_text_register_font(ctx: *const WhText, data: *const u8, len: usize, bytes_id: u64) -> usize {
    // SAFETY: the caller's contract.
    let (Some(t), Ok(bytes)) = (unsafe { ctx.as_ref() }, unsafe { slice(data, len) }) else { return 0 };
    catch_unwind(AssertUnwindSafe(|| t.ctx.register_fonts(bytes.to_vec(), bytes_id))).unwrap_or(0)
}

// The setters and getters from here to wh_text_coords need no catch_unwind:
// they only lock (which recovers from poisoning), copy and clone, so they
// can't panic short of an allocation failure, which aborts anyway.

/// family is WH_TEXT_FAMILY_*. Returns 0 for an unknown slot or non-UTF-8 name.
#[no_mangle]
pub unsafe extern "C" fn wh_text_set_family(ctx: *const WhText, family: u32, name: Text) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(t), Some(family), Ok(name)) = (unsafe { ctx.as_ref() }, Family::from_slot(family), unsafe { name.read() }) else {
        return 0;
    };
    t.ctx.set_family(family, &name);
    1
}

/// Tried in order before fontique's own fallback. Returns 0, changing
/// nothing, if a name is not UTF-8.
#[no_mangle]
pub unsafe extern "C" fn wh_text_set_fallbacks(ctx: *const WhText, names: *const Text, count: usize) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(t), Ok(names)) = (unsafe { ctx.as_ref() }, unsafe { slice(names, count) }) else { return 0 };
    // SAFETY: the caller's contract.
    let Ok(names) = names.iter().map(|n| unsafe { n.read() }).collect::<Result<Vec<_>, _>>() else { return 0 };
    t.ctx.set_fallbacks(&names);
    1
}

/// Returns 0 for a scale that is not positive and finite.
#[no_mangle]
pub unsafe extern "C" fn wh_text_set_scale(ctx: *const WhText, scale: f32) -> u32 {
    // SAFETY: the caller's contract.
    match unsafe { ctx.as_ref() } {
        Some(t) if scale.is_finite() && scale > 0.0 => {
            t.ctx.set_scale(scale);
            1
        }
        _ => 0,
    }
}

/// Rescans the system's fonts.
#[no_mangle]
pub unsafe extern "C" fn wh_text_fonts_changed(ctx: *const WhText) {
    // SAFETY: the caller's contract.
    if let Some(t) = unsafe { ctx.as_ref() } {
        let _ = catch_unwind(AssertUnwindSafe(|| t.ctx.fonts_changed()));
    }
}

/// Changes with font settings, installed fonts or scale.
#[no_mangle]
pub unsafe extern "C" fn wh_text_epoch(ctx: *const WhText) -> u64 {
    // SAFETY: the caller's contract.
    unsafe { ctx.as_ref() }.map_or(0, |t| t.ctx.epoch())
}

/// Its path and bytes live as long as ctx. Returns 0 for an unknown id.
#[no_mangle]
pub unsafe extern "C" fn wh_text_font(ctx: *const WhText, font: u32, out: *mut WhTextFont) -> u32 {
    // SAFETY: the caller's contract.
    let (Some(t), Some(out)) = (unsafe { ctx.as_ref() }, unsafe { out.as_mut() }) else { return 0 };
    let Some(face) = t.ctx.font(font) else { return 0 };
    let (kind, bytes_id, path) = match &face.source {
        FontSource::Bytes(id) => (WH_TEXT_FONT_BYTES, *id, Text::borrowed("")),
        FontSource::Path(path) => (WH_TEXT_FONT_PATH, 0, Text::borrowed(path)),
        FontSource::Unknown => (WH_TEXT_FONT_UNKNOWN, 0, Text::borrowed("")),
    };
    let data = face.data.as_ref();
    *out = WhTextFont { kind, bytes_id, path, face_index: face.index, data: data.as_ptr(), data_len: data.len() };
    1
}

/// Normalized coordinates, live as long as ctx. Returns how many.
#[no_mangle]
pub unsafe extern "C" fn wh_text_coords(ctx: *const WhText, coords: u32, out: *mut *const i16) -> usize {
    // SAFETY: the caller's contract.
    let (Some(t), Some(out)) = (unsafe { ctx.as_ref() }, unsafe { out.as_mut() }) else { return 0 };
    // The context keeps every entry for its life, so the pointer outlives the Arc.
    let coords = t.ctx.coords(coords);
    *out = coords.as_ptr();
    coords.len()
}

/// Entries in any one scratch array, a few MB in all, past which a thread
/// drops its scratch after the call rather than keep it.
const SCRATCH_KEEP: usize = 1 << 16;

thread_local! {
    static SCRATCH: RefCell<Paragraph> = RefCell::default();
    static INPUTS: RefCell<(Vec<StyleRun>, Vec<InlineBox>)> = RefCell::default();
}

/// Pass width INFINITY for no wrapping: a large finite width aligns RTL or
/// centred lines against that width, far from 0, where f32 loses precision.
/// A NaN width is an error, and a negative one is 0. On failure, returns 0
/// with an error and leaves out untouched.
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
            let text = str::from_utf8(slice(input.text.data, input.text.len)?).map_err(|e| e.to_string())?;
            let (mut runs, mut boxes) = INPUTS.with(|inputs| mem::take(&mut *inputs.borrow_mut()));
            // Called at once, so the buffers go back even when the input is refused.
            let laid_out = (|| {
                runs.clear();
                runs.extend(slice(input.runs, input.run_count)?.iter().map(|r| StyleRun {
                    start: r.start as usize,
                    len: r.len as usize,
                    style: r.style as usize,
                }));
                let raw_styles = slice(input.styles, input.style_count)?;
                let features = raw_styles
                    .iter()
                    .map(|s| {
                        slice(s.features, s.feature_count)?
                            .iter()
                            .map(|f| {
                                let value = u16::try_from(f.value).map_err(|_| format!("feature value {} is above 65535", f.value))?;
                                Ok(Feature { tag: f.tag, value })
                            })
                            .collect()
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
                boxes.clear();
                boxes.extend(slice(input.boxes, input.box_count)?.iter().map(|b| InlineBox {
                    id: b.id,
                    offset: b.offset as usize,
                    width: b.width,
                    height: b.height,
                    baseline: b.baseline,
                }));
                let input = Input {
                    text,
                    runs: &runs,
                    styles: &styles,
                    boxes: &boxes,
                    max_lines: input.max_lines as usize,
                    ellipsis: input.ellipsis != 0,
                    align: match input.align {
                        WH_TEXT_ALIGN_START => Align::Start,
                        WH_TEXT_ALIGN_END => Align::End,
                        WH_TEXT_ALIGN_CENTER => Align::Center,
                        WH_TEXT_ALIGN_JUSTIFY => Align::Justify,
                        other => return Err(format!("unknown alignment {other}")),
                    },
                    tab_size: input.tab_size,
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
                        rtl: p.rtl as u32,
                    };
                    if [p.lines.capacity(), p.runs.capacity(), p.glyphs.capacity(), p.clusters.capacity(), p.boxes.capacity()]
                        .iter()
                        .any(|&c| c > SCRATCH_KEEP)
                    {
                        *p = Paragraph::default();
                    }
                    Ok(())
                })
            })();
            INPUTS.with(|inputs| *inputs.borrow_mut() = (runs, boxes));
            laid_out
        })
        .map_or(0, |()| 1)
    }
}

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
            rtl: l.rtl != 0,
        })
    }
}

/// What the queries index: lines' cluster and run ranges, and clusters' bytes.
/// A layout corrupt there must return 0, not read past its arrays or overflow.
fn well_formed(v: &View<'_>) -> bool {
    v.lines.iter().all(|l| {
        (l.cluster_start as usize).checked_add(l.cluster_count as usize).is_some_and(|end| end <= v.clusters.len())
            && (l.run_start as usize).checked_add(l.run_count as usize).is_some_and(|end| end <= v.runs.len())
    }) && v.clusters.iter().all(|c| c.start.checked_add(c.len).is_some())
}

unsafe fn over<T>(layout: *const WhTextLayout, f: impl FnOnce(&View<'_>) -> Option<T>) -> Option<T> {
    // SAFETY: the caller's contract.
    let v = unsafe { view(layout) }?;
    if !well_formed(&v) {
        return None;
    }
    catch_unwind(AssertUnwindSafe(|| f(&v))).ok().flatten()
}

fn affinity(a: Affinity) -> u32 {
    match a {
        Affinity::Downstream => WH_TEXT_DOWNSTREAM,
        Affinity::Upstream => WH_TEXT_UPSTREAM,
    }
}

fn answer<T>(out: Option<&mut T>, value: Option<T>) -> u32 {
    match (out, value) {
        (Some(out), Some(value)) => {
            *out = value;
            1
        }
        _ => 0,
    }
}

/// Returns 0 for a malformed layout.
#[no_mangle]
pub unsafe extern "C" fn wh_text_offset_at(layout: *const WhTextLayout, x: f32, y: f32, out: *mut WhTextPosition) -> u32 {
    // SAFETY: the caller's contract.
    let out = unsafe { out.as_mut() };
    if out.is_none() {
        return 0;
    }
    // SAFETY: the caller's contract.
    let p = unsafe {
        over(layout, |v| {
            let p = query::offset_at(v, x, y);
            // Lossless: offsets come from u32 cluster and line fields, whose sums well_formed checked.
            Some(WhTextPosition { offset: p.offset as u32, affinity: affinity(p.affinity) })
        })
    };
    answer(out, p)
}

/// An offset inside a cluster counts as its start. Returns 0 for a
/// malformed layout or an affinity other than WH_TEXT_DOWNSTREAM or
/// WH_TEXT_UPSTREAM.
#[no_mangle]
pub unsafe extern "C" fn wh_text_caret_rect(layout: *const WhTextLayout, offset: u32, affinity: u32, out: *mut WhTextCaret) -> u32 {
    // SAFETY: the caller's contract.
    let out = unsafe { out.as_mut() };
    if out.is_none() {
        return 0;
    }
    let affinity = match affinity {
        WH_TEXT_DOWNSTREAM => Affinity::Downstream,
        WH_TEXT_UPSTREAM => Affinity::Upstream,
        _ => return 0,
    };
    // SAFETY: the caller's contract.
    let caret = unsafe {
        over(layout, |v| {
            let c = query::caret_rect(v, offset as usize, affinity);
            Some(WhTextCaret { x: c.x, y: c.y, height: c.height, line: c.line as u32, rtl: c.rtl as u32 })
        })
    };
    answer(out, caret)
}

/// Bidi text can give several rects on one line. Returns 0 for a malformed
/// layout or a failed allocation.
#[no_mangle]
pub unsafe extern "C" fn wh_text_selection_rects(
    layout: *const WhTextLayout,
    start: u32,
    end: u32,
    alloc: WhTextAllocator,
    out: *mut WhTextRects,
) -> u32 {
    // SAFETY: the caller's contract.
    let out = unsafe { out.as_mut() };
    if out.is_none() {
        return 0;
    }
    // SAFETY: the caller's contract, and the allocator's.
    let rects = unsafe {
        over(layout, |v| {
            let rects = query::selection_rects(v, start as usize, end as usize);
            Some(WhTextRects { rects: copy_out(&alloc, &rects).ok()?, count: rects.len() })
        })
    };
    answer(out, rects)
}

/// A run id is (Block key, line, segment, generation). Returns 0 for a
/// malformed layout or a failed allocation.
#[no_mangle]
pub unsafe extern "C" fn wh_text_segments(layout: *const WhTextLayout, alloc: WhTextAllocator, out: *mut WhTextSegments) -> u32 {
    // SAFETY: the caller's contract.
    let out = unsafe { out.as_mut() };
    if out.is_none() {
        return 0;
    }
    // SAFETY: the caller's contract, and the allocator's.
    let segments = unsafe {
        over(layout, |v| {
            let segments = query::segments(v);
            Some(WhTextSegments { segments: copy_out(&alloc, &segments).ok()?, count: segments.len() })
        })
    };
    answer(out, segments)
}
