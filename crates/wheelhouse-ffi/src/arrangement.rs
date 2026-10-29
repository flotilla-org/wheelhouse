//! An arrangement as a host edits it (#516): the edit document, its edits
//! and intents, applied by the core's rules. No sidebar handle is needed:
//! each call takes a document and returns the edited one.
//!
//! The document mirrors the core's `arrangement::Arrangement` as the host
//! keeps its live copy: panels in preorder, each naming its parent by
//! index, with numeric IDs and the `next_id` counter. A tab is an opaque
//! fixed-size value the core only copies and compares bytewise (the native
//! shell's View Address). A result is one allocation the host frees with
//! wheelhouse_edit_result_free; every pointer in it stays valid until then.
use super::*;
use std::ffi::c_void;
use wheelhouse_core::arrangement::{
    apply, apply_intent, problem, Arrangement, Dir, Edit, Intent, Outcome, Owner, OwnerKind, Panel, PanelId,
};
use wheelhouse_core::suggested_layout::Axis;

/// The size of a tab: the host's View Address, copied whole.
pub const WHEELHOUSE_TAB_BYTES: usize = 40;

/// A tab: bytes the core compares, never reads.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct WheelhouseTab {
    pub bytes: [u8; WHEELHOUSE_TAB_BYTES],
}

/// A panel of an edit document.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseEditPanel {
    /// The index of an earlier panel, or WHEELHOUSE_NONE for the root.
    pub parent: usize,
    /// Nonzero.
    pub id: u64,
    /// Relative to its siblings, as an edit wrote it or the host loaded it.
    pub weight: f32,
    /// Nonzero once a dock restore took a sidebar section out of it.
    pub restore_emptied: u32,
    /// The panel whose Presentation State it takes when the host next sets
    /// the arrangement, or 0.
    pub options_from: u64,
    /// The ID a provider's document gave it; empty for none. A named panel
    /// stays when its last tab goes.
    pub named: Text,
    /// Its tabs, in order. Edits give tabs only to leaves.
    pub tabs: *const WheelhouseTab,
    pub tab_count: usize,
    /// Nonzero when it has a Selected View, `selected`.
    pub has_selected: u32,
    pub selected: WheelhouseTab,
}

/// An arrangement as a host edits it: at most one root, first.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseEditDoc {
    /// Panels, each after its parent; at most one root.
    pub panels: *const WheelhouseEditPanel,
    pub panel_count: usize,
    /// The root's WHEELHOUSE_AXIS_*, kept while the root is a leaf.
    pub root_axis: u32,
    /// The next panel's ID: above every panel's.
    pub next_id: u64,
    /// Tabs removed since the host last set it, oldest first, whose Views
    /// go when it is set unless an arrangement has them again.
    pub removed: *const WheelhouseTab,
    pub removed_count: usize,
}

/// A side, for a split or a drop that splits.
pub const WHEELHOUSE_DIR_LEFT: u32 = 0;
pub const WHEELHOUSE_DIR_UP: u32 = 1;
pub const WHEELHOUSE_DIR_RIGHT: u32 = 2;
pub const WHEELHOUSE_DIR_DOWN: u32 = 3;
/// No side: a drop into the panel, or a settle after a move.
pub const WHEELHOUSE_DIR_NONE: u32 = u32::MAX;

/// Edits, one per rule (core: arrangement::Edit). Each names the fields it
/// reads; the rest are ignored.
/// One empty root replaces every panel: `panel` in the outcome.
pub const WHEELHOUSE_EDIT_CLEAR: u32 = 0;
/// Every panel and tab goes.
pub const WHEELHOUSE_EDIT_DISCARD: u32 = 1;
/// Splits `panel` towards `dir`: the new panel in the outcome.
pub const WHEELHOUSE_EDIT_SPLIT: u32 = 2;
/// Adds a child to `panel` (0 with no panels: the root first) at `weight`,
/// or an equal share for 0: the new panel in the outcome.
pub const WHEELHOUSE_EDIT_ADD: u32 = 3;
/// Removes `panel` as the sidebar's reconciliation does.
pub const WHEELHOUSE_EDIT_REMOVE: u32 = 4;
/// Closes `panel`: its heir in the outcome.
pub const WHEELHOUSE_EDIT_CLOSE: u32 = 5;
/// Moves `tab` into `panel` after `after` (if `has_after`), selecting it
/// there when `select`.
pub const WHEELHOUSE_EDIT_INSERT_TAB: u32 = 6;
/// Takes `tab` out, keeping its View.
pub const WHEELHOUSE_EDIT_DETACH_TAB: u32 = 7;
/// Removes `tab`; its View goes when the arrangement is set.
pub const WHEELHOUSE_EDIT_REMOVE_TAB: u32 = 8;
/// Drops `tab` (if `has_tab`; none only splits) into `panel`, or a split
/// of it towards `dir`, after `after`; settles the panel it left.
pub const WHEELHOUSE_EDIT_DROP: u32 = 9;
/// Settles `panel` after a drop (`dir` the drop's), closing it when
/// `may_close` and the rule says so.
pub const WHEELHOUSE_EDIT_SETTLE: u32 = 10;
/// Selects `tab` (if `has_tab`; else none) in `panel`.
pub const WHEELHOUSE_EDIT_SELECT: u32 = 11;
/// Moves `panel` among its siblings to after `after_panel` (0: first).
pub const WHEELHOUSE_EDIT_REORDER: u32 = 12;
/// Moves the boundary after `panel` by `delta`, keeping both sides `floor`.
pub const WHEELHOUSE_EDIT_RESIZE: u32 = 13;
/// Evens the boundary after `panel`.
pub const WHEELHOUSE_EDIT_EQUALIZE: u32 = 14;
/// Every tab of `tab` becomes `to`.
pub const WHEELHOUSE_EDIT_READDRESS: u32 = 15;

/// One edit: `kind` and the fields it reads.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseEdit {
    /// WHEELHOUSE_EDIT_*.
    pub kind: u32,
    /// WHEELHOUSE_DIR_*.
    pub dir: u32,
    /// The panel edited, split, added to or dropped into.
    pub panel: u64,
    /// A reorder's: the sibling it goes after, or 0 for first.
    pub after_panel: u64,
    pub has_tab: u32,
    pub tab: WheelhouseTab,
    pub has_after: u32,
    pub after: WheelhouseTab,
    /// A readdress's new address.
    pub to: WheelhouseTab,
    pub weight: f32,
    pub delta: f32,
    pub floor: f32,
    pub select: u32,
    pub may_close: u32,
}

/// What an edit or intent did (core: arrangement::Outcome).
#[repr(C)]
pub struct WheelhouseEditOutcome {
    /// Nonzero if it was applied. A refused edit changed nothing, but for a
    /// drop's split made before its tab was refused.
    pub applied: u32,
    /// The panel made (split, add, clear) or landed in (drop), or 0.
    pub panel: u64,
    /// The panel a drop's tab left, when this arrangement had it, or 0.
    pub source: u64,
    /// The leaf that takes focus after a close, if the closed panel had it,
    /// or 0.
    pub heir: u64,
    /// Tabs this edit removed (now at the end of the document's).
    pub removed: *const WheelhouseTab,
    pub removed_count: usize,
}

/// The edited document and what the edit did.
#[repr(C)]
pub struct WheelhouseEditResult {
    pub doc: WheelhouseEditDoc,
    pub outcome: WheelhouseEditOutcome,
    /// For an intent whose dropped tab was in another arrangement: nonzero,
    /// with that arrangement edited and its outcome.
    pub has_source: u32,
    pub source_doc: WheelhouseEditDoc,
    pub source_outcome: WheelhouseEditOutcome,
}

/// Which tabs a settle passes over (the native shell's: Views the current
/// project doesn't show): nonzero to hide `tab`. NULL hides none.
pub type WheelhouseTabRule = Option<unsafe extern "C" fn(context: *mut c_void, tab: *const WheelhouseTab) -> u32>;

/// A host's document as the core's.
pub(crate) unsafe fn read_edit_doc(doc: &WheelhouseEditDoc) -> Result<Arrangement<WheelhouseTab>, String> {
    let views = slice(doc.panels, doc.panel_count)?;
    let mut built: Vec<Option<Panel<WheelhouseTab>>> = Vec::with_capacity(views.len());
    for (index, view) in views.iter().enumerate() {
        if (index == 0) != (view.parent == NONE) || (view.parent != NONE && view.parent >= index) {
            return Err(format!("panel {index}'s parent is not an earlier panel, or it is a second root"));
        }
        let mut panel = Panel::new(PanelId(view.id));
        panel.weight = view.weight;
        panel.restore_emptied = view.restore_emptied != 0;
        panel.options_from = (view.options_from != 0).then_some(PanelId(view.options_from));
        let named = view.named.read()?;
        panel.named = (!named.is_empty()).then_some(named);
        panel.tabs = slice(view.tabs, view.tab_count)?.to_vec();
        panel.selected = (view.has_selected != 0).then_some(view.selected);
        built.push(Some(panel));
    }
    for index in (1..built.len()).rev() {
        let child = built[index].take().expect("attached once");
        built[views[index].parent].as_mut().expect("a parent is attached after its children").children.insert(0, child);
    }
    Ok(Arrangement {
        root: built.into_iter().next().flatten(),
        root_axis: match doc.root_axis {
            WHEELHOUSE_AXIS_ROW => Axis::Row,
            WHEELHOUSE_AXIS_COLUMN => Axis::Column,
            other => return Err(format!("unknown axis {other}")),
        },
        next_id: doc.next_id,
        removed: slice(doc.removed, doc.removed_count)?.to_vec(),
    })
}

/// A core document as a host reads one, and what it points into.
pub(crate) struct EditDoc {
    panels: Vec<WheelhouseEditPanel>,
    _tabs: Vec<Vec<WheelhouseTab>>,
    _names: Vec<String>,
    removed: Vec<WheelhouseTab>,
    root_axis: u32,
    next_id: u64,
}

impl EditDoc {
    pub(crate) fn of(mut a: Arrangement<WheelhouseTab>) -> Self {
        let mut out = EditDoc {
            panels: vec![],
            _tabs: vec![],
            _names: vec![],
            removed: std::mem::take(&mut a.removed),
            root_axis: if a.root_axis == Axis::Row { WHEELHOUSE_AXIS_ROW } else { WHEELHOUSE_AXIS_COLUMN },
            next_id: a.next_id,
        };
        // Preorder, in a loop: a document may nest deeply.
        let mut stack: Vec<(Panel<WheelhouseTab>, usize)> = a.root.take().into_iter().map(|root| (root, NONE)).collect();
        while let Some((mut panel, parent)) = stack.pop() {
            let index = out.panels.len();
            let tabs = std::mem::take(&mut panel.tabs);
            let name = panel.named.take().unwrap_or_default();
            out.panels.push(WheelhouseEditPanel {
                parent,
                id: panel.id.0,
                weight: panel.weight,
                restore_emptied: panel.restore_emptied as u32,
                options_from: panel.options_from.map_or(0, |p| p.0),
                named: Text::borrowed(&name),
                tabs: tabs.as_ptr(),
                tab_count: tabs.len(),
                has_selected: panel.selected.is_some() as u32,
                selected: panel.selected.unwrap_or(WheelhouseTab { bytes: [0; WHEELHOUSE_TAB_BYTES] }),
            });
            out._tabs.push(tabs);
            out._names.push(name);
            stack.extend(std::mem::take(&mut panel.children).into_iter().rev().map(|child| (child, index)));
        }
        out
    }

    pub(crate) fn doc(&self) -> WheelhouseEditDoc {
        WheelhouseEditDoc {
            panels: self.panels.as_ptr(),
            panel_count: self.panels.len(),
            root_axis: self.root_axis,
            next_id: self.next_id,
            removed: self.removed.as_ptr(),
            removed_count: self.removed.len(),
        }
    }
}

pub(crate) fn dir(raw: u32) -> Result<Option<Dir>, String> {
    Ok(match raw {
        WHEELHOUSE_DIR_LEFT => Some(Dir::Left),
        WHEELHOUSE_DIR_UP => Some(Dir::Up),
        WHEELHOUSE_DIR_RIGHT => Some(Dir::Right),
        WHEELHOUSE_DIR_DOWN => Some(Dir::Down),
        WHEELHOUSE_DIR_NONE => None,
        other => return Err(format!("unknown direction {other}")),
    })
}

fn some_dir(raw: u32) -> Result<Dir, String> {
    dir(raw)?.ok_or_else(|| "a split needs a direction".into())
}

fn panel(raw: u64) -> PanelId {
    PanelId(raw)
}

fn option_panel(raw: u64) -> Option<PanelId> {
    (raw != 0).then_some(PanelId(raw))
}

/// The core's edit for a host's.
pub(crate) fn read_edit(e: &WheelhouseEdit) -> Result<Edit<WheelhouseTab>, String> {
    let tab = (e.has_tab != 0).then_some(e.tab);
    let after = (e.has_after != 0).then_some(e.after);
    let need_tab = || tab.ok_or_else(|| "this edit needs a tab".to_string());
    Ok(match e.kind {
        WHEELHOUSE_EDIT_CLEAR => Edit::Clear,
        WHEELHOUSE_EDIT_DISCARD => Edit::Discard,
        WHEELHOUSE_EDIT_SPLIT => Edit::Split { panel: panel(e.panel), dir: some_dir(e.dir)? },
        WHEELHOUSE_EDIT_ADD => Edit::Add { parent: option_panel(e.panel), weight: e.weight },
        WHEELHOUSE_EDIT_REMOVE => Edit::Remove { panel: panel(e.panel) },
        WHEELHOUSE_EDIT_CLOSE => Edit::Close { panel: panel(e.panel) },
        WHEELHOUSE_EDIT_INSERT_TAB => Edit::InsertTab { tab: need_tab()?, to: panel(e.panel), after, select: e.select != 0 },
        WHEELHOUSE_EDIT_DETACH_TAB => Edit::DetachTab { tab: need_tab()? },
        WHEELHOUSE_EDIT_REMOVE_TAB => Edit::RemoveTab { tab: need_tab()? },
        WHEELHOUSE_EDIT_DROP => Edit::Drop { tab, to: panel(e.panel), dir: dir(e.dir)?, after },
        WHEELHOUSE_EDIT_SETTLE => Edit::Settle { panel: panel(e.panel), dir: dir(e.dir)?, may_close: e.may_close != 0 },
        WHEELHOUSE_EDIT_SELECT => Edit::Select { panel: panel(e.panel), tab },
        WHEELHOUSE_EDIT_REORDER => Edit::Reorder { panel: panel(e.panel), after: option_panel(e.after_panel) },
        WHEELHOUSE_EDIT_RESIZE => Edit::Resize { panel: panel(e.panel), delta: e.delta, floor: e.floor },
        WHEELHOUSE_EDIT_EQUALIZE => Edit::Equalize { panel: panel(e.panel) },
        WHEELHOUSE_EDIT_READDRESS => Edit::Readdress { from: need_tab()?, to: e.to },
        other => return Err(format!("unknown edit {other}")),
    })
}

/// The hidden rule as a closure. Callbacks never unwind.
unsafe fn hidden_rule(rule: WheelhouseTabRule, context: *mut c_void) -> impl Fn(&WheelhouseTab) -> bool {
    move |tab: &WheelhouseTab| rule.is_some_and(|f| unsafe { f(context, tab) } != 0)
}

struct Owned {
    doc: EditDoc,
    removed: Vec<WheelhouseTab>,
    source: Option<(EditDoc, Vec<WheelhouseTab>)>,
}

fn outcome_view(o: &Outcome<WheelhouseTab>, removed: &[WheelhouseTab]) -> WheelhouseEditOutcome {
    WheelhouseEditOutcome {
        applied: o.applied as u32,
        panel: o.panel.map_or(0, |p| p.0),
        source: o.source.map_or(0, |p| p.0),
        heir: o.heir.map_or(0, |p| p.0),
        removed: removed.as_ptr(),
        removed_count: removed.len(),
    }
}

fn empty_doc() -> WheelhouseEditDoc {
    WheelhouseEditDoc {
        panels: ptr::null(),
        panel_count: 0,
        root_axis: WHEELHOUSE_AXIS_COLUMN,
        next_id: 1,
        removed: ptr::null(),
        removed_count: 0,
    }
}

fn empty_outcome() -> WheelhouseEditOutcome {
    WheelhouseEditOutcome { applied: 0, panel: 0, source: 0, heir: 0, removed: ptr::null(), removed_count: 0 }
}

fn result(
    a: Arrangement<WheelhouseTab>,
    out: Outcome<WheelhouseTab>,
    source: Option<(Arrangement<WheelhouseTab>, Outcome<WheelhouseTab>)>,
) -> *mut WheelhouseEditResult {
    let source_outcome = source.as_ref().map(|(_, o)| o.clone());
    let owned = Owned { doc: EditDoc::of(a), removed: out.removed.clone(), source: source.map(|(s, o)| (EditDoc::of(s), o.removed)) };
    let value = WheelhouseEditResult {
        doc: owned.doc.doc(),
        outcome: outcome_view(&out, &owned.removed),
        has_source: owned.source.is_some() as u32,
        source_doc: owned.source.as_ref().map_or_else(empty_doc, |(d, _)| d.doc()),
        source_outcome: match (&source_outcome, &owned.source) {
            (Some(o), Some((_, removed))) => outcome_view(o, removed),
            _ => empty_outcome(),
        },
    };
    give(value, owned)
}

/// A call without a sidebar handle: errors and panics become a message.
pub(crate) unsafe fn pure<T>(error: *mut *mut c_char, f: impl FnOnce() -> Result<T, String>) -> Option<T> {
    if !error.is_null() {
        *error = ptr::null_mut();
    }
    match catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|_| Err("core panicked".into())) {
        Ok(value) => Some(value),
        Err(message) => {
            if !error.is_null() {
                *error = string(message);
            }
            None
        }
    }
}

/// Applies `edit` to `doc` by the core's rules, with `hidden` (and its
/// `context`) saying which tabs a settle passes over. Returns the edited
/// document and what the edit did, or NULL with an error for a malformed
/// document or edit. A refused edit is not an error: its outcome isn't
/// applied.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_apply(
    doc: *const WheelhouseEditDoc,
    edit: *const WheelhouseEdit,
    hidden: WheelhouseTabRule,
    context: *mut c_void,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseEditResult {
    pure(error_out, || {
        let mut a = read_edit_doc(doc.as_ref().ok_or("null document")?)?;
        let edit = read_edit(edit.as_ref().ok_or("null edit")?)?;
        let rule = hidden_rule(hidden, context);
        let out = apply(&mut a, &edit, &rule);
        Ok(result(a, out, None))
    })
    .unwrap_or(ptr::null_mut())
}

/// The document's first broken invariant, or 0 with *problem_out NULL. The
/// problem is owned: free it with wheelhouse_string_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_problem(
    doc: *const WheelhouseEditDoc,
    problem_out: *mut *mut c_char,
    error_out: *mut *mut c_char,
) -> u32 {
    if !problem_out.is_null() {
        *problem_out = ptr::null_mut();
    }
    pure(error_out, || {
        let found = problem(&read_edit_doc(doc.as_ref().ok_or("null document")?)?);
        if let (Some(text), false) = (&found, problem_out.is_null()) {
            *problem_out = string(text.clone());
        }
        Ok(found.is_some() as u32)
    })
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_edit_result_free(result: *mut WheelhouseEditResult) {
    take::<WheelhouseEditResult, Owned>(result);
}

/// Arrangement intents (core: arrangement::Intent), by their names for the
/// Automation Endpoint (wheelhouse_arrangement_intent_name).
/// Selects `tab` (if `has_tab`; else none) in `panel`.
pub const WHEELHOUSE_INTENT_SELECT_TAB: u32 = 0;
/// Moves `tab` into `panel` after `after` (if `has_after`), selected.
pub const WHEELHOUSE_INTENT_MOVE_TAB: u32 = 1;
/// Removes `tab`, closing its panel when that leaves it empty and unnamed;
/// the heir in the outcome.
pub const WHEELHOUSE_INTENT_CLOSE_TAB: u32 = 2;
/// A tab drag's drop of `tab` into `panel`, or a split of it towards `dir`,
/// after `after`. The panel it left is settled where it is: in a source
/// document passed with the intent when that arrangement is another's.
pub const WHEELHOUSE_INTENT_DROP_TAB: u32 = 3;
/// Splits `panel` towards `dir`.
pub const WHEELHOUSE_INTENT_SPLIT_PANEL: u32 = 4;
/// Closes `panel`; the heir in the outcome.
pub const WHEELHOUSE_INTENT_CLOSE_PANEL: u32 = 5;
/// Moves `panel` after `after_panel` (0: first) among its siblings.
pub const WHEELHOUSE_INTENT_REORDER_PANEL: u32 = 6;
/// A boundary drag's release: the boundary after `panel` moves by `delta`,
/// keeping both sides `floor`.
pub const WHEELHOUSE_INTENT_RESIZE_PANEL: u32 = 7;
/// A boundary double-click: the boundary after `panel` evens.
pub const WHEELHOUSE_INTENT_EQUALIZE_PANEL: u32 = 8;

/// A Workspace's arrangement.
pub const WHEELHOUSE_OWNER_WORKSPACE: u32 = 0;
/// A window's sidebar dock.
pub const WHEELHOUSE_OWNER_SIDEBAR: u32 = 1;

/// One intent: `kind`, its owner, and the fields it reads.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseArrangementIntent {
    /// WHEELHOUSE_INTENT_*.
    pub kind: u32,
    /// WHEELHOUSE_OWNER_*, and the owner's Workspace ID (the sidebar's:
    /// its window's Window ID).
    pub owner_kind: u32,
    pub owner: WorkspaceIdView,
    /// WHEELHOUSE_DIR_*.
    pub dir: u32,
    pub panel: u64,
    pub after_panel: u64,
    pub has_tab: u32,
    pub tab: WheelhouseTab,
    pub has_after: u32,
    pub after: WheelhouseTab,
    pub delta: f32,
    pub floor: f32,
}

pub(crate) fn read_intent(i: &WheelhouseArrangementIntent) -> Result<Intent<WheelhouseTab>, String> {
    let owner = Owner {
        kind: match i.owner_kind {
            WHEELHOUSE_OWNER_WORKSPACE => OwnerKind::Workspace,
            WHEELHOUSE_OWNER_SIDEBAR => OwnerKind::Sidebar,
            other => return Err(format!("unknown owner kind {other}")),
        },
        id: i.owner.into(),
    };
    let tab = (i.has_tab != 0).then_some(i.tab);
    let after = (i.has_after != 0).then_some(i.after);
    let need_tab = || tab.ok_or_else(|| "this intent needs a tab".to_string());
    let p = panel(i.panel);
    Ok(match i.kind {
        WHEELHOUSE_INTENT_SELECT_TAB => Intent::SelectTab { owner, panel: p, tab },
        WHEELHOUSE_INTENT_MOVE_TAB => Intent::MoveTab { owner, tab: need_tab()?, to: p, after },
        WHEELHOUSE_INTENT_CLOSE_TAB => Intent::CloseTab { owner, tab: need_tab()? },
        WHEELHOUSE_INTENT_DROP_TAB => Intent::DropTab { owner, tab: need_tab()?, to: p, dir: dir(i.dir)?, after },
        WHEELHOUSE_INTENT_SPLIT_PANEL => Intent::SplitPanel { owner, panel: p, dir: some_dir(i.dir)? },
        WHEELHOUSE_INTENT_CLOSE_PANEL => Intent::ClosePanel { owner, panel: p },
        WHEELHOUSE_INTENT_REORDER_PANEL => Intent::ReorderPanel { owner, panel: p, after: option_panel(i.after_panel) },
        WHEELHOUSE_INTENT_RESIZE_PANEL => Intent::ResizePanel { owner, panel: p, delta: i.delta, floor: i.floor },
        WHEELHOUSE_INTENT_EQUALIZE_PANEL => Intent::EqualizePanel { owner, panel: p },
        other => return Err(format!("unknown intent {other}")),
    })
}

/// Applies `intent` to `doc`, its owner's arrangement. For a drop whose tab
/// is in another owner's arrangement, `source` is that one (else NULL),
/// and the result has it edited too. As wheelhouse_arrangement_apply
/// otherwise.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_apply_intent(
    doc: *const WheelhouseEditDoc,
    intent: *const WheelhouseArrangementIntent,
    source: *const WheelhouseEditDoc,
    hidden: WheelhouseTabRule,
    context: *mut c_void,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseEditResult {
    pure(error_out, || {
        let mut a = read_edit_doc(doc.as_ref().ok_or("null document")?)?;
        let intent = read_intent(intent.as_ref().ok_or("null intent")?)?;
        let mut source = source.as_ref().map(|s| read_edit_doc(s)).transpose()?;
        let rule = hidden_rule(hidden, context);
        let out = apply_intent(&mut a, &intent, source.as_mut(), &rule);
        let source = match (source, out.source) {
            (Some(s), Some(o)) => Some((s, o)),
            _ => None,
        };
        Ok(result(a, out.outcome, source))
    })
    .unwrap_or(ptr::null_mut())
}

/// An intent's stable name for the Automation Endpoint ("select-tab"),
/// static; empty for an unknown kind.
#[no_mangle]
pub extern "C" fn wheelhouse_arrangement_intent_name(kind: u32) -> Text {
    let owner = Owner { kind: OwnerKind::Workspace, id: WorkspaceId::from_bytes([0; 16]) };
    let p = PanelId(0);
    let tab = WheelhouseTab { bytes: [0; WHEELHOUSE_TAB_BYTES] };
    let intent: Option<Intent<WheelhouseTab>> = match kind {
        WHEELHOUSE_INTENT_SELECT_TAB => Some(Intent::SelectTab { owner, panel: p, tab: None }),
        WHEELHOUSE_INTENT_MOVE_TAB => Some(Intent::MoveTab { owner, tab, to: p, after: None }),
        WHEELHOUSE_INTENT_CLOSE_TAB => Some(Intent::CloseTab { owner, tab }),
        WHEELHOUSE_INTENT_DROP_TAB => Some(Intent::DropTab { owner, tab, to: p, dir: None, after: None }),
        WHEELHOUSE_INTENT_SPLIT_PANEL => Some(Intent::SplitPanel { owner, panel: p, dir: Dir::Left }),
        WHEELHOUSE_INTENT_CLOSE_PANEL => Some(Intent::ClosePanel { owner, panel: p }),
        WHEELHOUSE_INTENT_REORDER_PANEL => Some(Intent::ReorderPanel { owner, panel: p, after: None }),
        WHEELHOUSE_INTENT_RESIZE_PANEL => Some(Intent::ResizePanel { owner, panel: p, delta: 0.0, floor: 0.0 }),
        WHEELHOUSE_INTENT_EQUALIZE_PANEL => Some(Intent::EqualizePanel { owner, panel: p }),
        _ => None,
    };
    Text::borrowed(intent.map_or("", |i| i.name()))
}

/// A weight as an edit writes it: the native config formatter's six
/// places, read back (core: arrangement::quantize).
#[no_mangle]
pub extern "C" fn wheelhouse_arrangement_quantize(weight: f32) -> f32 {
    wheelhouse_core::arrangement::quantize(weight)
}

/// The sibling the boundary after child `i` of a split pairs it with, given
/// the `len` children's weights: the next one with space. `len` when there is
/// none (the child has no space, or nothing with space follows it). The
/// core's rule (arrangement::boundary_partner), for the shell to draw
/// splitters by.
///
/// # Safety
/// `weights` is `len` floats, or NULL when `len` is 0.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_boundary_partner(weights: *const f32, len: usize, i: usize) -> usize {
    let weights = if len == 0 { &[] } else { std::slice::from_raw_parts(weights, len) };
    wheelhouse_core::arrangement::boundary_partner(weights, i).unwrap_or(len)
}
