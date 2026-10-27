//! What crosses to the browser: the change stream's deltas, intents and
//! their receipts, as plain objects with generated TypeScript types. They
//! project the core's types as the C ABI does, keeping what a page renders
//! and leaving out what it doesn't (effective KDL, chrome, facts).
use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};
use tsify::Tsify;
use wheelhouse_core::{
    presentation::{Content, PresentationState, Presented as CorePresented},
    sidebar::{HostEffect, Intent as CoreIntent, IntentReceipt},
    stream::{ListOp as CoreListOp, NodeContent, Parent, SectionContent, SidebarDelta, SidebarOp, StreamView},
    template_config::{TemplateConfigFieldClass, TemplateControlKind},
    DisplayVariableValue, EntityRef, PlacementKey,
};

/// An entity: its provider (the subscription its facts come from), kind
/// and ID.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize, Tsify)]
pub struct Entity {
    pub provider: String,
    pub kind: String,
    pub id: String,
}

impl From<&EntityRef> for Entity {
    fn from(entity: &EntityRef) -> Self {
        Self { provider: entity.provider.clone(), kind: entity.kind.clone(), id: entity.id.clone() }
    }
}

impl From<&Entity> for EntityRef {
    fn from(entity: &Entity) -> Self {
        EntityRef::new(&entity.provider, &entity.kind, &entity.id)
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "state", rename_all = "kebab-case")]
pub enum State {
    Catalog,
    Latent {
        openable: bool,
    },
    Opening,
    Live {
        #[serde(rename = "workspaceId")]
        workspace_id: String,
        selected: bool,
    },
}

impl From<&PresentationState> for State {
    fn from(state: &PresentationState) -> Self {
        match state {
            PresentationState::Catalog => Self::Catalog,
            PresentationState::Latent { openable } => Self::Latent { openable: *openable },
            PresentationState::Opening => Self::Opening,
            PresentationState::Live { workspace_id, selected } => {
                Self::Live { workspace_id: workspace_id.to_string(), selected: *selected }
            }
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "kebab-case")]
pub enum FieldClass {
    Required,
    Optional,
    Priority,
}

/// A rendered field: text to show, and how much it matters when room runs
/// out.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
pub struct Field {
    pub text: String,
    pub class: FieldClass,
    pub priority: Option<i64>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "kebab-case")]
pub enum ControlKind {
    OpenConfig,
    DisplayVariable,
    ScrollDown,
    ScrollUp,
    InspectRoot,
}

/// A control. A display-variable control's intent toggles its variable,
/// whose value comes with the display variables.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
pub struct Control {
    pub kind: ControlKind,
    pub variable: Option<String>,
    pub glyph: Option<String>,
}

/// Values a template presents by meaning: each frontend decides how they
/// look.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct Presented {
    pub status: Option<String>,
    pub attention: bool,
    /// A symbolic name or a glyph.
    pub icon: Option<String>,
    pub short_label: Option<String>,
    pub medium_label: Option<String>,
}

impl From<&CorePresented> for Presented {
    fn from(p: &CorePresented) -> Self {
        Self {
            status: p.status.clone(),
            attention: p.attention,
            icon: p.icon.clone(),
            short_label: p.short_label.clone(),
            medium_label: p.medium_label.clone(),
        }
    }
}

/// A section's or node's rendered content.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
pub struct Rendered {
    pub fields: Vec<Field>,
    pub controls: Vec<Control>,
    pub presented: Presented,
    pub error: Option<String>,
}

impl From<&Content> for Rendered {
    fn from(content: &Content) -> Self {
        Self {
            fields: content
                .fields
                .iter()
                .map(|f| Field {
                    text: f.value.clone(),
                    class: match f.class {
                        TemplateConfigFieldClass::Required => FieldClass::Required,
                        TemplateConfigFieldClass::Optional => FieldClass::Optional,
                        TemplateConfigFieldClass::Priority => FieldClass::Priority,
                    },
                    priority: f.priority,
                })
                .collect(),
            controls: content
                .controls
                .iter()
                .map(|c| Control {
                    kind: match c.kind {
                        TemplateControlKind::OpenConfig => ControlKind::OpenConfig,
                        TemplateControlKind::DisplayVariable => ControlKind::DisplayVariable,
                        TemplateControlKind::ScrollDown => ControlKind::ScrollDown,
                        TemplateControlKind::ScrollUp => ControlKind::ScrollUp,
                        TemplateControlKind::InspectRoot => ControlKind::InspectRoot,
                    },
                    variable: c.variable.clone(),
                    glyph: c.glyph.clone(),
                })
                .collect(),
            presented: (&content.presented).into(),
            error: content.error.clone(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct Section {
    pub name: String,
    pub default_host: Option<String>,
    pub order: Option<i64>,
    pub pinned: bool,
    pub content: Rendered,
}

impl From<&SectionContent> for Section {
    fn from(s: &SectionContent) -> Self {
        Self { name: s.name.clone(), default_host: s.default_host.clone(), order: s.order, pinned: s.pinned, content: (&s.content).into() }
    }
}

/// A node's own content, without its children. `key` is opaque and unique
/// within its section: compare it, and give it back in intents.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(rename_all = "camelCase")]
pub struct Node {
    pub key: String,
    /// Opaque: siblings from one loop share it.
    pub loop_key: String,
    pub entity: Entity,
    pub label: String,
    pub layout: Option<String>,
    pub form: String,
    pub state: State,
    pub content: Rendered,
    pub detail: Rendered,
    pub collapsed: bool,
    pub stale: bool,
    /// Its subject has a URL: copy-subject-url of its entity copies it.
    pub has_url: bool,
}

impl From<&NodeContent> for Node {
    fn from(n: &NodeContent) -> Self {
        Self {
            key: n.key.encode(),
            loop_key: n.loop_key.encode(),
            entity: (&n.entity).into(),
            label: n.label.clone(),
            layout: n.layout.clone(),
            form: n.form.clone(),
            state: (&n.state).into(),
            content: (&n.content).into(),
            detail: (&n.detail).into(),
            collapsed: n.collapsed,
            stale: n.stale,
            has_url: n.has_url,
        }
    }
}

/// A positional change to a list: a move takes the item out at `from` and
/// puts it in at `to` in the list without it.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "op", rename_all = "kebab-case")]
pub enum ListOp {
    Insert { at: usize, key: String },
    Remove { at: usize },
    Move { from: usize, to: usize },
}

fn list_ops<K>(ops: &[CoreListOp<K>], key: impl Fn(&K) -> String) -> Vec<ListOp> {
    ops.iter()
        .map(|op| match op {
            CoreListOp::Insert { at, key: k } => ListOp::Insert { at: *at, key: key(k) },
            CoreListOp::Remove { at } => ListOp::Remove { at: *at },
            CoreListOp::Move { from, to } => ListOp::Move { from: *from, to: *to },
        })
        .collect()
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(untagged)]
pub enum Value {
    Bool(bool),
    Enum(String),
}

/// A display variable and its value.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
pub struct Variable {
    pub name: String,
    pub label: String,
    pub icon: String,
    pub persist: bool,
    pub value: Option<Value>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
pub struct ActivationError {
    pub entity: Entity,
    pub message: String,
}

/// An operation of a delta, applied in order.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "op", rename_all = "kebab-case")]
pub enum Op {
    /// A section's content, new or changed.
    Section {
        section: Section,
    },
    /// A section and its child list go; its nodes go by their own ops.
    RemoveSection {
        name: String,
    },
    /// The order of the sections (keys are names).
    Sections {
        ops: Vec<ListOp>,
    },
    /// A node's content, new or changed.
    Upsert {
        section: String,
        node: Node,
    },
    /// A node and its child list go; its descendants go by their own ops.
    Remove {
        section: String,
        key: String,
    },
    /// A child list changes: the node `parent`'s, or the section's own when
    /// `parent` is null.
    Children {
        section: String,
        parent: Option<String>,
        ops: Vec<ListOp>,
    },
    DisplayVariables {
        variables: Vec<Variable>,
    },
    Diagnostics {
        diagnostics: Vec<String>,
    },
    Errors {
        errors: Vec<ActivationError>,
    },
}

/// One poll's changes, applied whole.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
pub struct Delta {
    /// The model's generation once this delta is applied.
    pub generation: u64,
    pub ops: Vec<Op>,
    /// Intents accepted since the last delta, in order: all they changed is
    /// in this delta or an earlier one.
    pub intents: Vec<u64>,
}

fn variables(
    variables: &[wheelhouse_core::template_config::TemplateVariableDefinition],
    values: &BTreeMap<String, DisplayVariableValue>,
) -> Vec<Variable> {
    variables
        .iter()
        .map(|v| Variable {
            name: v.name.clone(),
            label: v.label.clone(),
            icon: v.icon.clone(),
            persist: v.persist,
            value: values.get(&v.name).map(|value| match value {
                DisplayVariableValue::Bool(b) => Value::Bool(*b),
                DisplayVariableValue::Enum(e) => Value::Enum(e.clone()),
            }),
        })
        .collect()
}

impl From<&SidebarDelta> for Delta {
    fn from(delta: &SidebarDelta) -> Self {
        let ops = delta
            .ops
            .iter()
            .map(|op| match op {
                SidebarOp::Section(section) => Op::Section { section: (&**section).into() },
                SidebarOp::RemoveSection { name } => Op::RemoveSection { name: name.clone() },
                SidebarOp::Sections { ops } => Op::Sections { ops: list_ops(ops, Clone::clone) },
                SidebarOp::Upsert { section, node } => Op::Upsert { section: section.clone(), node: (&**node).into() },
                SidebarOp::Remove(id) => Op::Remove { section: id.section.clone(), key: id.key.encode() },
                SidebarOp::Children { parent, ops } => {
                    let (section, parent) = match parent {
                        Parent::Section { name } => (name.clone(), None),
                        Parent::Node(id) => (id.section.clone(), Some(id.key.encode())),
                    };
                    Op::Children { section, parent, ops: list_ops(ops, PlacementKey::encode) }
                }
                SidebarOp::DisplayVariables { variables: v, values } => Op::DisplayVariables { variables: variables(v, values) },
                SidebarOp::Diagnostics { diagnostics } => Op::Diagnostics { diagnostics: diagnostics.clone() },
                SidebarOp::Errors { errors } => Op::Errors {
                    errors: errors.iter().map(|e| ActivationError { entity: (&e.entity).into(), message: e.message.clone() }).collect(),
                },
            })
            .collect();
        Self { generation: delta.generation, ops, intents: delta.intents.clone() }
    }
}

/// One section of a [`View`]: its content, nodes by key, and child lists by
/// parent key (`""` for the section's own).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[tsify(hashmap_as_object)]
pub struct SectionView {
    pub section: Section,
    pub nodes: BTreeMap<String, Node>,
    pub children: BTreeMap<String, Vec<String>>,
}

/// A whole model, built directly: what following the deltas must give.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[tsify(hashmap_as_object)]
pub struct View {
    pub generation: u64,
    pub sections: Vec<SectionView>,
    pub variables: Vec<Variable>,
    pub diagnostics: Vec<String>,
    pub errors: Vec<ActivationError>,
}

impl From<&StreamView> for View {
    fn from(view: &StreamView) -> Self {
        let sections = view
            .sections
            .iter()
            .map(|name| {
                let mut section = SectionView { section: (&view.section[name]).into(), nodes: BTreeMap::new(), children: BTreeMap::new() };
                for (parent, keys) in &view.children {
                    let parent = match parent {
                        Parent::Section { name: of } if of == name => String::new(),
                        Parent::Node(id) if &id.section == name => id.key.encode(),
                        _ => continue,
                    };
                    section.children.insert(parent, keys.iter().map(PlacementKey::encode).collect());
                }
                for (id, node) in &view.nodes {
                    if &id.section == name {
                        section.nodes.insert(id.key.encode(), node.into());
                    }
                }
                section
            })
            .collect();
        Self {
            generation: view.generation,
            sections,
            variables: variables(&view.display_variables, &view.display_values),
            diagnostics: view.diagnostics.clone(),
            errors: view.errors.iter().map(|e| ActivationError { entity: (&e.entity).into(), message: e.message.clone() }).collect(),
        }
    }
}

/// A request to change the sidebar's logical state, by its stable name.
/// Nodes are named by the keys deltas give.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "intent", rename_all = "kebab-case")]
pub enum Intent {
    CopySubjectUrl { entity: Entity },
    ActivatePlacement { key: String },
    Activate { entity: Entity },
    TogglePlacement { key: String },
    SetVariable { key: String, name: String, value: Option<String> },
    ToggleDisplayVariable { name: String },
}

impl Intent {
    pub fn read(&self) -> Result<CoreIntent, String> {
        let key = |key: &str| PlacementKey::decode(key).ok_or_else(|| format!("malformed placement key {key:?}"));
        Ok(match self {
            Self::CopySubjectUrl { entity } => CoreIntent::CopySubjectUrl { entity: entity.into() },
            Self::ActivatePlacement { key: k } => CoreIntent::ActivatePlacement { key: key(k)? },
            Self::Activate { entity } => CoreIntent::Activate { entity: entity.into() },
            Self::TogglePlacement { key: k } => CoreIntent::TogglePlacement { key: key(k)? },
            Self::SetVariable { key: k, name, value } => CoreIntent::SetVariable { key: key(k)?, name: name.clone(), value: value.clone() },
            Self::ToggleDisplayVariable { name } => CoreIntent::ToggleDisplayVariable { name: name.clone() },
        })
    }
}

/// What the page carries out for an intent it sent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "effect", rename_all = "kebab-case")]
pub enum Effect {
    /// Focus the workspace, then complete the request.
    Focus {
        request: u64,
        #[serde(rename = "workspaceId")]
        workspace_id: String,
    },
    /// Make a workspace for the entity, then complete the request with it.
    Materialize {
        request: u64,
        entity: Entity,
        name: String,
        recipe: Option<String>,
        cwd: Option<String>,
    },
    /// Show the entity's inspector.
    Inspect {
        entity: Entity,
    },
    OpenUrl {
        url: String,
    },
    CopyUrl {
        url: String,
    },
}

impl From<&HostEffect> for Effect {
    fn from(effect: &HostEffect) -> Self {
        match effect {
            HostEffect::Focus { request_id, workspace_id } => Self::Focus { request: *request_id, workspace_id: workspace_id.to_string() },
            HostEffect::Materialize { request_id, entity, name, recipe, cwd, .. } => Self::Materialize {
                request: *request_id,
                entity: entity.into(),
                name: name.clone(),
                recipe: recipe.clone(),
                cwd: cwd.clone(),
            },
            HostEffect::Inspect { entity } => Self::Inspect { entity: entity.into() },
            HostEffect::OpenUrl { url } => Self::OpenUrl { url: url.clone() },
            HostEffect::CopyUrl { url } => Self::CopyUrl { url: url.clone() },
        }
    }
}

/// What became of an intent: accepted, with its request (a later delta lists
/// it) and the effects to carry out, or refused, and nothing changed.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Tsify)]
#[serde(tag = "receipt", rename_all = "kebab-case")]
pub enum Receipt {
    Accepted { request: u64, effects: Vec<Effect> },
    Refused { reason: String },
}

impl From<IntentReceipt> for Receipt {
    fn from(receipt: IntentReceipt) -> Self {
        match receipt {
            IntentReceipt::Accepted { request, effects } => Self::Accepted { request, effects: effects.iter().map(Into::into).collect() },
            IntentReceipt::Refused { reason } => Self::Refused { reason },
        }
    }
}
