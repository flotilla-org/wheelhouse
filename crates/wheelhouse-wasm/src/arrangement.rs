//! An arrangement as a page edits it (#516): the core's edit document,
//! edits, intents and docking rules as plain objects, with generated
//! TypeScript types. Tabs are strings; panel IDs numbers. No page draws an
//! arrangement yet: this is the shape it will use.
use serde::{Deserialize, Serialize};
use tsify::Tsify;
use wheelhouse_core::arrangement::{self as core, Arrangement, Dir, PanelId};
use wheelhouse_core::dock;
use wheelhouse_core::suggested_layout::Axis;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "kebab-case")]
pub enum EditAxis {
    Row,
    Column,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "kebab-case")]
pub enum EditDir {
    Left,
    Up,
    Right,
    Down,
}

/// A panel: a split's children, or a leaf's tabs.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct EditPanel {
    pub id: u64,
    pub weight: f32,
    pub children: Vec<EditPanel>,
    pub tabs: Vec<String>,
    pub selected: Option<String>,
    /// The ID a provider's document gave it: it stays when emptied.
    pub named: Option<String>,
    /// The panel whose Presentation State it takes when next set.
    pub options_from: Option<u64>,
    pub restore_emptied: bool,
}

/// An arrangement as a frontend keeps its live copy.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct EditDoc {
    pub root: Option<EditPanel>,
    pub root_axis: EditAxis,
    pub next_id: u64,
    /// Tabs removed since the page last set it, oldest first.
    pub removed: Vec<String>,
}

/// One edit, by rule (core: arrangement::Edit).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(tag = "edit", rename_all = "kebab-case", rename_all_fields = "camelCase")]
pub enum ArrangementEdit {
    Clear,
    Discard,
    Split { panel: u64, dir: EditDir },
    Add { parent: Option<u64>, weight: f32 },
    Remove { panel: u64 },
    Close { panel: u64 },
    InsertTab { tab: String, to: u64, after: Option<String>, select: bool },
    DetachTab { tab: String },
    RemoveTab { tab: String },
    Drop { tab: Option<String>, to: u64, dir: Option<EditDir>, after: Option<String> },
    Settle { panel: u64, dir: Option<EditDir>, may_close: bool },
    Select { panel: u64, tab: Option<String> },
    Reorder { panel: u64, after: Option<u64> },
    Resize { panel: u64, delta: f32, floor: f32 },
    Equalize { panel: u64 },
    Readdress { from: String, to: String },
}

/// Whose arrangement: a Workspace's, or a window's sidebar, by ID.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "kind", rename_all = "kebab-case")]
pub enum ArrangementOwner {
    Workspace { id: String },
    Sidebar { id: String },
}

/// An arrangement intent, by its stable name (core: arrangement::Intent).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(tag = "intent", rename_all = "kebab-case", rename_all_fields = "camelCase")]
pub enum ArrangementIntent {
    SelectTab { owner: ArrangementOwner, panel: u64, tab: Option<String> },
    MoveTab { owner: ArrangementOwner, tab: String, to: u64, after: Option<String> },
    CloseTab { owner: ArrangementOwner, tab: String },
    DropTab { owner: ArrangementOwner, tab: String, to: u64, dir: Option<EditDir>, after: Option<String> },
    SplitPanel { owner: ArrangementOwner, panel: u64, dir: EditDir },
    ClosePanel { owner: ArrangementOwner, panel: u64 },
    ReorderPanel { owner: ArrangementOwner, panel: u64, after: Option<u64> },
    ResizePanel { owner: ArrangementOwner, panel: u64, delta: f32, floor: f32 },
    EqualizePanel { owner: ArrangementOwner, panel: u64 },
}

/// What an edit did.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct EditOutcome {
    pub applied: bool,
    pub panel: Option<u64>,
    pub source: Option<u64>,
    pub heir: Option<u64>,
    pub removed: Vec<String>,
}

/// The edited document and what the edit did.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
pub struct EditResult {
    pub doc: EditDoc,
    pub outcome: EditOutcome,
}

/// Docking's verdict (core: dock::Rule), with what to tell the user.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "kebab-case")]
pub enum DockRule {
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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "kebab-case")]
pub enum DockHostKind {
    Sidebar,
    WorkspaceRegion,
}

/// A View kind as a page's renderers declare it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct ViewRegistration {
    pub name: String,
    /// The core's trait bits (dock::traits).
    pub traits: u32,
    pub minimum_width: f32,
    pub default_host: DockHostKind,
}

/// A host as a page measures it; IDs are opaque, 0 none.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct DockHost {
    pub kind: Option<DockHostKind>,
    pub controlled_split: u64,
    pub workspace_region: u64,
    pub has_workspace_subject: bool,
    pub available_width: f32,
    pub level: u64,
}

/// A placement, creation or close as the rule judges it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct DockProposal {
    pub host: DockHost,
    pub selected_workspace_region: u64,
    pub instances_after: u32,
    pub control_surfaces_after: u32,
    pub closing: bool,
    pub view_level: u64,
}

fn axis(a: EditAxis) -> Axis {
    match a {
        EditAxis::Row => Axis::Row,
        EditAxis::Column => Axis::Column,
    }
}

fn dir(d: EditDir) -> Dir {
    match d {
        EditDir::Left => Dir::Left,
        EditDir::Up => Dir::Up,
        EditDir::Right => Dir::Right,
        EditDir::Down => Dir::Down,
    }
}

fn panel_in(p: &EditPanel) -> core::Panel<String> {
    core::Panel {
        id: PanelId(p.id),
        weight: p.weight,
        children: p.children.iter().map(panel_in).collect(),
        tabs: p.tabs.clone(),
        selected: p.selected.clone(),
        named: p.named.clone(),
        options_from: p.options_from.map(PanelId),
        restore_emptied: p.restore_emptied,
    }
}

fn panel_out(p: &core::Panel<String>) -> EditPanel {
    EditPanel {
        id: p.id.0,
        weight: p.weight,
        children: p.children.iter().map(panel_out).collect(),
        tabs: p.tabs.clone(),
        selected: p.selected.clone(),
        named: p.named.clone(),
        options_from: p.options_from.map(|p| p.0),
        restore_emptied: p.restore_emptied,
    }
}

impl From<&EditDoc> for Arrangement<String> {
    fn from(doc: &EditDoc) -> Self {
        Arrangement {
            root: doc.root.as_ref().map(panel_in),
            root_axis: axis(doc.root_axis),
            next_id: doc.next_id,
            removed: doc.removed.clone(),
        }
    }
}

impl From<&Arrangement<String>> for EditDoc {
    fn from(a: &Arrangement<String>) -> Self {
        EditDoc {
            root: a.root.as_ref().map(panel_out),
            root_axis: if a.root_axis == Axis::Row { EditAxis::Row } else { EditAxis::Column },
            next_id: a.next_id,
            removed: a.removed.clone(),
        }
    }
}

impl From<&ArrangementEdit> for core::Edit<String> {
    fn from(e: &ArrangementEdit) -> Self {
        use core::Edit;
        let p = PanelId;
        match e.clone() {
            ArrangementEdit::Clear => Edit::Clear,
            ArrangementEdit::Discard => Edit::Discard,
            ArrangementEdit::Split { panel, dir: d } => Edit::Split { panel: p(panel), dir: dir(d) },
            ArrangementEdit::Add { parent, weight } => Edit::Add { parent: parent.map(p), weight },
            ArrangementEdit::Remove { panel } => Edit::Remove { panel: p(panel) },
            ArrangementEdit::Close { panel } => Edit::Close { panel: p(panel) },
            ArrangementEdit::InsertTab { tab, to, after, select } => Edit::InsertTab { tab, to: p(to), after, select },
            ArrangementEdit::DetachTab { tab } => Edit::DetachTab { tab },
            ArrangementEdit::RemoveTab { tab } => Edit::RemoveTab { tab },
            ArrangementEdit::Drop { tab, to, dir: d, after } => Edit::Drop { tab, to: p(to), dir: d.map(dir), after },
            ArrangementEdit::Settle { panel, dir: d, may_close } => Edit::Settle { panel: p(panel), dir: d.map(dir), may_close },
            ArrangementEdit::Select { panel, tab } => Edit::Select { panel: p(panel), tab },
            ArrangementEdit::Reorder { panel, after } => Edit::Reorder { panel: p(panel), after: after.map(p) },
            ArrangementEdit::Resize { panel, delta, floor } => Edit::Resize { panel: p(panel), delta, floor },
            ArrangementEdit::Equalize { panel } => Edit::Equalize { panel: p(panel) },
            ArrangementEdit::Readdress { from, to } => Edit::Readdress { from, to },
        }
    }
}

impl ArrangementIntent {
    /// The core's intent; an owner's ID must be a Workspace (or Window) ID.
    pub fn read(&self) -> Result<core::Intent<String>, String> {
        use core::{Intent, Owner, OwnerKind};
        let owner = |o: &ArrangementOwner| -> Result<Owner, String> {
            let (kind, id) = match o {
                ArrangementOwner::Workspace { id } => (OwnerKind::Workspace, id),
                ArrangementOwner::Sidebar { id } => (OwnerKind::Sidebar, id),
            };
            Ok(Owner { kind, id: id.parse().map_err(|e| format!("owner {id:?}: {e}"))? })
        };
        let p = PanelId;
        Ok(match self.clone() {
            Self::SelectTab { owner: o, panel, tab } => Intent::SelectTab { owner: owner(&o)?, panel: p(panel), tab },
            Self::MoveTab { owner: o, tab, to, after } => Intent::MoveTab { owner: owner(&o)?, tab, to: p(to), after },
            Self::CloseTab { owner: o, tab } => Intent::CloseTab { owner: owner(&o)?, tab },
            Self::DropTab { owner: o, tab, to, dir: d, after } => {
                Intent::DropTab { owner: owner(&o)?, tab, to: p(to), dir: d.map(dir), after }
            }
            Self::SplitPanel { owner: o, panel, dir: d } => Intent::SplitPanel { owner: owner(&o)?, panel: p(panel), dir: dir(d) },
            Self::ClosePanel { owner: o, panel } => Intent::ClosePanel { owner: owner(&o)?, panel: p(panel) },
            Self::ReorderPanel { owner: o, panel, after } => {
                Intent::ReorderPanel { owner: owner(&o)?, panel: p(panel), after: after.map(p) }
            }
            Self::ResizePanel { owner: o, panel, delta, floor } => Intent::ResizePanel { owner: owner(&o)?, panel: p(panel), delta, floor },
            Self::EqualizePanel { owner: o, panel } => Intent::EqualizePanel { owner: owner(&o)?, panel: p(panel) },
        })
    }
}

impl From<dock::Rule> for DockRule {
    fn from(rule: dock::Rule) -> Self {
        use dock::Rule as R;
        match rule {
            R::Valid => Self::Valid,
            R::RegisteredView => Self::RegisteredView,
            R::HostAcceptance => Self::HostAcceptance,
            R::WorkspaceSubject => Self::WorkspaceSubject,
            R::MinimumWidth => Self::MinimumWidth,
            R::Singleton => Self::Singleton,
            R::OneControlSurface => Self::OneControlSurface,
            R::SelectorOutsideSelectedRegion => Self::SelectorOutsideSelectedRegion,
            R::SelectorCannotClose => Self::SelectorCannotClose,
            R::ControlSplitLevel => Self::ControlSplitLevel,
        }
    }
}

fn host_kind(k: DockHostKind) -> dock::HostKind {
    match k {
        DockHostKind::Sidebar => dock::HostKind::Sidebar,
        DockHostKind::WorkspaceRegion => dock::HostKind::WorkspaceRegion,
    }
}

impl From<&ViewRegistration> for dock::Registration {
    fn from(r: &ViewRegistration) -> Self {
        dock::Registration {
            name: r.name.clone(),
            traits: r.traits,
            minimum_width: r.minimum_width,
            default_host: host_kind(r.default_host),
        }
    }
}

impl From<&DockHost> for dock::Host {
    fn from(h: &DockHost) -> Self {
        dock::Host {
            kind: h.kind.map(host_kind),
            controlled_split: h.controlled_split,
            workspace_region: h.workspace_region,
            has_workspace_subject: h.has_workspace_subject,
            available_width: h.available_width,
            level: h.level,
        }
    }
}

impl From<&DockProposal> for dock::Proposal {
    fn from(p: &DockProposal) -> Self {
        dock::Proposal {
            host: (&p.host).into(),
            selected_workspace_region: p.selected_workspace_region,
            instances_after: p.instances_after,
            control_surfaces_after: p.control_surfaces_after,
            closing: p.closing,
            view_level: p.view_level,
        }
    }
}

/// Applies `edit` to `doc`, `hidden` the tabs a settle passes over.
pub fn apply_edit(doc: &EditDoc, edit: &ArrangementEdit, hidden: &[String]) -> EditResult {
    let mut a = Arrangement::from(doc);
    let out = core::apply(&mut a, &edit.into(), &|tab| hidden.contains(tab));
    EditResult {
        doc: EditDoc::from(&a),
        outcome: EditOutcome {
            applied: out.applied,
            panel: out.panel.map(|p| p.0),
            source: out.source.map(|p| p.0),
            heir: out.heir.map(|p| p.0),
            removed: out.removed,
        },
    }
}
