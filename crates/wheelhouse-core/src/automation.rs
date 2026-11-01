//! The Semantic Tree served to automation clients (#554; CONTEXT.md,
//! Semantic Tree): each workspace, its panel tree from its arrangement and
//! the current Views of its Slots, with no geometry. Nodes carry Automation
//! IDs that mirror View Address (docs/protocol/automation-endpoint.md).
//! Frontends add what only they know, such as a terminal's size, to the
//! nodes by ID.
use serde::{Deserialize, Serialize};

use crate::{
    slots::{ArrangementDoc, Panel, PanelNode, SlotInfo},
    suggested_layout::{Axis, CommandLine, Content, LocalRecipe, ViewSpec},
    EntityRef, WorkspaceId,
};

/// One Semantic Tree node: `{id, kind, name, state, durable, props,
/// children}`, empty fields left out.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Node {
    pub id: String,
    pub kind: Kind,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub state: Vec<State>,
    pub durable: bool,
    #[serde(default, skip_serializing_if = "Props::is_empty")]
    pub props: Props,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub children: Vec<Node>,
}

/// The kinds the Semantic Tree has; the Placement Tree (#564) adds its own.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Kind {
    Workspace,
    Panel,
    View,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum State {
    /// A workspace a window shows, or the View its panel shows.
    Selected,
    /// A panel shown without space.
    Collapsed,
}

/// How a split lays out its children: a row left to right, a column top to
/// bottom.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Split {
    Row,
    Column,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Props {
    /// A workspace's Workspace Subject.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subject: Option<EntityRef>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub split: Option<Split>,
    /// A split's children's, in order.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub weights: Vec<f64>,
    /// A View's Slot key.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub slot: Option<String>,
    /// What a View shows ([`content_kind`]).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub content: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub role: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub terminal: Option<Terminal>,
}

impl Props {
    fn is_empty(&self) -> bool {
        *self == Props::default()
    }
}

/// A terminal View's: from its Slot, the command it runs and where; from
/// the frontend, its session, size and, when asked, its screen.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Terminal {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub session: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cwd: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub process: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cols: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rows: Option<u32>,
    /// The visible rows, never scrollback.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cursor: Option<Cursor>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Cursor {
    pub row: u32,
    pub col: u32,
    pub visible: bool,
}

impl Node {
    fn new(id: String, kind: Kind) -> Self {
        Node { id, kind, name: None, state: Vec::new(), durable: true, props: Props::default(), children: Vec::new() }
    }

    /// This node and every node under it, parents first.
    pub fn preorder(&self) -> Vec<&Node> {
        let mut out = vec![self];
        for child in &self.children {
            out.extend(child.preorder());
        }
        out
    }

    /// This node and every node under it, parents first, to change.
    pub fn visit_mut(&mut self, f: &mut impl FnMut(&mut Node)) {
        f(self);
        for child in &mut self.children {
            child.visit_mut(f);
        }
    }
}

/// `nodes` as a JSON array.
pub fn to_json(nodes: &[Node]) -> Vec<u8> {
    serde_json::to_vec(nodes).expect("nodes serialise")
}

/// Who owns a View, as its View Address names it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ViewOwner {
    Workspace(WorkspaceId),
    /// A window, by its Window ID.
    Window(WorkspaceId),
}

impl ViewOwner {
    pub fn automation_id(&self) -> String {
        match self {
            ViewOwner::Workspace(id) => format!("workspace/{id}"),
            ViewOwner::Window(id) => format!("window/{id}"),
        }
    }
}

/// A View Address: its owner, key, and whether it is the previous instance
/// a keep-previous rebind kept.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ViewAddress {
    pub owner: ViewOwner,
    pub key: String,
    pub previous: bool,
}

impl ViewAddress {
    /// `<owner>/view/<key>`, with `@previous` for a kept previous instance.
    pub fn automation_id(&self) -> String {
        format!("{}/view/{}{}", self.owner.automation_id(), self.key, if self.previous { "@previous" } else { "" })
    }

    /// The address an Automation ID names, if it names a View.
    pub fn from_automation_id(id: &str) -> Option<Self> {
        let (owner, rest) = id.split_once('/')?;
        let (uuid, key) = rest.split_once("/view/")?;
        let uuid: WorkspaceId = uuid.parse().ok()?;
        let owner = match owner {
            "workspace" => ViewOwner::Workspace(uuid),
            "window" => ViewOwner::Window(uuid),
            _ => return None,
        };
        let (key, previous) = match key.strip_suffix("@previous") {
            Some(key) => (key, true),
            None => (key, false),
        };
        (!key.is_empty()).then(|| ViewAddress { owner, key: key.to_owned(), previous })
    }
}

/// What a View shows, as `props.content`: its presentation, else what its
/// content implies (`terminal`, `jackstay`, a `wheelhouse:<kind>` page's
/// kind, ...).
pub fn content_kind(spec: &ViewSpec) -> String {
    if let Some(presentation) = &spec.presentation {
        return presentation.clone();
    }
    match &spec.content {
        Content::ProviderFacet { facet, .. } => facet.clone(),
        Content::Local(LocalRecipe::Command { .. } | LocalRecipe::CleatSession { .. }) => "terminal".into(),
        Content::Local(LocalRecipe::Jackstay { .. }) => "jackstay".into(),
        Content::Local(LocalRecipe::File { .. }) => "text".into(),
        Content::Local(LocalRecipe::Url { url }) => match url.strip_prefix("wheelhouse:") {
            Some(page) => page.split(['?', '/']).next().unwrap_or(page).to_owned(),
            None => "web".into(),
        },
    }
}

/// A current View of a workspace's Slot.
fn view_node(workspace: WorkspaceId, slot: &SlotInfo, selected: bool) -> Node {
    let address = ViewAddress { owner: ViewOwner::Workspace(workspace), key: slot.key.clone(), previous: false };
    let mut node = Node::new(address.automation_id(), Kind::View);
    if selected {
        node.state.push(State::Selected);
    }
    node.props.slot = Some(slot.key.clone());
    node.props.content = Some(content_kind(&slot.spec));
    node.props.role = slot.spec.role.clone();
    // What the frontend knows of a terminal joins this.
    node.props.terminal = match &slot.spec.content {
        Content::Local(LocalRecipe::Command { line, cwd }) => Some(Terminal {
            process: Some(match line {
                CommandLine::Shell(line) => line.clone(),
                CommandLine::Argv(argv) => argv.join(" "),
            }),
            cwd: cwd.clone(),
            ..Terminal::default()
        }),
        Content::Local(LocalRecipe::CleatSession { session, .. }) => {
            Some(Terminal { session: Some(session.clone()), ..Terminal::default() })
        }
        _ => None,
    };
    node
}

fn panel_node(workspace: WorkspaceId, panel: &Panel, slots: &[SlotInfo], placed: &mut Vec<String>) -> Node {
    let mut node = Node::new(format!("workspace/{workspace}/panel/{}", panel.id), Kind::Panel);
    // The arrangement as shown: a collapsed panel takes no space.
    if panel.weight == 0.0 {
        node.state.push(State::Collapsed);
    }
    match &panel.node {
        PanelNode::Split { axis, children } => {
            node.props.split = Some(match axis {
                Axis::Row => Split::Row,
                Axis::Column => Split::Column,
            });
            node.props.weights = children.iter().map(|child| child.weight).collect();
            node.children = children.iter().map(|child| panel_node(workspace, child, slots, placed)).collect();
        }
        PanelNode::Tabs { tabs, selected } => {
            for tab in tabs {
                // A tab whose Slot has gone shows no current View.
                if let Some(slot) = slots.iter().find(|slot| &slot.key == tab) {
                    placed.push(tab.clone());
                    node.children.push(view_node(workspace, slot, selected.as_ref() == Some(tab)));
                }
            }
        }
    }
    node
}

/// A workspace's Semantic Tree: its panel tree from `doc`, the arrangement
/// as shown, with the current View of each placed Slot in its panel, then
/// those of Slots the arrangement doesn't place.
pub fn workspace_node(
    workspace: WorkspaceId,
    name: Option<String>,
    subject: Option<&EntityRef>,
    doc: &ArrangementDoc,
    slots: &[SlotInfo],
) -> Node {
    let mut node = Node::new(format!("workspace/{workspace}"), Kind::Workspace);
    node.name = name;
    node.props.subject = subject.cloned();
    let mut placed = Vec::new();
    if let Some(root) = &doc.root {
        node.children.push(panel_node(workspace, root, slots, &mut placed));
    }
    for slot in slots.iter().filter(|slot| !placed.contains(&slot.key)) {
        node.children.push(view_node(workspace, slot, false));
    }
    node
}

#[cfg(test)]
mod tests;
