//! Arrangement documents, a workspace's and the sidebar's, as plain structs.
//!
//! A document is an array of panels, each naming its parent by index: a
//! host writes one by walking its tree in preorder, and reads one in a
//! single pass, linking each panel to its parent. A document the core
//! returns is one allocation the host frees with its `_free` call: every
//! pointer in it, and all its text, stays valid until then. A document the
//! host passes in is only read, during the call.
use super::*;
use wheelhouse_core::{
    sidebar::LayoutInfo,
    sidebar_arrangement::{Report, SidebarDoc, SidebarView},
    slots::{ArrangementDoc, ArrangementView, Panel, PanelNode, Resolve},
    suggested_layout::Axis,
    versioned::CommitError,
};

/// A split panel: its children are the panels that name it as their parent,
/// in array order, laid out along its axis.
pub const WHEELHOUSE_PANEL_SPLIT: u32 = 0;
/// A tab panel: its tabs are slot keys (a workspace's) or section keys (the
/// sidebar's).
pub const WHEELHOUSE_PANEL_TABS: u32 = 1;
/// Children left to right.
pub const WHEELHOUSE_AXIS_ROW: u32 = 0;
/// Children top to bottom.
pub const WHEELHOUSE_AXIS_COLUMN: u32 = 1;

/// A panel of an arrangement document.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseDocPanel {
    /// The index of an earlier SPLIT panel in the same array, or
    /// WHEELHOUSE_NONE for a root.
    pub parent: usize,
    /// Stable within its document; nonempty and unique.
    pub id: Text,
    /// Relative to its siblings: finite and positive. A root's is kept but
    /// means nothing.
    pub weight: f64,
    /// WHEELHOUSE_PANEL_SPLIT or WHEELHOUSE_PANEL_TABS.
    pub kind: u32,
    /// A split's WHEELHOUSE_AXIS_*.
    pub axis: u32,
    /// A tab panel's keys, in tab order; at least one. None for a split.
    pub tabs: *const Text,
    pub tab_count: usize,
    /// The Selected View: an index into tabs, or WHEELHOUSE_NONE.
    pub selected: usize,
}

/// Panels, each after its parent. A split has at least one child.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseArrangementDoc {
    pub panels: *const WheelhouseDocPanel,
    pub panel_count: usize,
}

/// The sidebar's document: the dock, one tree or none.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseSidebarDoc {
    pub dock: WheelhouseArrangementDoc,
}

/// A key the core placed since the host last committed.
pub const WHEELHOUSE_NOTE_PLACED: u32 = 0;
/// A workspace tab whose slot has gone. It stays until the host removes it.
pub const WHEELHOUSE_NOTE_GONE: u32 = 1;
/// A key that no longer resolves, kept until the host drops it: a soft
/// override's panel ID (a workspace's); a tabbed or closed section, or a
/// saved dashboard key ("display:", "collapse:", "order:" or "variable:"
/// and the key) the template no longer has (the sidebar's).
pub const WHEELHOUSE_NOTE_UNRESOLVED: u32 = 2;
/// A declared section with no tab, closed on purpose.
pub const WHEELHOUSE_NOTE_CLOSED: u32 = 3;
/// A closed section that has a tab again.
pub const WHEELHOUSE_NOTE_RESTORED: u32 = 4;
/// A key whose later tabs were removed.
pub const WHEELHOUSE_NOTE_DUPLICATE: u32 = 5;
/// A workspace slot the Workspace Layout doesn't show, in slot order. It
/// still resolves; a commit that gives it a tab places it.
pub const WHEELHOUSE_NOTE_UNPLACED: u32 = 6;

#[repr(C)]
pub struct WheelhouseNote {
    /// WHEELHOUSE_NOTE_*.
    pub kind: u32,
    pub key: Text,
    /// For an UNPLACED slot, the tab panel showing it puts it in: the first
    /// whose template matcher takes it, ignoring hide, else the first. Empty
    /// for other notes.
    pub panel: Text,
}

/// The provider changed its hint, or the configuration the Workspace
/// Layout's template, while the user owns the document.
pub const WHEELHOUSE_ARRANGEMENT_FLAG_PROVIDER_CHANGED: u32 = 1;
/// Soft overrides apply.
pub const WHEELHOUSE_ARRANGEMENT_FLAG_SOFT: u32 = 2;
/// A soft override no longer resolves (an UNRESOLVED note).
pub const WHEELHOUSE_ARRANGEMENT_FLAG_UNRESOLVED: u32 = 4;
/// A soft override sets a panel's weight: the user resized a layout the
/// provider still arranges, and resetting it gives its weights back.
pub const WHEELHOUSE_ARRANGEMENT_FLAG_RESIZED: u32 = 8;

/// A workspace's arrangement as stored, or after a commit. While the user
/// doesn't own it, it is derived from the provider's hint, less slots the
/// user removed, every slot placed, with soft overrides (weights and
/// selected tabs, keyed by panel ID) applied. A structural commit (a split,
/// move, close, reorder or added tab) makes the user own it; a changed hint
/// is then flagged PROVIDER_CHANGED until wheelhouse_arrangement_resolve.
///
/// A workspace whose subject kind has a template shows a Workspace Layout
/// instead of the hint: the template evaluated over the slots, less those
/// it leaves out, noted UNPLACED. Once owned, a new slot goes to the first
/// surviving panel whose template matcher takes it, or is unplaced; a slot
/// a commit gives no tab is unplaced. Each layout keeps its own edits.
#[repr(C)]
pub struct WheelhouseWorkspaceArrangement {
    /// WHEELHOUSE_COMMITTED, or WHEELHOUSE_STALE after a commit at another
    /// generation: nothing changed, and this is the current document.
    pub status: u32,
    /// 0 until a document is stored. Never reused, and never kept across
    /// sessions: commit at the generation last read.
    pub generation: u64,
    pub owned: u32,
    /// WHEELHOUSE_ARRANGEMENT_FLAG_* bits.
    pub flags: u32,
    /// The Workspace Layout this arranges, when has_layout is nonzero.
    pub has_layout: u32,
    pub layout: Text,
    /// At most one root.
    pub doc: WheelhouseArrangementDoc,
    pub notes: *const WheelhouseNote,
    pub note_count: usize,
}

/// The Dashboard's sidebar arrangement: which sections are docked where.
/// Tabs hold section keys: a region's name, ".section:<id>" for a local
/// section, ".unplaced" for the workspace fallback, or "u:<id>" for a view
/// of the host's own, which the core stores but never places or flags.
#[repr(C)]
pub struct WheelhouseSidebarArrangement {
    /// As for a workspace's arrangement.
    pub status: u32,
    pub generation: u64,
    /// The host has committed since the last reset.
    pub owned: u32,
    pub doc: WheelhouseSidebarDoc,
    /// Every CLOSED section and UNRESOLVED key, every section PLACED since
    /// the host last committed (so a stale commit still names what was
    /// placed meanwhile), and what the last reconciliation or call did:
    /// RESTORED and DUPLICATE.
    pub notes: *const WheelhouseNote,
    pub note_count: usize,
}

/// Text a document's panels and notes borrow. The views own the strings,
/// whose bytes don't move when the views do.
struct Panels {
    panels: Vec<WheelhouseDocPanel>,
    tabs: Vec<Vec<Text>>,
}

impl Panels {
    /// Trees in preorder, each panel naming its parent.
    fn of<'a>(roots: impl IntoIterator<Item = &'a Panel>) -> Self {
        fn walk(panel: &Panel, parent: usize, out: &mut Panels) {
            let index = out.panels.len();
            let mut c = WheelhouseDocPanel {
                parent,
                id: Text::borrowed(&panel.id),
                weight: panel.weight,
                kind: WHEELHOUSE_PANEL_TABS,
                axis: WHEELHOUSE_AXIS_ROW,
                tabs: ptr::null(),
                tab_count: 0,
                selected: NONE,
            };
            match &panel.node {
                PanelNode::Split { axis, children } => {
                    c.kind = WHEELHOUSE_PANEL_SPLIT;
                    if *axis == Axis::Column {
                        c.axis = WHEELHOUSE_AXIS_COLUMN;
                    }
                    out.panels.push(c);
                    for child in children {
                        walk(child, index, out);
                    }
                }
                PanelNode::Tabs { tabs, selected } => {
                    let texts: Vec<Text> = tabs.iter().map(|tab| Text::borrowed(tab)).collect();
                    (c.tabs, c.tab_count) = (texts.as_ptr(), texts.len());
                    c.selected = selected.as_ref().and_then(|key| tabs.iter().position(|tab| tab == key)).unwrap_or(NONE);
                    out.tabs.push(texts);
                    out.panels.push(c);
                }
            }
        }
        let mut out = Self { panels: Vec::new(), tabs: Vec::new() };
        for root in roots {
            walk(root, NONE, &mut out);
        }
        out
    }

    fn doc(&self) -> WheelhouseArrangementDoc {
        WheelhouseArrangementDoc { panels: self.panels.as_ptr(), panel_count: self.panels.len() }
    }
}

/// A host's document as trees, one per root.
unsafe fn read_doc(doc: &WheelhouseArrangementDoc) -> Result<Vec<Panel>, String> {
    let views = slice(doc.panels, doc.panel_count)?;
    let mut built: Vec<Option<Panel>> = Vec::with_capacity(views.len());
    for (index, view) in views.iter().enumerate() {
        if view.parent != NONE && (view.parent >= index || views[view.parent].kind != WHEELHOUSE_PANEL_SPLIT) {
            return Err(format!("panel {index}'s parent is not an earlier split"));
        }
        let node = match view.kind {
            WHEELHOUSE_PANEL_SPLIT if view.tab_count == 0 => PanelNode::Split {
                axis: match view.axis {
                    WHEELHOUSE_AXIS_ROW => Axis::Row,
                    WHEELHOUSE_AXIS_COLUMN => Axis::Column,
                    other => return Err(format!("unknown axis {other}")),
                },
                children: Vec::new(),
            },
            WHEELHOUSE_PANEL_SPLIT => return Err(format!("split {index} has tabs")),
            WHEELHOUSE_PANEL_TABS => {
                let tabs = slice(view.tabs, view.tab_count)?.iter().map(|tab| tab.read()).collect::<Result<Vec<_>, _>>()?;
                let selected = match view.selected {
                    NONE => None,
                    n => Some(tabs.get(n).ok_or_else(|| format!("panel {index} selects a tab it doesn't have"))?.clone()),
                };
                PanelNode::Tabs { tabs, selected }
            }
            other => return Err(format!("unknown panel kind {other}")),
        };
        built.push(Some(Panel { id: view.id.read()?, weight: view.weight, node }));
    }
    // Attach children last first, so each is complete when attached.
    for index in (0..built.len()).rev() {
        let parent = views[index].parent;
        if parent == NONE {
            continue;
        }
        let child = built[index].take().expect("attached once");
        if let Some(Panel { node: PanelNode::Split { children, .. }, .. }) = &mut built[parent] {
            children.insert(0, child);
        }
    }
    Ok(built.into_iter().flatten().collect())
}

/// At most one root.
fn one_root(mut roots: Vec<Panel>) -> Result<Option<Panel>, String> {
    let root = roots.pop();
    if !roots.is_empty() {
        return Err("a document has one root".into());
    }
    Ok(root)
}

fn notes<'a>(groups: impl IntoIterator<Item = (u32, Vec<&'a String>)>) -> Vec<WheelhouseNote> {
    groups
        .into_iter()
        .flat_map(|(kind, keys)| {
            keys.into_iter().map(move |key| WheelhouseNote { kind, key: Text::borrowed(key), panel: Text::borrowed("") })
        })
        .collect()
}

/// A compare-and-set's result as a status, or the error for an invalid one.
fn status<T>(result: Result<T, CommitError>) -> Result<u32, String> {
    match result {
        Ok(_) => Ok(WHEELHOUSE_COMMITTED),
        Err(CommitError::Stale { .. }) => Ok(WHEELHOUSE_STALE),
        Err(CommitError::Invalid(error)) => Err(error),
    }
}

fn workspace_c(view: ArrangementView, status: u32) -> *mut WheelhouseWorkspaceArrangement {
    let panels = Panels::of(&view.doc.root);
    let mut notes = notes([
        (WHEELHOUSE_NOTE_PLACED, view.placed.iter().collect()),
        (WHEELHOUSE_NOTE_GONE, view.gone.iter().collect()),
        (WHEELHOUSE_NOTE_UNRESOLVED, view.unresolved.iter().collect()),
    ]);
    notes.extend(view.unplaced.iter().map(|key| WheelhouseNote {
        kind: WHEELHOUSE_NOTE_UNPLACED,
        key: Text::borrowed(key),
        panel: Text::borrowed(view.homes.get(key).map(String::as_str).unwrap_or_default()),
    }));
    let flags = [
        (view.provider_changed, WHEELHOUSE_ARRANGEMENT_FLAG_PROVIDER_CHANGED),
        (!view.soft.is_empty(), WHEELHOUSE_ARRANGEMENT_FLAG_SOFT),
        (!view.unresolved.is_empty(), WHEELHOUSE_ARRANGEMENT_FLAG_UNRESOLVED),
        (view.soft.values().any(|edit| edit.weight.is_some()), WHEELHOUSE_ARRANGEMENT_FLAG_RESIZED),
    ];
    let value = WheelhouseWorkspaceArrangement {
        status,
        generation: view.generation,
        owned: view.owned as u32,
        flags: flags.iter().filter(|(on, _)| *on).map(|(_, bit)| bit).sum(),
        has_layout: view.layout.is_some() as u32,
        layout: Text::borrowed(view.layout.as_deref().unwrap_or_default()),
        doc: panels.doc(),
        notes: notes.as_ptr(),
        note_count: notes.len(),
    };
    give(value, (view, panels, notes))
}
type WorkspaceKeep = (ArrangementView, Panels, Vec<WheelhouseNote>);

/// A workspace's arrangement. Free it with wheelhouse_arrangement_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_get(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseWorkspaceArrangement {
    run(h, error_out, |h| {
        let view = h.sidebar.arrangement(workspace.into())?;
        Ok(workspace_c(view, WHEELHOUSE_COMMITTED))
    })
    .unwrap_or(ptr::null_mut())
}

/// Commit a workspace's whole arrangement at the end of a gesture, if
/// expected_generation is the current one (0 before any), and return the
/// arrangement after: COMMITTED or STALE. NULL with an error, and no
/// change, for an invalid document. A tab names a slot, or a slot the
/// stored document already tabbed that has since gone (noted GONE); each
/// slot has at most one tab. Slots with no tab are appended to the first
/// tab panel in preorder (selected there if it selects nothing) and noted
/// PLACED; in a Workspace Layout they are unplaced instead. Neither this nor
/// any slot call changes the snapshot's revision.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_commit(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    doc: *const WheelhouseArrangementDoc,
    expected_generation: u64,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseWorkspaceArrangement {
    run(h, error_out, |h| {
        let doc = doc.as_ref().ok_or("null document")?;
        let doc = ArrangementDoc { root: one_root(read_doc(doc)?)? };
        let workspace = workspace.into();
        let status = status(h.sidebar.set_arrangement(workspace, doc, expected_generation))?;
        Ok(workspace_c(h.sidebar.arrangement(workspace)?, status))
    })
    .unwrap_or(ptr::null_mut())
}

/// Keep the user's arrangement over the provider's change.
pub const WHEELHOUSE_ARRANGEMENT_KEEP: u32 = 0;
/// Follow the provider's arrangement, or the Workspace Layout's template,
/// again, dropping ownership and soft overrides: resetting the layout. It
/// need not be PROVIDER_CHANGED.
pub const WHEELHOUSE_ARRANGEMENT_FOLLOW: u32 = 1;

/// Settle a PROVIDER_CHANGED arrangement at expected_generation: KEEP or
/// FOLLOW. Returns WHEELHOUSE_COMMITTED, _STALE or _INVALID (with an
/// error); generation_out, when not NULL, receives the generation after.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_resolve(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    choice: u32,
    expected_generation: u64,
    generation_out: *mut u64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        let choice = match choice {
            WHEELHOUSE_ARRANGEMENT_KEEP => Resolve::Keep,
            WHEELHOUSE_ARRANGEMENT_FOLLOW => Resolve::Follow,
            other => return Err(format!("unknown arrangement choice {other}")),
        };
        let workspace = workspace.into();
        let result = h.sidebar.resolve_arrangement(workspace, choice, expected_generation);
        if let Some(out) = generation_out.as_mut() {
            *out = h.sidebar.arrangement(workspace)?.generation;
        }
        status(result)
    })
    .unwrap_or(WHEELHOUSE_INVALID)
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_arrangement_free(arrangement: *mut WheelhouseWorkspaceArrangement) {
    take::<_, WorkspaceKeep>(arrangement)
}

/// The layout a workspace shows when the user chose none.
pub const WHEELHOUSE_LAYOUT_DEFAULT: u32 = 1;
/// The layout a workspace shows now.
pub const WHEELHOUSE_LAYOUT_CURRENT: u32 = 2;

/// One of a workspace's Workspace Layouts.
#[repr(C)]
pub struct WheelhouseLayout {
    pub name: Text,
    /// The template's glyph for it: 1 to 8 characters, or empty.
    pub glyph: Text,
    /// WHEELHOUSE_LAYOUT_* bits.
    pub flags: u32,
}

/// A workspace's Workspace Layouts, in the template's order: none when its
/// subject's kind has no template.
#[repr(C)]
pub struct WheelhouseLayoutList {
    pub layouts: *const WheelhouseLayout,
    pub count: usize,
}
type LayoutsKeep = (Vec<LayoutInfo>, Vec<WheelhouseLayout>);

/// A workspace's layouts. Free the list with wheelhouse_layouts_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_layouts_get(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseLayoutList {
    run(h, error_out, |h| {
        let infos = h.sidebar.layouts(workspace.into())?;
        let layouts: Vec<WheelhouseLayout> = infos
            .iter()
            .map(|info| WheelhouseLayout {
                name: Text::borrowed(&info.name),
                glyph: Text::borrowed(info.glyph.as_deref().unwrap_or_default()),
                flags: if info.default { WHEELHOUSE_LAYOUT_DEFAULT } else { 0 } | if info.current { WHEELHOUSE_LAYOUT_CURRENT } else { 0 },
            })
            .collect();
        let value = WheelhouseLayoutList { layouts: layouts.as_ptr(), count: layouts.len() };
        Ok(give(value, (infos, layouts)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_layouts_free(list: *mut WheelhouseLayoutList) {
    take::<_, LayoutsKeep>(list)
}

/// Show the layout name in a workspace, or (has_name 0) the default one.
/// The arrangement of the layout shown until now keeps the user's edits for
/// when it is shown again; the arrangement's generation and the content
/// revision move, so a commit prepared against the old layout is STALE.
/// Returns 1, or 0 with an error for a layout the workspace doesn't have.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_layout_choose(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    has_name: u32,
    name: Text,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        let name = if has_name != 0 { Some(name.read()?) } else { None };
        h.sidebar.choose_layout(workspace.into(), name.as_deref())?;
        Ok(())
    })
    .is_some() as u32
}

fn sidebar_c(view: SidebarView, report: &Report, status: u32) -> *mut WheelhouseSidebarArrangement {
    let dock = Panels::of(&view.doc.dock.root);
    let report = report.clone();
    let notes = notes([
        (WHEELHOUSE_NOTE_CLOSED, view.closed.iter().collect()),
        (WHEELHOUSE_NOTE_UNRESOLVED, view.unresolved.iter().collect()),
        (WHEELHOUSE_NOTE_PLACED, view.placed.iter().collect()),
        (WHEELHOUSE_NOTE_RESTORED, report.restored.iter().collect()),
        (WHEELHOUSE_NOTE_DUPLICATE, report.duplicates.iter().collect()),
    ]);
    let value = WheelhouseSidebarArrangement {
        status,
        generation: view.generation,
        owned: view.owned as u32,
        doc: WheelhouseSidebarDoc { dock: dock.doc() },
        notes: notes.as_ptr(),
        note_count: notes.len(),
    };
    give(value, (view, report, dock, notes))
}
type SidebarKeep = (SidebarView, Report, Panels, Vec<WheelhouseNote>);

/// The sidebar arrangement after a compare-and-set.
fn sidebar_after(h: &WheelhouseCore, result: Result<Report, CommitError>) -> Result<*mut WheelhouseSidebarArrangement, String> {
    let status = status(result)?;
    let (view, report) = h.sidebar.sidebar_arrangement();
    Ok(sidebar_c(view, report, status))
}

/// The sidebar arrangement. Free it with wheelhouse_sidebar_arrangement_free.
/// The core reconciles it when what is declared changes (on configure, and
/// when local sections or groups change or are imported): a section with
/// neither a tab nor a closed record is placed in a new tab panel in the
/// dock, before the subtree holding the next section in default order
/// placed there, else last.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_sidebar_arrangement_get(
    h: *mut WheelhouseCore,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseSidebarArrangement {
    run(h, error_out, |h| {
        let (view, report) = h.sidebar.sidebar_arrangement();
        Ok(sidebar_c(view, report, WHEELHOUSE_COMMITTED))
    })
    .unwrap_or(ptr::null_mut())
}

/// Commit the host's whole sidebar document at the end of a gesture, as
/// wheelhouse_arrangement_commit does. A tab names a declared or local
/// section, a host view, or a key the stored document already holds; a key
/// with two tabs keeps the first. Every declared section the document leaves
/// out is closed, and a closed one it tabs again is restored.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_sidebar_arrangement_commit(
    h: *mut WheelhouseCore,
    doc: *const WheelhouseSidebarDoc,
    expected_generation: u64,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseSidebarArrangement {
    run(h, error_out, |h| {
        let doc = doc.as_ref().ok_or("null document")?;
        let doc = SidebarDoc { dock: ArrangementDoc { root: one_root(read_doc(&doc.dock)?)? } };
        let result = h.sidebar.set_sidebar_arrangement(doc, expected_generation);
        sidebar_after(h, result)
    })
    .unwrap_or(ptr::null_mut())
}

/// Reopen a closed (or never placed) declared section by its hints, with an
/// equal share of its host, at expected_generation. A section with a tab is
/// left alone; an undeclared one is invalid.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_sidebar_restore_section(
    h: *mut WheelhouseCore,
    key: Text,
    expected_generation: u64,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseSidebarArrangement {
    run(h, error_out, |h| {
        let result = h.sidebar.restore_sidebar_section(&key.read()?, expected_generation);
        sidebar_after(h, result)
    })
    .unwrap_or(ptr::null_mut())
}

/// Drop the user's sidebar arrangement, closed sections and unresolved keys,
/// and place every declared section by its hints, at expected_generation.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_sidebar_reset(
    h: *mut WheelhouseCore,
    expected_generation: u64,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseSidebarArrangement {
    run(h, error_out, |h| {
        let result = h.sidebar.reset_sidebar_arrangement(expected_generation);
        sidebar_after(h, result)
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_sidebar_arrangement_free(arrangement: *mut WheelhouseSidebarArrangement) {
    take::<_, SidebarKeep>(arrangement)
}

/// The sidebar arrangement's generation: cheap enough to poll, to learn
/// when to read it again.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_sidebar_arrangement_generation(h: *mut WheelhouseCore, error_out: *mut *mut c_char) -> u64 {
    run(h, error_out, |h| Ok(h.sidebar.sidebar_arrangement_generation())).unwrap_or(0)
}

/// The revision of workspace content: slots, their plans and saved
/// resolutions, and arrangements. Poll it to learn when to read them again;
/// it is separate from the snapshot's.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_workspace_content_revision(h: *mut WheelhouseCore, error_out: *mut *mut c_char) -> u64 {
    run(h, error_out, |h| Ok(h.sidebar.content_revision())).unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CStr;
    use wheelhouse_core::tab_workspace_id;

    fn text(s: &str) -> Text {
        Text::borrowed(s)
    }

    /// A document as `id(parent):tabs<selected>` per panel, for comparing.
    unsafe fn shape(doc: WheelhouseArrangementDoc) -> Vec<String> {
        slice(doc.panels, doc.panel_count)
            .unwrap()
            .iter()
            .map(|panel| {
                let tabs = slice(panel.tabs, panel.tab_count).unwrap();
                format!(
                    "{}({}):{}<{}>",
                    panel.id.read().unwrap(),
                    panel.parent as isize,
                    tabs.iter().map(|tab| tab.read().unwrap()).collect::<Vec<_>>().join(","),
                    panel.selected as isize
                )
            })
            .collect()
    }

    fn panel(parent: usize, id: &str, tabs: &[Text], selected: usize) -> WheelhouseDocPanel {
        WheelhouseDocPanel {
            parent,
            id: text(id),
            weight: 0.5,
            kind: if tabs.is_empty() { WHEELHOUSE_PANEL_SPLIT } else { WHEELHOUSE_PANEL_TABS },
            axis: WHEELHOUSE_AXIS_COLUMN,
            tabs: tabs.as_ptr(),
            tab_count: tabs.len(),
            selected,
        }
    }

    fn doc(panels: &[WheelhouseDocPanel]) -> WheelhouseArrangementDoc {
        WheelhouseArrangementDoc { panels: panels.as_ptr(), panel_count: panels.len() }
    }

    #[test]
    fn documents_round_trip_as_panel_arrays() {
        unsafe {
            let config = include_str!("../../wheelhouse-core/tests/fixtures/abbreviation.kdl");
            let h = wheelhouse_create(config.as_ptr(), config.len(), ptr::null_mut());
            let ws: WorkspaceIdView = tab_workspace_id(7).into();
            assert_eq!(wheelhouse_workspace_register(h, ws, ptr::null_mut()), 1);
            for key in ["u:1", "u:2", "u:3"] {
                let mut spec: ViewSpecView = std::mem::zeroed();
                spec.content = WHEELHOUSE_SLOT_URL;
                spec.url = text("https://example.com");
                assert_eq!(wheelhouse_slot_set(h, ws, text(key), &spec, 0, ptr::null_mut()), 1);
            }
            // u:3 is left out: committing places it.
            let tabs = [text("u:1"), text("u:2")];
            let panels = [panel(NONE, "root", &[], NONE), panel(0, "1", &[], NONE), panel(1, "2", &tabs, 1)];
            let committed = wheelhouse_arrangement_commit(h, ws, &doc(&panels), 0, ptr::null_mut());
            let c = &*committed;
            assert_eq!((c.status, c.owned), (WHEELHOUSE_COMMITTED, 1));
            assert_eq!(shape(c.doc), ["root(-1):<-1>", "1(0):<-1>", "2(1):u:1,u:2,u:3<1>"]);
            let notes = slice(c.notes, c.note_count).unwrap();
            assert_eq!(notes.len(), 1);
            assert_eq!((notes[0].kind, notes[0].key.read().unwrap().as_str()), (WHEELHOUSE_NOTE_PLACED, "u:3"));
            // What get returns is what the commit returned.
            let got = wheelhouse_arrangement_get(h, ws, ptr::null_mut());
            assert_eq!((*got).generation, c.generation);
            assert_eq!(shape((*got).doc), shape(c.doc));
            // A stale commit returns the current document.
            let stale = wheelhouse_arrangement_commit(h, ws, &doc(&panels), 0, ptr::null_mut());
            assert_eq!(((*stale).status, (*stale).generation), (WHEELHOUSE_STALE, c.generation));
            // Malformed documents are invalid, with a reason.
            let two_roots = [panel(NONE, "a", &tabs, 0), panel(NONE, "b", &tabs[..1], 0)];
            let later_parent = [panel(1, "a", &tabs, 0), panel(NONE, "b", &[], NONE)];
            let tab_parent = [panel(NONE, "a", &tabs, 0), panel(0, "b", &tabs, 0)];
            let bad_selection = [panel(NONE, "a", &tabs, 5)];
            for (panels, reason) in [
                (&two_roots[..], "one root"),
                (&later_parent[..], "earlier split"),
                (&tab_parent[..], "earlier split"),
                (&bad_selection[..], "selects a tab"),
            ] {
                let mut error = ptr::null_mut();
                let result = wheelhouse_arrangement_commit(h, ws, &doc(panels), c.generation, &mut error);
                assert!(result.is_null());
                let message = CStr::from_ptr(error).to_str().unwrap();
                assert!(message.contains(reason), "{message}");
                wheelhouse_string_free(error);
            }
            for result in [committed, got, stale] {
                wheelhouse_arrangement_free(result);
            }
            wheelhouse_destroy(h);
        }
    }
}

/// The Semantic Tree of the core's registered workspaces, as a JSON array
/// of workspace nodes (docs/protocol/automation-endpoint.md): each its panel
/// tree as shown and the current Views of its Slots. The host adds what
/// only it knows, such as terminals' sizes, by Automation ID. Free the bytes
/// with wheelhouse_bytes_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_semantic_workspaces(h: *mut WheelhouseCore, out: *mut Bytes, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| write_bytes(out, wheelhouse_core::automation::to_json(&h.sidebar.semantic_workspaces()))).is_some() as u32
}
