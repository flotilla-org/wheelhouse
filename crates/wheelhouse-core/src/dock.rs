//! Docking validity (docs/design/docking-validity.md): which View may go
//! where, and the repair of saved arrangements that break it. One rule
//! serves drag feedback, commands and restore; the frontend works out the
//! inputs it needs pixels or configuration for (a host's available width,
//! its Controlled Split and level, instance counts) and the rule judges
//! them. Host identities (the native shell's config IDs) are opaque.
//!
//! These are the native shell's C rules (`native/src/shell/shell_docking.c`)
//! with the same results.
use crate::arrangement::{apply, Arrangement, Edit, PanelId};

#[cfg(test)]
mod tests;

/// The kind of host a panel is in.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum HostKind {
    /// The sidebar's dock: it accepts sections.
    Sidebar,
    /// A workspace's panels (a window's own too): they accept content.
    WorkspaceRegion,
}

impl HostKind {
    pub const ALL: [HostKind; 2] = [HostKind::Sidebar, HostKind::WorkspaceRegion];
}

/// How a host shows a panel's Views.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Presentation {
    Tabs,
    /// A sidebar section's header holds all its panel's Views
    /// (sidebar-headers.md).
    SectionHeader,
}

/// A View kind's traits, the native `RD_ViewTrait_*` bits.
pub mod traits {
    pub const CONTENT: u32 = 1 << 0;
    pub const SECTION: u32 = 1 << 1;
    pub const SELECTS_WORKSPACES: u32 = 1 << 2;
    pub const SINGLETON: u32 = 1 << 3;
    pub const NEEDS_WORKSPACE_SUBJECT: u32 = 1 << 4;
    /// Content supplied by the root Controlled Split, not a child workspace.
    pub const CONTROL_SPLIT_SCOPE: u32 = 1 << 6;
    /// Its drag floater shows it live rather than as a texture (#253).
    pub const LIVE_DRAG_PREVIEW: u32 = 1 << 7;
    /// Draws a value: a Lens type of its name opens it.
    pub const VISUALIZER: u32 = 1 << 8;
    /// A dragged expression dropped on it becomes its expression.
    pub const EXPRESSION_DROP: u32 = 1 << 9;
    /// Draws its evaluated expression from its context's `legacy_eval` (#313).
    pub const LEGACY_EVAL: u32 = 1 << 10;
    /// Declares a producer version, so shape-only workspace preservation is
    /// safe for it.
    pub const VERSIONED_SURFACE: u32 = 1 << 11;
    /// Offered by name where a View is made from an expression.
    pub const LISTED: u32 = 1 << 12;
    /// A control view: it shows the Dashboard's arrangement, not a Slot.
    pub const CONTROL: u32 = 1 << 13;
    /// One of the shell's own pages: presses keep focus on its query bar.
    pub const QUERY_FOCUS: u32 = 1 << 14;
}

/// The View kind restore moves by its section hints once committed.
pub const SIDEBAR_SECTION: &str = "sidebar_section";

/// A View kind as the frontend's Renderers declare it.
#[derive(Clone, Debug, PartialEq)]
pub struct Registration {
    pub name: String,
    pub traits: u32,
    pub minimum_width: f32,
    pub default_host: HostKind,
}

impl Registration {
    fn has(&self, trait_bits: u32) -> bool {
        self.traits & trait_bits != 0
    }
}

/// The registration named `name`.
pub fn registration<'a>(registrations: &'a [Registration], name: &str) -> Option<&'a Registration> {
    registrations.iter().find(|r| r.name == name)
}

/// A host as the frontend measures it. Identities are opaque; 0 is none.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Host {
    /// None for no host (no such panel).
    pub kind: Option<HostKind>,
    pub controlled_split: u64,
    pub workspace_region: u64,
    pub has_workspace_subject: bool,
    pub available_width: f32,
    /// Its layout owner: independent of its kind or where it is on screen.
    pub level: u64,
}

/// A placement, creation or close as the rule judges it. Counts describe
/// the result, not the layout before: a move keeps its instance, a creation
/// adds one, a close removes one.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Proposal {
    pub host: Host,
    pub selected_workspace_region: u64,
    pub instances_after: u32,
    pub control_surfaces_after: u32,
    pub closing: bool,
    /// The View's own level, rather than its destination's.
    pub view_level: u64,
}

/// The rule's verdict: valid, or which rule refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Rule {
    Valid,
    RegisteredView,
    HostAcceptance,
    WorkspaceSubject,
    MinimumWidth,
    Singleton,
    OneControlSurface,
    SelectorOutsideSelectedRegion,
    SelectorCannotClose,
    ControlSplitLevel,
}

impl Rule {
    pub const ALL: [Rule; 10] = [
        Rule::Valid,
        Rule::RegisteredView,
        Rule::HostAcceptance,
        Rule::WorkspaceSubject,
        Rule::MinimumWidth,
        Rule::Singleton,
        Rule::OneControlSurface,
        Rule::SelectorOutsideSelectedRegion,
        Rule::SelectorCannotClose,
        Rule::ControlSplitLevel,
    ];

    /// What a refused command tells the user.
    pub fn message(self) -> &'static str {
        match self {
            Rule::Valid => "valid placement",
            Rule::RegisteredView => "the View type is not registered",
            Rule::HostAcceptance => "the host does not accept this View",
            Rule::WorkspaceSubject => "a Workspace Subject is required",
            Rule::MinimumWidth => "the target is narrower than the View's minimum width",
            Rule::Singleton => "only one instance of this View is allowed",
            Rule::OneControlSurface => "a Controlled Split must retain exactly one Control Surface",
            Rule::SelectorOutsideSelectedRegion => "a selector cannot occupy its selected Workspace Region",
            Rule::SelectorCannotClose => "the workspace selector cannot be closed",
            Rule::ControlSplitLevel => "the View belongs to a different Controlled Split level",
        }
    }
}

/// A View as docking checks it: what it is, and where it is now.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct View<'a> {
    /// Whether there is such a View.
    pub exists: bool,
    /// None for saved content that isn't a registered View.
    pub registration: Option<&'a Registration>,
    /// The host it is in, and the level it belongs to.
    pub source: Host,
    pub level: u64,
}

/// How many Views of the View's kind a Controlled Split has; the frontend
/// counts its window's tabs.
pub type Instances<'a> = &'a dyn Fn(u64) -> u32;

/// What a host of `kind` accepts: any of these traits.
fn accepts(kind: HostKind) -> u32 {
    match kind {
        HostKind::Sidebar => traits::SECTION,
        HostKind::WorkspaceRegion => traits::CONTENT,
    }
}

/// The rule, for drag feedback, commands and restore.
pub fn check(view: Option<&Registration>, p: &Proposal) -> Rule {
    // Unknown saved content can be removed, but has no placement traits.
    let Some(view) = view else { return if p.closing { Rule::Valid } else { Rule::RegisteredView } };
    if p.closing && view.has(traits::SELECTS_WORKSPACES) {
        return Rule::SelectorCannotClose;
    }
    if p.host.controlled_split != 0 && p.control_surfaces_after != 1 {
        return Rule::OneControlSurface;
    }
    if p.closing {
        return Rule::Valid;
    }
    if view.has(traits::SELECTS_WORKSPACES) && p.selected_workspace_region != 0 && p.selected_workspace_region == p.host.workspace_region {
        return Rule::SelectorOutsideSelectedRegion;
    }
    let Some(kind) = p.host.kind else { return Rule::HostAcceptance };
    if p.view_level != p.host.level {
        return Rule::ControlSplitLevel;
    }
    if !view.has(accepts(kind)) {
        return Rule::HostAcceptance;
    }
    if view.has(traits::NEEDS_WORKSPACE_SUBJECT) && !p.host.has_workspace_subject {
        return Rule::WorkspaceSubject;
    }
    if p.host.available_width < view.minimum_width {
        return Rule::MinimumWidth;
    }
    if view.has(traits::SINGLETON) && p.instances_after != 1 {
        return Rule::Singleton;
    }
    Rule::Valid
}

/// The kind of host a panel owner's panels are in: the sidebar's for the
/// sidebar, a workspace region for everything else.
pub fn host_kind_from_owner(sidebar: bool) -> HostKind {
    if sidebar {
        HostKind::Sidebar
    } else {
        HostKind::WorkspaceRegion
    }
}

/// How `host` shows a panel's tabs.
pub fn presentation(host: Option<HostKind>, _tab_count: usize) -> Presentation {
    match host {
        Some(HostKind::Sidebar) => Presentation::SectionHeader,
        _ => Presentation::Tabs,
    }
}

/// The level a View in `host` belongs to: its host's, or for a View of the
/// root Controlled Split's own scope, that split's.
pub fn view_level(registration: Option<&Registration>, host: &Host) -> u64 {
    match registration {
        Some(r) if r.has(traits::CONTROL_SPLIT_SCOPE) => host.controlled_split,
        _ => host.level,
    }
}

/// Moving `view` into a panel of `destination`; a host of no kind is no
/// destination. `instances` counts a Controlled Split's Views of its kind,
/// when the frontend counts them.
pub fn placement(view: &View, destination: &Host, instances: Option<Instances>) -> Rule {
    if !view.exists || destination.kind.is_none() {
        return Rule::RegisteredView;
    }
    let source = view.source;
    let mut p = Proposal {
        host: *destination,
        selected_workspace_region: source.controlled_split,
        view_level: view.level,
        instances_after: 1,
        // TODO(#164): derive counts from nested Controlled Splits. The
        // current root Control Surface is implicit.
        control_surfaces_after: 1,
        closing: false,
    };
    let registration = view.registration;
    if registration.is_some_and(|r| r.has(traits::SINGLETON)) {
        p.instances_after = instances.map_or(1, |count| count(destination.controlled_split));
        if source.controlled_split != destination.controlled_split {
            p.instances_after = p.instances_after.wrapping_add(1);
        }
    }
    // Moving a selector to another Controlled Split would remove the
    // source's only Control Surface and add a second to the destination.
    if registration.is_some_and(|r| r.has(traits::SELECTS_WORKSPACES)) && source.controlled_split != destination.controlled_split {
        p.control_surfaces_after = 2;
    }
    check(registration, &p)
}

/// Making a View of `registration` in `destination`.
pub fn creation(registration: Option<&Registration>, destination: &Host, instances: Option<Instances>) -> Rule {
    let Some(view) = registration else { return Rule::RegisteredView };
    let p = Proposal {
        host: *destination,
        view_level: if view.has(traits::CONTROL_SPLIT_SCOPE) { destination.controlled_split } else { destination.level },
        selected_workspace_region: destination.controlled_split,
        instances_after: match instances {
            Some(count) if view.has(traits::SINGLETON) => count(destination.controlled_split).wrapping_add(1),
            _ => 1,
        },
        // TODO(#164): derive this from nested splits. A creation must not
        // add another selector to the implicit root surface.
        control_surfaces_after: if view.has(traits::SELECTS_WORKSPACES) { 2 } else { 1 },
        closing: false,
    };
    if destination.kind.is_some() {
        check(Some(view), &p)
    } else {
        Rule::HostAcceptance
    }
}

/// Closing `view`.
pub fn closure(view: &View) -> Rule {
    if !view.exists {
        return Rule::RegisteredView;
    }
    // TODO(#164): derive this with placement and creation from nested splits.
    let p = Proposal { host: view.source, closing: true, control_surfaces_after: 1, ..Proposal::default() };
    check(view.registration, &p)
}

/// An arrangement restore checks and may edit, with the host its panels
/// are in.
#[derive(Clone, Debug, PartialEq)]
pub struct Document<T> {
    pub arrangement: Arrangement<T>,
    pub host: Host,
    /// Set when restore edited it.
    pub dirty: bool,
}

/// A window's arrangements, and where refused Views go by default host.
#[derive(Clone, Debug, PartialEq)]
pub struct Documents<T> {
    pub documents: Vec<Document<T>>,
    /// The sidebar's dock.
    pub sidebar: Option<usize>,
    /// The window's first workspace, else its own panels.
    pub workspace: Option<usize>,
}

/// The leaf a View restore moves goes to: down the first children of the
/// root, which is made when there is none.
pub fn restore_leaf<T: Clone + PartialEq>(arrangement: &mut Arrangement<T>) -> PanelId {
    if arrangement.root.is_none() {
        apply(arrangement, &Edit::Clear, &|_| false);
    }
    arrangement.root.as_ref().expect("cleared").first_leaf().id
}

/// A View as restore found it.
struct Found<T> {
    tab: T,
    registration: Option<usize>,
    document: usize,
    panel: PanelId,
    selected: bool,
    gone: bool,
}

/// Whether the rule accepts `view` where it is, assessed with one instance
/// so that duplicates don't hide validity.
fn valid_where_it_is(registration: Option<&Registration>, host: &Host) -> bool {
    let p = Proposal {
        host: *host,
        selected_workspace_region: host.controlled_split,
        instances_after: 1,
        control_surfaces_after: 1,
        closing: false,
        view_level: view_level(registration, host),
    };
    check(registration, &p) == Rule::Valid
}

/// Repairs a window's arrangements: one copy of each singleton, and each
/// View its host refuses moved to the end of its default host's first
/// leaf, keeping its address. Marks the documents it edits dirty, and
/// returns the sidebar sections it moved, which the sidebar places by their
/// hints once committed; the panel such a section leaves is marked
/// `restore_emptied`, to go with it then. `registration` names a tab's kind
/// among `registrations`; content that isn't a registered View stays.
/// `documents.sidebar` and `documents.workspace` must index
/// `documents.documents`, or it panics.
// Views are indexed: each step reads one and writes the documents.
#[allow(clippy::needless_range_loop)]
pub fn restore<T: Clone + PartialEq>(
    documents: &mut Documents<T>,
    registrations: &[Registration],
    registration: &dyn Fn(&T) -> Option<usize>,
) -> Vec<T> {
    let reg = |index: Option<usize>| index.and_then(|i| registrations.get(i));
    let mut found: Vec<Found<T>> = vec![];
    for (d, document) in documents.documents.iter().enumerate() {
        for panel in document.arrangement.panels() {
            for tab in &panel.tabs {
                found.push(Found {
                    tab: tab.clone(),
                    registration: registration(tab).filter(|&i| i < registrations.len()),
                    document: d,
                    panel: panel.id,
                    selected: panel.selected.as_ref() == Some(tab),
                    gone: false,
                });
            }
        }
    }
    let valid = |documents: &Documents<T>, v: &Found<T>| valid_where_it_is(reg(v.registration), &documents.documents[v.document].host);

    // One copy of each singleton: the first in tree order, unless a later
    // one is where it is valid and the first is not.
    let mut keepers: Vec<Option<usize>> = vec![None; registrations.len()];
    for (i, v) in found.iter().enumerate() {
        let Some(r) = v.registration.filter(|&r| registrations[r].has(traits::SINGLETON)) else { continue };
        match keepers[r] {
            Some(k) if valid(documents, &found[k]) || !valid(documents, v) => {}
            _ => keepers[r] = Some(i),
        }
    }
    for i in 0..found.len() {
        let Some(r) = found[i].registration.filter(|&r| registrations[r].has(traits::SINGLETON)) else { continue };
        if keepers[r] == Some(i) {
            continue;
        }
        let document = &mut documents.documents[found[i].document];
        apply(&mut document.arrangement, &Edit::RemoveTab { tab: found[i].tab.clone() }, &|_| false);
        document.dirty = true;
        found[i].gone = true;
    }

    // A View refused where it is moves to the end of its default host's
    // first leaf, keeping its address.
    let mut pending = vec![];
    for i in 0..found.len() {
        let v = &found[i];
        let Some(r) = reg(v.registration) else { continue };
        if v.gone || valid(documents, v) {
            continue;
        }
        let destination = match r.default_host {
            HostKind::Sidebar => documents.sidebar,
            HostKind::WorkspaceRegion => documents.workspace,
        };
        let Some(destination) = destination else { continue };
        let (tab, source, panel, selected) = (v.tab.clone(), v.document, v.panel, v.selected);
        if r.name == SIDEBAR_SECTION {
            pending.push(tab.clone());
            let document = &mut documents.documents[source];
            if let Some(left) = document.arrangement.panel_mut(panel).filter(|p| !p.restore_emptied) {
                left.restore_emptied = true;
                document.dirty = true;
            }
        }
        let leaf = restore_leaf(&mut documents.documents[destination].arrangement);
        // A default can still lack context (a required subject, say).
        // Moving a View already there would only reorder it.
        if source == destination && panel == leaf {
            continue;
        }
        if source != destination {
            let document = &mut documents.documents[source];
            apply(&mut document.arrangement, &Edit::DetachTab { tab: tab.clone() }, &|_| false);
            document.dirty = true;
        }
        let document = &mut documents.documents[destination];
        let after = document.arrangement.panel(leaf).and_then(|p| p.tabs.last().cloned());
        // The destination keeps its Selected View unless this one was
        // selected.
        apply(&mut document.arrangement, &Edit::InsertTab { tab, to: leaf, after, select: selected }, &|_| false);
        document.dirty = true;
        found[i].document = destination;
        found[i].panel = leaf;
    }
    pending
}
