//! Rich Text (#558). A host follows deltas as it does the sidebar's stream
//! (docs/design/change-stream.md), and drives a View per subscription.
//!
//! What a delta returns is borrowed from it until its release; a window is
//! borrowed from the handle until the next call on it. Byte offsets are into
//! the core's text: markdown with invalid UTF-8 has U+FFFD in place of its
//! bad bytes, so hit-test against the delta's text, not the host's bytes.
//!
//! Every call that can fail is guarded by `run`. The plain reads, of a
//! handle's generation and a delta's fields, can't panic and aren't guarded.
use super::*;
use wheelhouse_rich_text::{
    copy_block, Alert, Align, Anchor, Attr, Color, Delta, Detail, FindOptions, FindStatus, Format, Key, ListOp, Match, Metrics,
    NodeContent, Op, Part, Point, Position, RichText, SubscriptionId, Tree, View, Window, MATCH_LIMIT, NO_STRING,
};

/// A node's content, new or changed: read it with wheelhouse_rich_delta_node.
pub const WHEELHOUSE_RICH_OP_UPSERT: u32 = 0;
/// A node and its child list go; its descendants go by their own ops.
pub const WHEELHOUSE_RICH_OP_REMOVE: u32 = 1;
/// `key`'s child list changes by `list`, in order.
pub const WHEELHOUSE_RICH_OP_CHILDREN: u32 = 2;

pub const WHEELHOUSE_RICH_KIND_ROOT: u32 = 0;
pub const WHEELHOUSE_RICH_KIND_LIST: u32 = 1;
pub const WHEELHOUSE_RICH_KIND_LIST_ITEM: u32 = 2;
pub const WHEELHOUSE_RICH_KIND_QUOTE: u32 = 3;
/// A footnote's definition: a Container with a label.
pub const WHEELHOUSE_RICH_KIND_FOOTNOTE: u32 = 4;
/// Reserved for transcripts.
pub const WHEELHOUSE_RICH_KIND_TURN: u32 = 5;
/// Reserved for transcripts.
pub const WHEELHOUSE_RICH_KIND_SECTION: u32 = 6;
pub const WHEELHOUSE_RICH_KIND_PARAGRAPH: u32 = 16;
pub const WHEELHOUSE_RICH_KIND_HEADING: u32 = 17;
/// Front matter is a hidden Code Block.
pub const WHEELHOUSE_RICH_KIND_CODE: u32 = 18;
pub const WHEELHOUSE_RICH_KIND_RULE: u32 = 19;
/// A table's text is its cells as TSV.
pub const WHEELHOUSE_RICH_KIND_EMBED: u32 = 20;

pub const WHEELHOUSE_RICH_ATTR_EMPHASIS: u32 = 0;
pub const WHEELHOUSE_RICH_ATTR_STRONG: u32 = 1;
pub const WHEELHOUSE_RICH_ATTR_CODE: u32 = 2;
pub const WHEELHOUSE_RICH_ATTR_STRIKETHROUGH: u32 = 3;
/// Inline math, drawn as code until a math Embed exists.
pub const WHEELHOUSE_RICH_ATTR_MATH: u32 = 4;
pub const WHEELHOUSE_RICH_ATTR_DISPLAY_MATH: u32 = 5;
/// `value` indexes the URL in the node's strings; `reference` the label of
/// the reference definition it used, or WHEELHOUSE_NONE.
pub const WHEELHOUSE_RICH_ATTR_LINK: u32 = 6;
/// `value` indexes its label in the strings.
pub const WHEELHOUSE_RICH_ATTR_FOOTNOTE_REFERENCE: u32 = 7;
/// `value` indexes the theme's palette.
pub const WHEELHOUSE_RICH_ATTR_COLOR_PALETTE: u32 = 8;
/// `value` is 0xRRGGBB.
pub const WHEELHOUSE_RICH_ATTR_COLOR_RGB: u32 = 9;
/// U+FFFC standing for an inline object: `value` indexes the objects.
pub const WHEELHOUSE_RICH_ATTR_INLINE_OBJECT: u32 = 10;

pub const WHEELHOUSE_RICH_TASK_NONE: u32 = 0;
pub const WHEELHOUSE_RICH_TASK_OPEN: u32 = 1;
pub const WHEELHOUSE_RICH_TASK_DONE: u32 = 2;

pub const WHEELHOUSE_RICH_ALERT_NONE: u32 = 0;
pub const WHEELHOUSE_RICH_ALERT_NOTE: u32 = 1;
pub const WHEELHOUSE_RICH_ALERT_TIP: u32 = 2;
pub const WHEELHOUSE_RICH_ALERT_IMPORTANT: u32 = 3;
pub const WHEELHOUSE_RICH_ALERT_WARNING: u32 = 4;
pub const WHEELHOUSE_RICH_ALERT_CAUTION: u32 = 5;

pub const WHEELHOUSE_RICH_ALIGN_NONE: u32 = 0;
pub const WHEELHOUSE_RICH_ALIGN_LEFT: u32 = 1;
pub const WHEELHOUSE_RICH_ALIGN_CENTER: u32 = 2;
pub const WHEELHOUSE_RICH_ALIGN_RIGHT: u32 = 3;

/// An entry: a Block, or a Container's header or footer row.
pub const WHEELHOUSE_RICH_PART_BLOCK: u32 = 0;
pub const WHEELHOUSE_RICH_PART_HEADER: u32 = 1;
pub const WHEELHOUSE_RICH_PART_FOOTER: u32 = 2;

/// The Scroll Anchor's place in its entry: a text position, or a fraction
/// of its height (an Embed's, or a row's).
pub const WHEELHOUSE_RICH_POSITION_TEXT: u32 = 0;
pub const WHEELHOUSE_RICH_POSITION_FRACTION: u32 = 1;

pub const WHEELHOUSE_RICH_COPY_PLAIN: u32 = 0;
pub const WHEELHOUSE_RICH_COPY_HTML: u32 = 1;
pub const WHEELHOUSE_RICH_COPY_MARKDOWN: u32 = 2;

pub const WHEELHOUSE_RICH_FIND_REGEX: u32 = 1;
pub const WHEELHOUSE_RICH_FIND_CASE_SENSITIVE: u32 = 2;

pub const WHEELHOUSE_RICH_ROOT_KEY: u64 = 0x5249_4348_5445_5854;
const _: () = assert!(WHEELHOUSE_RICH_ROOT_KEY == Key::ROOT.0);
// WheelhouseRichFindStatus says so.
const _: () = assert!(MATCH_LIMIT == 100_000);

/// Bytes `start` to `start + len` of a node's text.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichSpan {
    pub start: u32,
    pub len: u32,
    /// WHEELHOUSE_RICH_ATTR_*.
    pub attr: u32,
    /// What the attribute names (see each WHEELHOUSE_RICH_ATTR_*), else 0.
    pub value: u32,
    /// A link's reference label (in the strings), or WHEELHOUSE_NONE.
    pub reference: usize,
}

/// An object laid out inline as a box. Nothing makes one yet.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichObject {
    pub media_type: Text,
    pub source: Text,
    pub alt: Text,
    /// Size hints in the host's units, or 0.
    pub width: u32,
    pub height: u32,
}

/// Bytes `start` to `start + len` of its Embed's text.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichCell {
    pub start: u32,
    pub len: u32,
}

/// A node's content, without its children. An array with a count of 0 has a
/// pointer that is not NULL but must not be read.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichNode {
    pub key: u64,
    /// WHEELHOUSE_RICH_KIND_* (a Container below 16, a Block from 16).
    pub kind: u32,
    /// Changes whenever its content does: cache layout by (key, version).
    pub version: u64,
    pub level: u32,
    /// Front matter: kept, but not shown.
    pub hidden: u32,
    pub text: Text,
    pub spans: *const WheelhouseRichSpan,
    pub span_count: usize,
    pub strings: *const Text,
    pub string_count: usize,
    pub objects: *const WheelhouseRichObject,
    pub object_count: usize,
    /// A list's.
    pub ordered: u32,
    pub start: u64,
    pub tight: u32,
    /// A list item's WHEELHOUSE_RICH_TASK_*.
    pub task: u32,
    /// A quote's WHEELHOUSE_RICH_ALERT_*.
    pub alert: u32,
    /// A footnote's.
    pub label: Text,
    /// A code Block's info string's first word.
    pub language: Text,
    /// An Embed's.
    pub media_type: Text,
    /// A table's columns' WHEELHOUSE_RICH_ALIGN_*, and its cells row by row,
    /// the header row first.
    pub alignments: *const u32,
    pub column_count: usize,
    pub cells: *const WheelhouseRichCell,
    pub row_count: usize,
}

/// As ListOpView, with a Rich Text key: INSERT reads `at` and `key`, REMOVE
/// `at`, MOVE `from` and `to`; the others are WHEELHOUSE_NONE and 0.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichListOp {
    pub kind: u32,
    pub at: usize,
    pub from: usize,
    pub to: usize,
    pub key: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichOp {
    /// WHEELHOUSE_RICH_OP_*.
    pub kind: u32,
    /// The node upserted or removed, or the parent whose children change.
    pub key: u64,
    /// A CHILDREN op's.
    pub list: *const WheelhouseRichListOp,
    pub list_count: usize,
}

#[derive(Default)]
struct NodeArrays {
    spans: (usize, usize),
    strings: (usize, usize),
    objects: (usize, usize),
    alignments: (usize, usize),
    cells: (usize, usize),
}

#[derive(Default)]
struct Buffers {
    spans: Vec<WheelhouseRichSpan>,
    strings: Vec<Text>,
    objects: Vec<WheelhouseRichObject>,
    alignments: Vec<u32>,
    cells: Vec<WheelhouseRichCell>,
}

fn range<T>(items: &[T], (first, count): (usize, usize)) -> *const T {
    if count == 0 {
        // Non-NULL and aligned, as the header's empty arrays are.
        return ptr::NonNull::dangling().as_ptr();
    }
    items[first..first + count].as_ptr()
}

fn push<T>(vec: &mut Vec<T>, items: impl IntoIterator<Item = T>) -> (usize, usize) {
    let first = vec.len();
    vec.extend(items);
    (first, vec.len() - first)
}

impl Buffers {
    /// The node's strings must outlive the buffers.
    fn add(&mut self, content: &NodeContent) -> NodeArrays {
        let spans = push(
            &mut self.spans,
            content.spans.iter().map(|span| {
                let (attr, value, reference) = match span.attr {
                    Attr::Emphasis => (WHEELHOUSE_RICH_ATTR_EMPHASIS, 0, NONE),
                    Attr::Strong => (WHEELHOUSE_RICH_ATTR_STRONG, 0, NONE),
                    Attr::Code => (WHEELHOUSE_RICH_ATTR_CODE, 0, NONE),
                    Attr::Strikethrough => (WHEELHOUSE_RICH_ATTR_STRIKETHROUGH, 0, NONE),
                    Attr::Math => (WHEELHOUSE_RICH_ATTR_MATH, 0, NONE),
                    Attr::DisplayMath => (WHEELHOUSE_RICH_ATTR_DISPLAY_MATH, 0, NONE),
                    Attr::Link { target, reference } => {
                        (WHEELHOUSE_RICH_ATTR_LINK, target, if reference == NO_STRING { NONE } else { reference as usize })
                    }
                    Attr::FootnoteReference { label } => (WHEELHOUSE_RICH_ATTR_FOOTNOTE_REFERENCE, label, NONE),
                    Attr::Color(Color::Palette(index)) => (WHEELHOUSE_RICH_ATTR_COLOR_PALETTE, u32::from(index), NONE),
                    Attr::Color(Color::Rgb(r, g, b)) => {
                        (WHEELHOUSE_RICH_ATTR_COLOR_RGB, (u32::from(r) << 16) | (u32::from(g) << 8) | u32::from(b), NONE)
                    }
                    Attr::InlineObject { object } => (WHEELHOUSE_RICH_ATTR_INLINE_OBJECT, object, NONE),
                };
                WheelhouseRichSpan { start: span.start, len: span.len, attr, value, reference }
            }),
        );
        let strings = push(&mut self.strings, content.strings.iter().map(|s| Text::borrowed(s)));
        let objects = push(
            &mut self.objects,
            content.objects.iter().map(|o| WheelhouseRichObject {
                media_type: Text::borrowed(&o.media_type),
                source: Text::borrowed(&o.source),
                alt: Text::borrowed(&o.alt),
                width: o.width,
                height: o.height,
            }),
        );
        let (alignments, cells) = match &content.detail {
            Detail::Embed { table: Some(table), .. } => {
                let columns = table.alignments.len();
                let alignments = push(
                    &mut self.alignments,
                    table.alignments.iter().map(|a| match a {
                        Align::None => WHEELHOUSE_RICH_ALIGN_NONE,
                        Align::Left => WHEELHOUSE_RICH_ALIGN_LEFT,
                        Align::Center => WHEELHOUSE_RICH_ALIGN_CENTER,
                        Align::Right => WHEELHOUSE_RICH_ALIGN_RIGHT,
                    }),
                );
                let cells = push(
                    &mut self.cells,
                    table.rows.iter().flat_map(|row| {
                        (0..columns).map(|c| {
                            row.get(c).map_or(WheelhouseRichCell { start: 0, len: 0 }, |cell| WheelhouseRichCell {
                                start: cell.start,
                                len: cell.len,
                            })
                        })
                    }),
                );
                (alignments, cells)
            }
            _ => ((0, 0), (0, 0)),
        };
        NodeArrays { spans, strings, objects, alignments, cells }
    }

    fn view(&self, content: &NodeContent, arrays: &NodeArrays) -> WheelhouseRichNode {
        let (ordered, start, tight) = match &content.detail {
            Detail::List { ordered, start, tight } => (u32::from(*ordered), *start, u32::from(*tight)),
            _ => (0, 0, 0),
        };
        let task = match &content.detail {
            Detail::Item { task: Some(true) } => WHEELHOUSE_RICH_TASK_DONE,
            Detail::Item { task: Some(false) } => WHEELHOUSE_RICH_TASK_OPEN,
            _ => WHEELHOUSE_RICH_TASK_NONE,
        };
        let alert = match &content.detail {
            Detail::Quote { alert: Some(alert) } => match alert {
                Alert::Note => WHEELHOUSE_RICH_ALERT_NOTE,
                Alert::Tip => WHEELHOUSE_RICH_ALERT_TIP,
                Alert::Important => WHEELHOUSE_RICH_ALERT_IMPORTANT,
                Alert::Warning => WHEELHOUSE_RICH_ALERT_WARNING,
                Alert::Caution => WHEELHOUSE_RICH_ALERT_CAUTION,
            },
            _ => WHEELHOUSE_RICH_ALERT_NONE,
        };
        let text_of = |detail: &Detail, pick: fn(&Detail) -> Option<&str>| Text::borrowed(pick(detail).unwrap_or(""));
        WheelhouseRichNode {
            key: content.key.0,
            kind: content.kind as u32,
            version: content.version,
            level: u32::from(content.level),
            hidden: u32::from(content.hidden),
            text: Text::borrowed(&content.text),
            spans: range(&self.spans, arrays.spans),
            span_count: arrays.spans.1,
            strings: range(&self.strings, arrays.strings),
            string_count: arrays.strings.1,
            objects: range(&self.objects, arrays.objects),
            object_count: arrays.objects.1,
            ordered,
            start,
            tight,
            task,
            alert,
            label: text_of(&content.detail, |d| match d {
                Detail::Footnote { label } => Some(label),
                _ => None,
            }),
            language: text_of(&content.detail, |d| match d {
                Detail::Code { language } => Some(language),
                _ => None,
            }),
            media_type: text_of(&content.detail, |d| match d {
                Detail::Embed { media_type, .. } => Some(media_type),
                _ => None,
            }),
            alignments: range(&self.alignments, arrays.alignments),
            column_count: arrays.alignments.1,
            cells: range(&self.cells, arrays.cells),
            row_count: arrays.cells.1.checked_div(arrays.alignments.1).unwrap_or(0),
        }
    }
}

struct OpData {
    kind: u32,
    key: u64,
    /// An upsert's content and arrays.
    node: Option<(usize, NodeArrays)>,
    list: (usize, usize),
}

pub struct WheelhouseRichDelta {
    generation: u64,
    snapshot: bool,
    ops: Vec<OpData>,
    contents: Vec<NodeContent>,
    buffers: Buffers,
    lists: Vec<WheelhouseRichListOp>,
}

impl WheelhouseRichDelta {
    fn of(delta: Delta) -> Self {
        let mut out = Self {
            generation: delta.generation,
            snapshot: delta.snapshot,
            ops: vec![],
            contents: vec![],
            buffers: Buffers::default(),
            lists: vec![],
        };
        for op in delta.ops {
            let data = match op {
                Op::Upsert(content) => {
                    let key = content.key.0;
                    out.contents.push(*content);
                    let index = out.contents.len() - 1;
                    // The strings stay where they are: contents never grows
                    // past this loop's pushes, and a String's bytes don't move
                    // with it.
                    let arrays = out.buffers.add(&out.contents[index]);
                    OpData { kind: WHEELHOUSE_RICH_OP_UPSERT, key, node: Some((index, arrays)), list: (0, 0) }
                }
                Op::Remove(key) => OpData { kind: WHEELHOUSE_RICH_OP_REMOVE, key: key.0, node: None, list: (0, 0) },
                Op::Children { parent, ops } => {
                    let list = push(
                        &mut out.lists,
                        ops.iter().map(|op| match *op {
                            ListOp::Insert { at, key } => {
                                WheelhouseRichListOp { kind: WHEELHOUSE_LIST_INSERT, at, from: NONE, to: NONE, key: key.0 }
                            }
                            ListOp::Remove { at } => {
                                WheelhouseRichListOp { kind: WHEELHOUSE_LIST_REMOVE, at, from: NONE, to: NONE, key: 0 }
                            }
                            ListOp::Move { from, to } => WheelhouseRichListOp { kind: WHEELHOUSE_LIST_MOVE, at: NONE, from, to, key: 0 },
                        }),
                    );
                    OpData { kind: WHEELHOUSE_RICH_OP_CHILDREN, key: parent.0, node: None, list }
                }
            };
            out.ops.push(data);
        }
        // From here a delta is only read (its accessors take it by const
        // pointer), so the views they build into its vectors stay valid.
        out
    }
}

/// A markdown document and its subscribers. Calls on one are serialised by
/// the host.
pub struct WheelhouseRichText {
    rich: RichText,
    poisoned: bool,
    /// What the last window points into.
    window: Vec<WheelhouseRichWindowEntry>,
}

impl Handle for WheelhouseRichText {
    const NAME: &'static str = "rich text";
    fn poisoned(&mut self) -> &mut bool {
        &mut self.poisoned
    }
}

/// Byte offsets are 32-bit, so a document is under 4 GiB.
/// Checked after decoding: each invalid byte's U+FFFD takes three.
unsafe fn markdown(text: Text) -> Result<String, String> {
    let source = String::from_utf8_lossy(slice(text.data, text.len)?).into_owned();
    if u32::try_from(source.len()).is_err() {
        return Err("markdown of 4 GiB or more".into());
    }
    Ok(source)
}

/// Open UTF-8 markdown under 4 GiB; free it with wheelhouse_rich_text_free.
/// NULL with an error on invalid input (a NULL pointer with a length, or 4
/// GiB or more) or if parsing panicked.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_open(markdown_text: Text, error_out: *mut *mut c_char) -> *mut WheelhouseRichText {
    pure(error_out, || {
        let rich = RichText::open(&markdown(markdown_text)?);
        Ok(Box::into_raw(Box::new(WheelhouseRichText { rich, poisoned: false, window: vec![] })))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_free(h: *mut WheelhouseRichText) {
    if !h.is_null() {
        // A panic must not unwind into C, even from a poisoned handle's drop.
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(h))));
    }
}

/// Parse the whole document again (under 4 GiB): unchanged nodes keep their
/// versions. Deltas already polled stay valid. `changed_out` (if not NULL)
/// gets 1 if anything changed, and 0 otherwise or on an error.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_reload(
    h: *mut WheelhouseRichText,
    markdown_text: Text,
    changed_out: *mut u32,
    error_out: *mut *mut c_char,
) -> u32 {
    if let Some(out) = changed_out.as_mut() {
        *out = 0;
    }
    run(h, error_out, |h| {
        let source = markdown(markdown_text)?;
        let changed = h.rich.reload(&source);
        if let Some(out) = changed_out.as_mut() {
            *out = u32::from(changed);
        }
        Ok(())
    })
    .is_some() as u32
}

/// It rises with each reload that changes something. 0 for NULL.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_generation(h: *mut WheelhouseRichText) -> u64 {
    h.as_ref().map_or(0, |h| h.rich.generation())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_subscribe(h: *mut WheelhouseRichText, out: *mut u64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| {
        let out = out.as_mut().ok_or("null output")?;
        *out = h.rich.subscribe().0;
        Ok(())
    })
    .is_some() as u32
}

/// 0 (no error) for a subscription that doesn't exist.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_unsubscribe(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| Ok(h.rich.unsubscribe(SubscriptionId(subscription)))).unwrap_or(false) as u32
}

/// 1 if a poll has something to deliver; 0 with an error for an unknown
/// subscription.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_changed_since(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| h.rich.changed_since(SubscriptionId(subscription)).map_err(|e| e.to_string())).unwrap_or(false) as u32
}

/// What changed since this subscription's last delta: a snapshot for its
/// first, and for one that fell off the change ring's end. NULL with no
/// error when nothing changed. Release it with wheelhouse_rich_delta_release.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_poll(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseRichDelta {
    run(h, error_out, |h| {
        let delta = h.rich.poll(SubscriptionId(subscription)).map_err(|e| e.to_string())?;
        Ok(delta.map(|delta| Box::into_raw(Box::new(WheelhouseRichDelta::of(delta)))))
    })
    .flatten()
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_delta_generation(delta: *const WheelhouseRichDelta) -> u64 {
    delta.as_ref().map_or(0, |d| d.generation)
}

/// 1 for a snapshot: clear the model before applying it.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_delta_is_snapshot(delta: *const WheelhouseRichDelta) -> u32 {
    delta.as_ref().map_or(0, |d| u32::from(d.snapshot))
}

/// Removes come first, then upserts in document order, then child lists.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_delta_op_count(delta: *const WheelhouseRichDelta) -> usize {
    delta.as_ref().map_or(0, |d| d.ops.len())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_delta_op(delta: *const WheelhouseRichDelta, index: usize, out: *mut WheelhouseRichOp) -> u32 {
    let (Some(d), Some(out)) = (delta.as_ref(), out.as_mut()) else { return 0 };
    let Some(op) = d.ops.get(index) else { return 0 };
    *out = WheelhouseRichOp { kind: op.kind, key: op.key, list: range(&d.lists, op.list), list_count: op.list.1 };
    1
}

/// An UPSERT op's node; 0 for another op.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_delta_node(delta: *const WheelhouseRichDelta, index: usize, out: *mut WheelhouseRichNode) -> u32 {
    let (Some(d), Some(out)) = (delta.as_ref(), out.as_mut()) else { return 0 };
    let Some((content, arrays)) = d.ops.get(index).and_then(|op| op.node.as_ref()) else { return 0 };
    *out = d.buffers.view(&d.contents[*content], arrays);
    1
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_delta_release(delta: *mut WheelhouseRichDelta) {
    if !delta.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(delta))));
    }
}

fn part(part: u32) -> Result<Part, String> {
    match part {
        WHEELHOUSE_RICH_PART_BLOCK => Ok(Part::Block),
        WHEELHOUSE_RICH_PART_HEADER => Ok(Part::Header),
        WHEELHOUSE_RICH_PART_FOOTER => Ok(Part::Footer),
        _ => Err(format!("unknown part {part}")),
    }
}

/// NaN or infinity would stay in the height index's sums for good.
fn finite(values: &[f64]) -> Result<(), String> {
    match values.iter().all(|v| v.is_finite()) {
        true => Ok(()),
        false => Err("not a finite number".into()),
    }
}

unsafe fn view<T>(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
    f: impl FnOnce(&Tree, &mut View) -> Result<T, String>,
) -> Option<T> {
    run(h, error_out, |h| {
        let (doc, view) = h.rich.view_mut(SubscriptionId(subscription)).map_err(|e| e.to_string())?;
        f(doc, view)
    })
}

/// In the host's units.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_set_viewport(
    h: *mut WheelhouseRichText,
    subscription: u64,
    height: f64,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        finite(&[height])?;
        view.set_viewport(height);
        Ok(())
    })
    .is_some() as u32
}

/// For estimating unmeasured heights. On a width change every measured
/// height becomes an estimate scaled by the width ratio, until measured
/// again.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_set_metrics(
    h: *mut WheelhouseRichText,
    subscription: u64,
    width: f32,
    line_height: f32,
    chars_per_line: f32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        finite(&[width.into(), line_height.into(), chars_per_line.into()])?;
        view.set_metrics(doc, Metrics { width, line_height, chars_per_line });
        Ok(())
    })
    .is_some() as u32
}

/// The host measured an entry (WHEELHOUSE_RICH_PART_*) `height` high at
/// `width`.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_set_height(
    h: *mut WheelhouseRichText,
    subscription: u64,
    key: u64,
    entry_part: u32,
    height: f32,
    width: f32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        finite(&[height.into(), width.into()])?;
        view.set_height(doc, Key(key), part(entry_part)?, height, width)
    })
    .is_some() as u32
}

/// Scrolling up breaks follow-tail, and reaching the bottom re-arms it.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_scroll_by(
    h: *mut WheelhouseRichText,
    subscription: u64,
    delta: f64,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        finite(&[delta])?;
        view.scroll_by(doc, delta);
        Ok(())
    })
    .is_some() as u32
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_scroll_to_end(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        view.scroll_to_end();
        Ok(())
    })
    .is_some() as u32
}

/// Scrolling is worked out from the Scroll Anchor, so heights changing above
/// it never move what is shown.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichAnchor {
    pub key: u64,
    /// WHEELHOUSE_RICH_PART_*.
    pub part: u32,
    /// WHEELHOUSE_RICH_POSITION_*.
    pub position: u32,
    /// TEXT: where the anchored line starts, and how far into it the anchor
    /// point is.
    pub byte: u32,
    pub line_offset: f32,
    /// FRACTION: from 0 to 1.
    pub fraction: f32,
    /// How far below the viewport's top the anchor point sits.
    pub viewport_offset: f64,
}

/// Breaks follow-tail.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_scroll_to(
    h: *mut WheelhouseRichText,
    subscription: u64,
    anchor: *const WheelhouseRichAnchor,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        let a = anchor.as_ref().ok_or("null anchor")?;
        let position = match a.position {
            WHEELHOUSE_RICH_POSITION_TEXT => Position::Text { byte: a.byte, line_offset: a.line_offset },
            WHEELHOUSE_RICH_POSITION_FRACTION => Position::Fraction(a.fraction),
            other => return Err(format!("unknown position {other}")),
        };
        let used = match position {
            Position::Text { line_offset, .. } => line_offset,
            Position::Fraction(fraction) => fraction,
        };
        finite(&[used.into(), a.viewport_offset])?;
        view.scroll_to(doc, Anchor { key: Key(a.key), part: part(a.part)?, position, viewport_offset: a.viewport_offset })
    })
    .is_some() as u32
}

/// 0 (no error) at the document's top, with no anchor.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_anchor(
    h: *mut WheelhouseRichText,
    subscription: u64,
    out: *mut WheelhouseRichAnchor,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        let out = out.as_mut().ok_or("null output")?;
        let Some(anchor) = view.anchor() else { return Ok(false) };
        let (position, byte, line_offset, fraction) = match anchor.position {
            Position::Text { byte, line_offset } => (WHEELHOUSE_RICH_POSITION_TEXT, byte, line_offset, 0.0),
            Position::Fraction(f) => (WHEELHOUSE_RICH_POSITION_FRACTION, 0, 0.0, f),
        };
        *out = WheelhouseRichAnchor {
            key: anchor.key.0,
            part: anchor.part as u32,
            position,
            byte,
            line_offset,
            fraction,
            viewport_offset: anchor.viewport_offset,
        };
        Ok(true)
    })
    .unwrap_or(false) as u32
}

/// The host found where the viewport's top falls in the anchor's Block: in
/// the line starting at `byte`, whose top is `line_top` into the Block.
/// Nothing moves.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_set_anchor_text(
    h: *mut WheelhouseRichText,
    subscription: u64,
    byte: u32,
    line_top: f32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        finite(&[line_top.into()])?;
        view.set_anchor_text(byte, line_top);
        Ok(())
    })
    .is_some() as u32
}

/// The anchor's Block was laid out again (a new width), and the line the
/// anchor names now starts `line_top` into it: keep that line in place.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_relayout_anchor(
    h: *mut WheelhouseRichText,
    subscription: u64,
    line_top: f32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        finite(&[line_top.into()])?;
        view.relayout_anchor(line_top);
        Ok(())
    })
    .is_some() as u32
}

/// A collapsed Container's rows after its header count as zero, and
/// selections don't copy them.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_set_collapsed(
    h: *mut WheelhouseRichText,
    subscription: u64,
    key: u64,
    collapsed: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| view.set_collapsed(doc, Key(key), collapsed != 0)).is_some() as u32
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichWindowEntry {
    pub key: u64,
    /// WHEELHOUSE_RICH_PART_*.
    pub part: u32,
    /// 1 if its height is as measured at the current width.
    pub measured: u32,
    pub top: f64,
    pub height: f32,
}

/// The entries in the viewport plus half a viewport on each side, hidden
/// ones left out.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichWindow {
    pub top: f64,
    pub viewport: f64,
    /// Estimates included.
    pub total: f64,
    /// 1 while following the end.
    pub at_end: u32,
    pub entries: *const WheelhouseRichWindowEntry,
    pub entry_count: usize,
}

/// Its entries are borrowed from the handle until the next call on it.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_window(
    h: *mut WheelhouseRichText,
    subscription: u64,
    out: *mut WheelhouseRichWindow,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        let out = out.as_mut().ok_or("null output")?;
        let window: Window = h.rich.view(SubscriptionId(subscription)).map_err(|e| e.to_string())?.window(h.rich.doc());
        h.window = window
            .entries
            .iter()
            .map(|e| WheelhouseRichWindowEntry {
                key: e.key.0,
                part: e.part as u32,
                measured: u32::from(e.measured),
                top: e.top,
                height: e.height,
            })
            .collect();
        *out = WheelhouseRichWindow {
            top: window.top,
            viewport: window.viewport,
            total: window.total,
            at_end: u32::from(window.at_end),
            entries: range(&h.window, (0, h.window.len())),
            entry_count: h.window.len(),
        };
        Ok(())
    })
    .is_some() as u32
}

/// Fractions of the document's height.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichThumb {
    pub start: f64,
    pub size: f64,
    pub estimated: f64,
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_thumb(
    h: *mut WheelhouseRichText,
    subscription: u64,
    out: *mut WheelhouseRichThumb,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        let out = out.as_mut().ok_or("null output")?;
        let thumb = view.thumb(doc);
        *out = WheelhouseRichThumb { start: thumb.start, size: thumb.size, estimated: thumb.estimated };
        Ok(())
    })
    .is_some() as u32
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichSelection {
    pub anchor_key: u64,
    pub anchor_byte: u32,
    pub focus_key: u64,
    pub focus_byte: u32,
}

/// Points move onto character boundaries; an Embed is selected whole.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_select(
    h: *mut WheelhouseRichText,
    subscription: u64,
    anchor_key: u64,
    anchor_byte: u32,
    focus_key: u64,
    focus_byte: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        view.select(doc, Point { key: Key(anchor_key), byte: anchor_byte }, Point { key: Key(focus_key), byte: focus_byte })
    })
    .is_some() as u32
}

/// Move the focus, keeping the anchor.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_extend(
    h: *mut WheelhouseRichText,
    subscription: u64,
    focus_key: u64,
    focus_byte: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| view.extend(doc, Point { key: Key(focus_key), byte: focus_byte })).is_some() as u32
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_select_block(
    h: *mut WheelhouseRichText,
    subscription: u64,
    key: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| view.select_block(doc, Key(key))).is_some() as u32
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_select_all(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        view.select_all(doc);
        Ok(())
    })
    .is_some() as u32
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_clear_selection(
    h: *mut WheelhouseRichText,
    subscription: u64,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        view.clear_selection();
        Ok(())
    })
    .is_some() as u32
}

/// 0 (no error) when there is none.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_selection(
    h: *mut WheelhouseRichText,
    subscription: u64,
    out: *mut WheelhouseRichSelection,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        let out = out.as_mut().ok_or("null output")?;
        let Some(s) = view.selection() else { return Ok(false) };
        *out = WheelhouseRichSelection {
            anchor_key: s.anchor.key.0,
            anchor_byte: s.anchor.byte,
            focus_key: s.focus.key.0,
            focus_byte: s.focus.byte,
        };
        Ok(true)
    })
    .unwrap_or(false) as u32
}

fn format(format: u32) -> Result<Format, String> {
    match format {
        WHEELHOUSE_RICH_COPY_PLAIN => Ok(Format::Plain),
        WHEELHOUSE_RICH_COPY_HTML => Ok(Format::Html),
        WHEELHOUSE_RICH_COPY_MARKDOWN => Ok(Format::Markdown),
        _ => Err(format!("unknown copy format {format}")),
    }
}

/// As WHEELHOUSE_RICH_COPY_*: empty if nothing is selected. Free the bytes
/// with wheelhouse_bytes_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_copy(
    h: *mut WheelhouseRichText,
    subscription: u64,
    copy_format: u32,
    out: *mut Bytes,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        let text = view.copy(doc, format(copy_format)?).unwrap_or_default();
        write_bytes(out, text.into_bytes())
    })
    .is_some() as u32
}

/// A whole Block, collapsed or not.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_copy_block(
    h: *mut WheelhouseRichText,
    key: u64,
    copy_format: u32,
    out: *mut Bytes,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        let text = copy_block(h.rich.doc(), Key(key), format(copy_format)?).ok_or("no such Block")?;
        write_bytes(out, text.into_bytes())
    })
    .is_some() as u32
}

/// Show "N of count+" until the scan is done, and after, if `capped`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichFindStatus {
    pub count: usize,
    pub done: u32,
    /// 1 if the scan stopped at 100 000 matches.
    pub capped: u32,
    /// The match last moved to, or WHEELHOUSE_NONE.
    pub current: usize,
}

/// Bytes `start` to `end` of a Block's text.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseRichMatch {
    pub key: u64,
    pub start: u32,
    pub end: u32,
}

fn status(status: FindStatus) -> WheelhouseRichFindStatus {
    WheelhouseRichFindStatus {
        count: status.count,
        done: u32::from(status.done),
        capped: u32::from(status.capped),
        current: status.current.unwrap_or(NONE),
    }
}

fn found(m: Match) -> WheelhouseRichMatch {
    WheelhouseRichMatch { key: m.key.0, start: m.start, end: m.end }
}

/// Over every Block, collapsed or not: literal and case-insensitive unless
/// `flags` has WHEELHOUSE_RICH_FIND_*. Scan with
/// wheelhouse_rich_text_find_step.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_find(
    h: *mut WheelhouseRichText,
    subscription: u64,
    query: Text,
    flags: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        let query = query.read()?;
        let options = FindOptions {
            regex: flags & WHEELHOUSE_RICH_FIND_REGEX != 0,
            case_sensitive: flags & WHEELHOUSE_RICH_FIND_CASE_SENSITIVE != 0,
        };
        view.find(&query, options)
    })
    .is_some() as u32
}

/// Scan about `budget` bytes more (at least one Block). A reload starts the
/// scan again.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_find_step(
    h: *mut WheelhouseRichText,
    subscription: u64,
    budget: usize,
    out: *mut WheelhouseRichFindStatus,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        let out = out.as_mut().ok_or("null output")?;
        *out = status(view.find_step(doc, budget).ok_or("not finding")?);
        Ok(())
    })
    .is_some() as u32
}

/// 0 with an error past the count.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_find_match(
    h: *mut WheelhouseRichText,
    subscription: u64,
    index: usize,
    out: *mut WheelhouseRichMatch,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |_, view| {
        let out = out.as_mut().ok_or("null output")?;
        *out = found(*view.find_state().ok_or("not finding")?.matches().get(index).ok_or("no such match")?);
        Ok(())
    })
    .is_some() as u32
}

/// Select and reveal the next match (`forward` 1) or the previous one. 0
/// (no error) when there are none yet.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_find_next(
    h: *mut WheelhouseRichText,
    subscription: u64,
    forward: u32,
    out: *mut WheelhouseRichMatch,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| {
        let Some(m) = view.find_next(doc, forward != 0) else { return Ok(false) };
        if let Some(out) = out.as_mut() {
            *out = found(m);
        }
        Ok(true)
    })
    .unwrap_or(false) as u32
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_find_end(h: *mut WheelhouseRichText, subscription: u64, error_out: *mut *mut c_char) -> u32 {
    view(h, subscription, error_out, |_, view| {
        view.end_find();
        Ok(())
    })
    .is_some() as u32
}

/// Expand any collapsed Container holding it, and anchor it a third of the
/// way down the viewport. Breaks follow-tail.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_reveal(
    h: *mut WheelhouseRichText,
    subscription: u64,
    key: u64,
    byte: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    view(h, subscription, error_out, |doc, view| view.reveal(doc, Key(key), byte)).is_some() as u32
}
