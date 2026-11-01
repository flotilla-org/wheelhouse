//! Docking validity (#516): the core's rule over a host's registrations,
//! hosts and proposals, and the repair of a window's documents. The host
//! works out what needs pixels or configuration (widths, Controlled Split
//! and level IDs, instance counts); the core judges. Host and level IDs are
//! opaque (the native shell's config IDs); 0 is none.
use super::arrangement::{read_edit_doc, EditDoc, WheelhouseEditDoc, WheelhouseTab};
use super::*;
use std::ffi::c_void;
use wheelhouse_core::dock::{self, Document, Documents, Host, HostKind, Presentation, Proposal, Registration, Rule, View};

/// The sidebar's dock: it accepts sections.
pub const WHEELHOUSE_DOCK_HOST_SIDEBAR: u32 = 0;
/// A workspace's panels: they accept content.
pub const WHEELHOUSE_DOCK_HOST_WORKSPACE_REGION: u32 = 1;
/// No host (no such panel): any other value is the same.
pub const WHEELHOUSE_DOCK_HOST_NONE: u32 = 2;

/// A View kind's traits (core: dock::traits; the native RD_ViewTrait_*).
pub const WHEELHOUSE_VIEW_TRAIT_CONTENT: u32 = 1 << 0;
pub const WHEELHOUSE_VIEW_TRAIT_SECTION: u32 = 1 << 1;
pub const WHEELHOUSE_VIEW_TRAIT_SELECTS_WORKSPACES: u32 = 1 << 2;
pub const WHEELHOUSE_VIEW_TRAIT_SINGLETON: u32 = 1 << 3;
pub const WHEELHOUSE_VIEW_TRAIT_NEEDS_WORKSPACE_SUBJECT: u32 = 1 << 4;
/// Content supplied by the root Controlled Split, not a child workspace.
pub const WHEELHOUSE_VIEW_TRAIT_CONTROL_SPLIT_SCOPE: u32 = 1 << 6;
/// Its drag floater shows it live rather than as a texture (#253).
pub const WHEELHOUSE_VIEW_TRAIT_LIVE_DRAG_PREVIEW: u32 = 1 << 7;
/// Draws a value: a Lens type of its name opens it.
pub const WHEELHOUSE_VIEW_TRAIT_VISUALIZER: u32 = 1 << 8;
/// A dragged expression dropped on it becomes its expression.
pub const WHEELHOUSE_VIEW_TRAIT_EXPRESSION_DROP: u32 = 1 << 9;
/// Draws its evaluated expression from `legacy_eval` (#313).
pub const WHEELHOUSE_VIEW_TRAIT_LEGACY_EVAL: u32 = 1 << 10;
/// Declares a producer version: shape-only preservation is safe for it.
pub const WHEELHOUSE_VIEW_TRAIT_VERSIONED_SURFACE: u32 = 1 << 11;
/// Offered by name where a View is made from an expression.
pub const WHEELHOUSE_VIEW_TRAIT_LISTED: u32 = 1 << 12;
/// A control view: it shows the Dashboard's arrangement, not a Slot.
pub const WHEELHOUSE_VIEW_TRAIT_CONTROL: u32 = 1 << 13;
/// One of the shell's own pages: presses keep focus on its query bar.
pub const WHEELHOUSE_VIEW_TRAIT_QUERY_FOCUS: u32 = 1 << 14;

/// Verdicts (core: dock::Rule), in this order.
pub const WHEELHOUSE_DOCK_VALID: u32 = 0;
pub const WHEELHOUSE_DOCK_REGISTERED_VIEW: u32 = 1;
pub const WHEELHOUSE_DOCK_HOST_ACCEPTANCE: u32 = 2;
pub const WHEELHOUSE_DOCK_WORKSPACE_SUBJECT: u32 = 3;
pub const WHEELHOUSE_DOCK_MINIMUM_WIDTH: u32 = 4;
pub const WHEELHOUSE_DOCK_SINGLETON: u32 = 5;
pub const WHEELHOUSE_DOCK_ONE_CONTROL_SURFACE: u32 = 6;
pub const WHEELHOUSE_DOCK_SELECTOR_OUTSIDE_SELECTED_REGION: u32 = 7;
pub const WHEELHOUSE_DOCK_SELECTOR_CANNOT_CLOSE: u32 = 8;
pub const WHEELHOUSE_DOCK_CONTROL_SPLIT_LEVEL: u32 = 9;

/// How a host shows a panel's Views.
pub const WHEELHOUSE_DOCK_PRESENTATION_TABS: u32 = 0;
/// A sidebar section's header holds all its panel's Views.
pub const WHEELHOUSE_DOCK_PRESENTATION_SECTION_HEADER: u32 = 1;

/// A View kind as the host's Renderers declare it.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseViewRegistration {
    pub name: Text,
    /// WHEELHOUSE_VIEW_TRAIT_* bits.
    pub traits: u32,
    /// The narrowest host it docks in (0: any).
    pub minimum_width: f32,
    /// WHEELHOUSE_DOCK_HOST_SIDEBAR or _WORKSPACE_REGION.
    pub default_host: u32,
}

/// A host as the host measures it.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseDockHost {
    /// WHEELHOUSE_DOCK_HOST_*.
    pub kind: u32,
    pub has_workspace_subject: u32,
    pub controlled_split: u64,
    pub workspace_region: u64,
    /// Its layout owner.
    pub level: u64,
    pub available_width: f32,
}

/// A placement, creation or close as the rule judges it: counts describe
/// the result.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseDockProposal {
    pub host: WheelhouseDockHost,
    pub selected_workspace_region: u64,
    /// The View's own level.
    pub view_level: u64,
    pub instances_after: u32,
    pub control_surfaces_after: u32,
    pub closing: u32,
}

/// A View as docking checks it.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseDockView {
    /// Nonzero if there is such a View.
    pub exists: u32,
    /// NULL for saved content that isn't a registered View.
    pub registration: *const WheelhouseViewRegistration,
    /// The host it is in, and the level it belongs to.
    pub source: WheelhouseDockHost,
    pub level: u64,
}

/// How many Views of the checked View's kind a Controlled Split has. NULL
/// when the host doesn't count them.
pub type WheelhouseDockInstances = Option<unsafe extern "C" fn(context: *mut c_void, controlled_split: u64) -> u32>;

unsafe fn registration(r: &WheelhouseViewRegistration) -> Result<Registration, String> {
    Ok(Registration {
        name: r.name.read()?,
        traits: r.traits,
        minimum_width: r.minimum_width,
        default_host: match r.default_host {
            WHEELHOUSE_DOCK_HOST_SIDEBAR => HostKind::Sidebar,
            WHEELHOUSE_DOCK_HOST_WORKSPACE_REGION => HostKind::WorkspaceRegion,
            other => return Err(format!("unknown default host {other}")),
        },
    })
}

fn host(h: &WheelhouseDockHost) -> Host {
    Host {
        kind: match h.kind {
            WHEELHOUSE_DOCK_HOST_SIDEBAR => Some(HostKind::Sidebar),
            WHEELHOUSE_DOCK_HOST_WORKSPACE_REGION => Some(HostKind::WorkspaceRegion),
            _ => None,
        },
        controlled_split: h.controlled_split,
        workspace_region: h.workspace_region,
        has_workspace_subject: h.has_workspace_subject != 0,
        available_width: h.available_width,
        level: h.level,
    }
}

fn rule(r: Rule) -> u32 {
    Rule::ALL.iter().position(|x| *x == r).expect("every rule is listed") as u32
}

unsafe fn optional_registration(r: *const WheelhouseViewRegistration) -> Result<Option<Registration>, String> {
    r.as_ref().map(|r| registration(r)).transpose()
}

/// The rule's verdict (WHEELHOUSE_DOCK_*) for a proposal; `view` NULL is
/// unregistered content. A malformed registration is RegisteredView with
/// an error.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_check(
    view: *const WheelhouseViewRegistration,
    proposal: *const WheelhouseDockProposal,
    error_out: *mut *mut c_char,
) -> u32 {
    pure(error_out, || {
        let p = proposal.as_ref().ok_or("null proposal")?;
        let p = Proposal {
            host: host(&p.host),
            selected_workspace_region: p.selected_workspace_region,
            instances_after: p.instances_after,
            control_surfaces_after: p.control_surfaces_after,
            closing: p.closing != 0,
            view_level: p.view_level,
        };
        Ok(rule(dock::check(optional_registration(view)?.as_ref(), &p)))
    })
    .unwrap_or(WHEELHOUSE_DOCK_REGISTERED_VIEW)
}

fn instances_fn(instances: WheelhouseDockInstances, context: *mut c_void) -> Option<impl Fn(u64) -> u32> {
    instances.map(|f| move |split: u64| unsafe { f(context, split) })
}

/// Moving `view` into a panel of `destination`. `instances` (and its
/// context) counts the destination's Views of its kind, for a singleton.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_placement(
    view: *const WheelhouseDockView,
    destination: *const WheelhouseDockHost,
    instances: WheelhouseDockInstances,
    context: *mut c_void,
    error_out: *mut *mut c_char,
) -> u32 {
    pure(error_out, || {
        let v = view.as_ref().ok_or("null view")?;
        let r = optional_registration(v.registration)?;
        let view = View { exists: v.exists != 0, registration: r.as_ref(), source: host(&v.source), level: v.level };
        let count = instances_fn(instances, context);
        Ok(rule(dock::placement(&view, &host(destination.as_ref().ok_or("null host")?), count.as_ref().map(|f| f as dock::Instances))))
    })
    .unwrap_or(WHEELHOUSE_DOCK_REGISTERED_VIEW)
}

/// Making a View of `registration` (NULL: no such kind) in `destination`.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_creation(
    registration: *const WheelhouseViewRegistration,
    destination: *const WheelhouseDockHost,
    instances: WheelhouseDockInstances,
    context: *mut c_void,
    error_out: *mut *mut c_char,
) -> u32 {
    pure(error_out, || {
        let r = optional_registration(registration)?;
        let count = instances_fn(instances, context);
        Ok(rule(dock::creation(r.as_ref(), &host(destination.as_ref().ok_or("null host")?), count.as_ref().map(|f| f as dock::Instances))))
    })
    .unwrap_or(WHEELHOUSE_DOCK_REGISTERED_VIEW)
}

/// Closing `view`.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_closure(view: *const WheelhouseDockView, error_out: *mut *mut c_char) -> u32 {
    pure(error_out, || {
        let v = view.as_ref().ok_or("null view")?;
        let r = optional_registration(v.registration)?;
        Ok(rule(dock::closure(&View { exists: v.exists != 0, registration: r.as_ref(), source: host(&v.source), level: v.level })))
    })
    .unwrap_or(WHEELHOUSE_DOCK_REGISTERED_VIEW)
}

/// The level a View of `registration` (NULL: unregistered) in `host`
/// belongs to.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_view_level(registration: *const WheelhouseViewRegistration, h: *const WheelhouseDockHost) -> u64 {
    let Some(h) = h.as_ref() else { return 0 };
    pure(ptr::null_mut(), || Ok(dock::view_level(optional_registration(registration)?.as_ref(), &host(h)))).unwrap_or(h.level)
}

/// How a host of `kind` (WHEELHOUSE_DOCK_HOST_*) shows a panel of
/// `tab_count` Views: WHEELHOUSE_DOCK_PRESENTATION_*.
#[no_mangle]
pub extern "C" fn wheelhouse_dock_presentation(kind: u32, tab_count: usize) -> u32 {
    let h = host(&WheelhouseDockHost {
        kind,
        has_workspace_subject: 0,
        controlled_split: 0,
        workspace_region: 0,
        level: 0,
        available_width: 0.0,
    });
    match dock::presentation(h.kind, tab_count) {
        Presentation::Tabs => WHEELHOUSE_DOCK_PRESENTATION_TABS,
        Presentation::SectionHeader => WHEELHOUSE_DOCK_PRESENTATION_SECTION_HEADER,
    }
}

/// The kind of host a panel owner's panels are in (WHEELHOUSE_OWNER_*):
/// WHEELHOUSE_DOCK_HOST_*.
#[no_mangle]
pub extern "C" fn wheelhouse_dock_host_kind_from_owner(owner_kind: u32) -> u32 {
    match dock::host_kind_from_owner(owner_kind == arrangement::WHEELHOUSE_OWNER_SIDEBAR) {
        HostKind::Sidebar => WHEELHOUSE_DOCK_HOST_SIDEBAR,
        HostKind::WorkspaceRegion => WHEELHOUSE_DOCK_HOST_WORKSPACE_REGION,
    }
}

/// What a refusal tells the user, static; empty for an unknown verdict.
#[no_mangle]
pub extern "C" fn wheelhouse_dock_rule_message(verdict: u32) -> Text {
    Text::borrowed(Rule::ALL.get(verdict as usize).map_or("", |r| r.message()))
}

/// A document restore checks and may edit.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseDockDocument {
    pub doc: WheelhouseEditDoc,
    /// The host its panels are in.
    pub host: WheelhouseDockHost,
    /// Each tab's registration, an index into the registrations, or
    /// WHEELHOUSE_NONE for content that isn't a registered View: one per
    /// tab, panel by panel in document order.
    pub tab_registrations: *const usize,
}

/// A window's documents after restore.
#[repr(C)]
pub struct WheelhouseDockRestore {
    /// Each document, edited or as it was, in the order given.
    pub docs: *const WheelhouseEditDoc,
    /// Nonzero for each document restore edited.
    pub dirty: *const u32,
    pub doc_count: usize,
    /// The sidebar sections it moved, which the sidebar places by their
    /// hints once committed.
    pub moved: *const WheelhouseTab,
    pub moved_count: usize,
}

struct RestoreOwned {
    _docs: Vec<EditDoc>,
    _views: Vec<WheelhouseEditDoc>,
    _dirty: Vec<u32>,
    _moved: Vec<WheelhouseTab>,
}

/// Repairs a window's documents: one copy of each singleton, and each View
/// its host refuses moved to the end of its default host's first leaf
/// (`sidebar` or `workspace`, document indices or WHEELHOUSE_NONE),
/// keeping its address. Free the result with wheelhouse_dock_restore_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_restore(
    documents: *const WheelhouseDockDocument,
    count: usize,
    sidebar: usize,
    workspace: usize,
    registrations: *const WheelhouseViewRegistration,
    registration_count: usize,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseDockRestore {
    pure(error_out, || {
        let registrations = slice(registrations, registration_count)?.iter().map(|r| registration(r)).collect::<Result<Vec<_>, _>>()?;
        let given = slice(documents, count)?;
        let mut kinds: Vec<(WheelhouseTab, usize)> = vec![];
        let mut docs = Documents {
            documents: vec![],
            sidebar: (sidebar != NONE).then_some(sidebar),
            workspace: (workspace != NONE).then_some(workspace),
        };
        if docs.sidebar.is_some_and(|i| i >= count) || docs.workspace.is_some_and(|i| i >= count) {
            return Err("a default host is not one of the documents".into());
        }
        for d in given {
            let arrangement = read_edit_doc(&d.doc)?;
            let tabs: Vec<WheelhouseTab> = arrangement.panels().flat_map(|p| p.tabs.iter().copied()).collect();
            let indices = slice(d.tab_registrations, tabs.len())?;
            for (tab, &index) in tabs.iter().zip(indices) {
                if index != NONE && index >= registrations.len() {
                    return Err(format!("registration {index} is not one of {}", registrations.len()));
                }
                if index != NONE && !kinds.iter().any(|(t, _)| t == tab) {
                    kinds.push((*tab, index));
                }
            }
            docs.documents.push(Document { arrangement, host: host(&d.host), dirty: false });
        }
        let moved = dock::restore(&mut docs, &registrations, &|tab| kinds.iter().find(|(t, _)| t == tab).map(|(_, i)| *i));
        let dirty: Vec<u32> = docs.documents.iter().map(|d| d.dirty as u32).collect();
        let edit_docs: Vec<EditDoc> = docs.documents.into_iter().map(|d| EditDoc::of(d.arrangement)).collect();
        let views: Vec<WheelhouseEditDoc> = edit_docs.iter().map(EditDoc::doc).collect();
        let value = WheelhouseDockRestore {
            docs: views.as_ptr(),
            dirty: dirty.as_ptr(),
            doc_count: views.len(),
            moved: moved.as_ptr(),
            moved_count: moved.len(),
        };
        Ok(give(value, RestoreOwned { _docs: edit_docs, _views: views, _dirty: dirty, _moved: moved }))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_dock_restore_free(restore: *mut WheelhouseDockRestore) {
    take::<WheelhouseDockRestore, RestoreOwned>(restore);
}

#[cfg(test)]
mod tests {
    use super::*;

    // The header's bits and verdicts are the core's.
    #[test]
    fn trait_bits_and_verdicts_are_the_cores() {
        use dock::traits::*;
        let pairs = [
            (WHEELHOUSE_VIEW_TRAIT_CONTENT, CONTENT),
            (WHEELHOUSE_VIEW_TRAIT_SECTION, SECTION),
            (WHEELHOUSE_VIEW_TRAIT_SELECTS_WORKSPACES, SELECTS_WORKSPACES),
            (WHEELHOUSE_VIEW_TRAIT_SINGLETON, SINGLETON),
            (WHEELHOUSE_VIEW_TRAIT_NEEDS_WORKSPACE_SUBJECT, NEEDS_WORKSPACE_SUBJECT),
            (WHEELHOUSE_VIEW_TRAIT_CONTROL_SPLIT_SCOPE, CONTROL_SPLIT_SCOPE),
            (WHEELHOUSE_VIEW_TRAIT_LIVE_DRAG_PREVIEW, LIVE_DRAG_PREVIEW),
            (WHEELHOUSE_VIEW_TRAIT_VISUALIZER, VISUALIZER),
            (WHEELHOUSE_VIEW_TRAIT_EXPRESSION_DROP, EXPRESSION_DROP),
            (WHEELHOUSE_VIEW_TRAIT_LEGACY_EVAL, LEGACY_EVAL),
            (WHEELHOUSE_VIEW_TRAIT_VERSIONED_SURFACE, VERSIONED_SURFACE),
            (WHEELHOUSE_VIEW_TRAIT_LISTED, LISTED),
            (WHEELHOUSE_VIEW_TRAIT_CONTROL, CONTROL),
            (WHEELHOUSE_VIEW_TRAIT_QUERY_FOCUS, QUERY_FOCUS),
        ];
        assert!(pairs.iter().all(|(abi, core)| abi == core));
        let verdicts = [
            WHEELHOUSE_DOCK_VALID,
            WHEELHOUSE_DOCK_REGISTERED_VIEW,
            WHEELHOUSE_DOCK_HOST_ACCEPTANCE,
            WHEELHOUSE_DOCK_WORKSPACE_SUBJECT,
            WHEELHOUSE_DOCK_MINIMUM_WIDTH,
            WHEELHOUSE_DOCK_SINGLETON,
            WHEELHOUSE_DOCK_ONE_CONTROL_SURFACE,
            WHEELHOUSE_DOCK_SELECTOR_OUTSIDE_SELECTED_REGION,
            WHEELHOUSE_DOCK_SELECTOR_CANNOT_CLOSE,
            WHEELHOUSE_DOCK_CONTROL_SPLIT_LEVEL,
        ];
        assert!(Rule::ALL.iter().zip(verdicts).all(|(r, v)| rule(*r) == v));
    }
}
