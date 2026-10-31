//! The Semantic Tree served to automation clients (#554; CONTEXT.md,
//! Semantic Tree): each workspace, its panel tree from its arrangement and
//! the current Views of its Slots, with no geometry. Nodes carry Automation
//! IDs that mirror View Address (docs/protocol/automation-endpoint.md).
//! Frontends add what only they know, such as a terminal's size, to the
//! nodes by ID.
use serde_json::{json, Map, Value};

use crate::{
    slots::{ArrangementDoc, Panel, PanelNode, SlotInfo},
    suggested_layout::{Axis, CommandLine, Content, LocalRecipe, ViewSpec},
    EntityRef, WorkspaceId,
};

/// One Semantic Tree node: `{id, kind, name, state, durable, props,
/// children}`, empty fields left out when serialised.
#[derive(Clone, Debug, PartialEq)]
pub struct Node {
    pub id: String,
    pub kind: &'static str,
    pub name: Option<String>,
    pub state: Vec<&'static str>,
    pub durable: bool,
    pub props: Map<String, Value>,
    pub children: Vec<Node>,
}

impl Node {
    fn new(id: String, kind: &'static str) -> Self {
        Node { id, kind, name: None, state: Vec::new(), durable: true, props: Map::new(), children: Vec::new() }
    }

    pub fn to_value(&self) -> Value {
        let mut node = Map::new();
        node.insert("id".into(), self.id.clone().into());
        node.insert("kind".into(), self.kind.into());
        if let Some(name) = &self.name {
            node.insert("name".into(), name.clone().into());
        }
        if !self.state.is_empty() {
            node.insert("state".into(), self.state.iter().map(|s| Value::from(*s)).collect());
        }
        node.insert("durable".into(), self.durable.into());
        if !self.props.is_empty() {
            node.insert("props".into(), self.props.clone().into());
        }
        if !self.children.is_empty() {
            node.insert("children".into(), self.children.iter().map(Node::to_value).collect());
        }
        node.into()
    }

    /// This node and every node under it, parents first.
    pub fn preorder(&self) -> Vec<&Node> {
        let mut out = vec![self];
        for child in &self.children {
            out.extend(child.preorder());
        }
        out
    }
}

/// `nodes` as a JSON array.
pub fn to_json(nodes: &[Node]) -> Vec<u8> {
    serde_json::to_vec(&nodes.iter().map(Node::to_value).collect::<Vec<_>>()).expect("JSON values serialise")
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

fn entity_value(entity: &EntityRef) -> Value {
    json!({"provider": entity.provider, "kind": entity.kind, "id": entity.id})
}

/// A current View of a workspace's Slot.
fn view_node(workspace: WorkspaceId, slot: &SlotInfo, selected: bool) -> Node {
    let address = ViewAddress { owner: ViewOwner::Workspace(workspace), key: slot.key.clone(), previous: false };
    let mut node = Node::new(address.automation_id(), "view");
    if selected {
        node.state.push("selected");
    }
    node.props.insert("slot".into(), slot.key.clone().into());
    node.props.insert("content".into(), content_kind(&slot.spec).into());
    if let Some(role) = &slot.spec.role {
        node.props.insert("role".into(), role.clone().into());
    }
    // What the frontend knows of a terminal joins this.
    let terminal = match &slot.spec.content {
        Content::Local(LocalRecipe::Command { line, cwd }) => {
            let process = match line {
                CommandLine::Shell(line) => line.clone(),
                CommandLine::Argv(argv) => argv.join(" "),
            };
            let mut terminal = json!({"process": process});
            if let Some(cwd) = cwd {
                terminal["cwd"] = cwd.clone().into();
            }
            Some(terminal)
        }
        Content::Local(LocalRecipe::CleatSession { session, .. }) => Some(json!({"session": session})),
        _ => None,
    };
    if let Some(terminal) = terminal {
        node.props.insert("terminal".into(), terminal);
    }
    node
}

fn panel_node(workspace: WorkspaceId, panel: &Panel, slots: &[SlotInfo], placed: &mut Vec<String>) -> Node {
    let mut node = Node::new(format!("workspace/{workspace}/panel/{}", panel.id), "panel");
    // The arrangement as shown: a collapsed panel takes no space.
    if panel.weight == 0.0 {
        node.state.push("collapsed");
    }
    match &panel.node {
        PanelNode::Split { axis, children } => {
            // Row lays children out left to right; column, top to bottom.
            let split = match axis {
                Axis::Row => "row",
                Axis::Column => "column",
            };
            node.props.insert("split".into(), split.into());
            node.props.insert("weights".into(), children.iter().map(|child| Value::from(child.weight)).collect());
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
    let mut node = Node::new(format!("workspace/{workspace}"), "workspace");
    node.name = name;
    if let Some(subject) = subject {
        node.props.insert("subject".into(), entity_value(subject));
    }
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
