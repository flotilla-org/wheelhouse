//! The sidebar's change stream (docs/design/change-stream.md). A frontend
//! subscribes, and each poll gives it a delta from the surface it last had to
//! the current one; its first poll gives everything. A delta is applied
//! whole, and its generation is the view's once applied.
//!
//! Deltas come from comparing two surfaces, not from a log: evaluation keeps
//! an unchanged node as the same `Rc`, so the walk skips every pair that is
//! `Rc::ptr_eq` and costs in proportion to what changed and the child lists
//! on the way to it. A subscriber that falls behind gets one delta to now.
//!
//! A node is named by its section and placement key: two sections can place
//! the same entity by the same loops (regions sharing a placement), and a
//! node's key, its path from its section, names it only within that.
use std::collections::{BTreeMap, HashMap, HashSet};
use std::hash::Hash;
use std::rc::Rc;

use serde::{Deserialize, Serialize};

use crate::presentation::{Content, PlacementNode, PresentationState, Section};
use crate::sidebar::{ActivationError, Snapshot};
use crate::template_config::TemplateVariableDefinition;
use crate::{DisplayVariableValue, EntityRef, PlacementKey, PlacementLoopKey};

/// A subscription to the sidebar's changes; see [`Sidebar::subscribe`](crate::sidebar::Sidebar::subscribe).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct SubscriptionId(pub u64);

/// What a frontend has of the sidebar: everything a delta can change.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct StreamView {
    pub generation: u64,
    /// Section names, in order.
    pub sections: Vec<String>,
    pub section: HashMap<String, SectionContent>,
    /// Each section's and node's children, in order; absent when empty.
    pub children: HashMap<Parent, Vec<PlacementKey>>,
    pub nodes: HashMap<NodeId, NodeContent>,
    pub display_variables: Vec<TemplateVariableDefinition>,
    pub display_values: BTreeMap<String, DisplayVariableValue>,
    pub diagnostics: Vec<String>,
    pub errors: Vec<ActivationError>,
}

/// A node: its placement key within its section.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct NodeId {
    pub section: String,
    pub key: PlacementKey,
}

/// Whose child list a [`SidebarOp::Children`] changes. A child list holds
/// placement keys, in the parent's section.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[serde(tag = "parent", rename_all = "kebab-case")]
pub enum Parent {
    Section { name: String },
    Node(NodeId),
}

impl Parent {
    fn section(&self) -> &str {
        match self {
            Self::Section { name } => name,
            Self::Node(id) => &id.section,
        }
    }
}

/// A positional change to an ordered list. A move takes the item out at
/// `from` and puts it in at `to` in the list without it.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "kebab-case")]
pub enum ListOp<K> {
    Insert { at: usize, key: K },
    Remove { at: usize },
    Move { from: usize, to: usize },
}

/// A section without its nodes.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SectionContent {
    pub name: String,
    pub default_host: Option<String>,
    pub order: Option<i64>,
    pub pinned: bool,
    pub content: Content,
}

/// A node's own content, without its children. Its facts and variables
/// stay in the core, as detail cards do: frontends ask for them.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct NodeContent {
    pub key: PlacementKey,
    pub loop_key: PlacementLoopKey,
    pub entity: EntityRef,
    pub label: String,
    pub layout: Option<String>,
    pub form: String,
    pub state: PresentationState,
    pub content: Content,
    pub detail: Content,
    pub collapsed: bool,
    pub stale: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "kebab-case")]
pub enum SidebarOp {
    /// A section's content, new or changed.
    Section(Box<SectionContent>),
    /// A section and its child list go; its nodes are removed by their own ops.
    RemoveSection {
        name: String,
    },
    /// The order of the sections.
    Sections {
        ops: Vec<ListOp<String>>,
    },
    /// A node's content, new or changed.
    Upsert {
        section: String,
        node: Box<NodeContent>,
    },
    /// A node and its child list go; its descendants are removed by their own ops.
    Remove(NodeId),
    Children {
        parent: Parent,
        ops: Vec<ListOp<PlacementKey>>,
    },
    DisplayVariables {
        variables: Vec<TemplateVariableDefinition>,
        values: BTreeMap<String, DisplayVariableValue>,
    },
    Diagnostics {
        diagnostics: Vec<String>,
    },
    Errors {
        errors: Vec<ActivationError>,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SidebarDelta {
    /// The view's generation once this delta is applied.
    pub generation: u64,
    pub ops: Vec<SidebarOp>,
}

impl SectionContent {
    fn of(section: &Section) -> Self {
        Self {
            name: section.name.clone(),
            default_host: section.default_host.clone(),
            order: section.order,
            pinned: section.pinned,
            content: section.content.clone(),
        }
    }

    fn describes(&self, section: &Section) -> bool {
        self.name == section.name
            && self.default_host == section.default_host
            && self.order == section.order
            && self.pinned == section.pinned
            && self.content == section.content
    }
}

impl NodeContent {
    fn of(node: &PlacementNode) -> Self {
        Self {
            key: node.key.clone(),
            loop_key: node.loop_key.clone(),
            entity: node.entity.clone(),
            label: node.label.clone(),
            layout: node.layout.clone(),
            form: node.form.clone(),
            state: node.state.clone(),
            content: node.content.clone(),
            detail: node.detail.clone(),
            collapsed: node.collapsed,
            stale: node.stale,
        }
    }
}

/// Whether two nodes' own content is the same, without copying either.
fn same_content(a: &PlacementNode, b: &PlacementNode) -> bool {
    a.key == b.key
        && a.loop_key == b.loop_key
        && a.entity == b.entity
        && a.label == b.label
        && a.layout == b.layout
        && a.form == b.form
        && a.state == b.state
        && a.content == b.content
        && a.detail == b.detail
        && a.collapsed == b.collapsed
        && a.stale == b.stale
}

impl StreamView {
    /// The view of a snapshot, built directly rather than from deltas.
    pub fn of(snapshot: &Snapshot) -> Self {
        fn add(view: &mut StreamView, parent: Parent, nodes: &[Rc<PlacementNode>]) {
            if nodes.is_empty() {
                return;
            }
            let section = parent.section().to_owned();
            view.children.insert(parent, nodes.iter().map(|node| node.key.clone()).collect());
            for node in nodes {
                let id = NodeId { section: section.clone(), key: node.key.clone() };
                view.nodes.insert(id.clone(), NodeContent::of(node));
                add(view, Parent::Node(id), &node.children);
            }
        }
        let surface = &snapshot.surface;
        let mut view = Self {
            generation: snapshot.revision,
            sections: surface.sections.iter().map(|section| section.name.clone()).collect(),
            display_variables: surface.display_variables.clone(),
            display_values: surface.display_values.clone(),
            diagnostics: surface.diagnostics.clone(),
            errors: snapshot.errors.clone(),
            ..Self::default()
        };
        for section in &surface.sections {
            view.section.insert(section.name.clone(), SectionContent::of(section));
            add(&mut view, Parent::Section { name: section.name.clone() }, &section.nodes);
        }
        view
    }

    /// Apply a delta whole. An operation that doesn't fit the view (a
    /// position past a list's end) fails it, leaving the view part-changed:
    /// the frontend's model and the core's have diverged, and it should
    /// subscribe again.
    pub fn apply(&mut self, delta: &SidebarDelta) -> Result<(), String> {
        for op in &delta.ops {
            match op {
                SidebarOp::Section(content) => {
                    self.section.insert(content.name.clone(), (**content).clone());
                }
                SidebarOp::RemoveSection { name } => {
                    self.section.remove(name);
                    self.children.remove(&Parent::Section { name: name.clone() });
                }
                SidebarOp::Sections { ops } => apply_list(&mut self.sections, ops)?,
                SidebarOp::Upsert { section, node } => {
                    self.nodes.insert(NodeId { section: section.clone(), key: node.key.clone() }, (**node).clone());
                }
                SidebarOp::Remove(id) => {
                    self.nodes.remove(id);
                    self.children.remove(&Parent::Node(id.clone()));
                }
                SidebarOp::Children { parent, ops } => {
                    let list = self.children.entry(parent.clone()).or_default();
                    apply_list(list, ops)?;
                    if list.is_empty() {
                        self.children.remove(parent);
                    }
                }
                SidebarOp::DisplayVariables { variables, values } => {
                    self.display_variables = variables.clone();
                    self.display_values = values.clone();
                }
                SidebarOp::Diagnostics { diagnostics } => self.diagnostics = diagnostics.clone(),
                SidebarOp::Errors { errors } => self.errors = errors.clone(),
            }
        }
        self.generation = delta.generation;
        Ok(())
    }
}

pub fn apply_list<K>(list: &mut Vec<K>, ops: &[ListOp<K>]) -> Result<(), String>
where
    K: Clone,
{
    for op in ops {
        match op {
            ListOp::Insert { at, key } if *at <= list.len() => list.insert(*at, key.clone()),
            ListOp::Remove { at } if *at < list.len() => {
                list.remove(*at);
            }
            ListOp::Move { from, to } if *from < list.len() && *to < list.len() => {
                let key = list.remove(*from);
                list.insert(*to, key);
            }
            _ => return Err("a list operation is out of range".into()),
        }
    }
    Ok(())
}

/// Operations that turn `before` into `after`, both lists of distinct keys.
/// Keys in both keep their place where they can: the longest run already in
/// order stays put, and only the others move, each at most twice.
pub fn list_ops<K>(before: &[K], after: &[K]) -> Vec<ListOp<K>>
where
    K: Clone + Eq + Hash,
{
    let wanted: HashSet<&K> = after.iter().collect();
    let mut list: Vec<K> = before.to_vec();
    let mut ops = vec![];
    for at in (0..list.len()).rev() {
        if !wanted.contains(&list[at]) {
            list.remove(at);
            ops.push(ListOp::Remove { at });
        }
    }
    let position: HashMap<&K, usize> = list.iter().enumerate().map(|(at, key)| (key, at)).collect();
    // Of the kept keys, in their new order, those whose old positions form
    // the longest increasing run stay where they are.
    let kept: Vec<(usize, usize)> = after.iter().enumerate().filter_map(|(i, key)| Some((i, *position.get(key)?))).collect();
    let mut stable = vec![false; after.len()];
    for k in longest_increasing(&kept.iter().map(|&(_, at)| at).collect::<Vec<_>>()) {
        stable[kept[k].0] = true;
    }
    for (i, key) in after.iter().enumerate() {
        loop {
            if list.get(i) == Some(key) {
                break;
            }
            // Everything before `i` is in place and the stable keys are in
            // order, so what sits here is a key that moves: send it to the end.
            if stable[i] {
                let last = list.len() - 1;
                let moved = list.remove(i);
                list.push(moved);
                ops.push(ListOp::Move { from: i, to: last });
                continue;
            }
            if let Some(from) = list[i..].iter().position(|k| k == key).map(|at| at + i) {
                let moved = list.remove(from);
                list.insert(i, moved);
                ops.push(ListOp::Move { from, to: i });
            } else {
                list.insert(i, key.clone());
                ops.push(ListOp::Insert { at: i, key: key.clone() });
            }
            break;
        }
    }
    ops
}

/// Indices into `values` of one longest strictly increasing subsequence.
fn longest_increasing(values: &[usize]) -> Vec<usize> {
    // tails[l]: the index ending the smallest-ended run of length l + 1.
    let mut tails: Vec<usize> = vec![];
    let mut previous = vec![usize::MAX; values.len()];
    for (i, &value) in values.iter().enumerate() {
        let length = tails.partition_point(|&t| values[t] < value);
        if length > 0 {
            previous[i] = tails[length - 1];
        }
        if length == tails.len() {
            tails.push(i);
        } else {
            tails[length] = i;
        }
    }
    let mut run = vec![];
    let mut at = tails.last().copied().unwrap_or(usize::MAX);
    while at != usize::MAX {
        run.push(at);
        at = previous[at];
    }
    run.reverse();
    run
}

/// The operations that turn `before` (nothing, for a first poll) into `after`.
pub(crate) fn delta(before: Option<&Snapshot>, after: &Snapshot) -> Vec<SidebarOp> {
    let mut walk = Walk::default();
    let old: HashMap<&str, &Section> =
        before.into_iter().flat_map(|snapshot| &snapshot.surface.sections).map(|section| (section.name.as_str(), section)).collect();
    for section in &after.surface.sections {
        let prior = old.get(section.name.as_str());
        if !prior.is_some_and(|prior| SectionContent::of(prior).describes(section)) {
            walk.ops.push(SidebarOp::Section(Box::new(SectionContent::of(section))));
        }
        walk.list(Parent::Section { name: section.name.clone() }, prior.map_or(&[][..], |prior| &prior.nodes), &section.nodes);
    }
    let names: HashSet<&str> = after.surface.sections.iter().map(|section| section.name.as_str()).collect();
    for section in before.into_iter().flat_map(|snapshot| &snapshot.surface.sections) {
        if !names.contains(section.name.as_str()) {
            walk.ops.push(SidebarOp::RemoveSection { name: section.name.clone() });
            for node in &section.nodes {
                walk.remove(&section.name, node);
            }
        }
    }
    let order = list_ops(
        &before
            .map(|snapshot| snapshot.surface.sections.iter().map(|section| section.name.clone()).collect::<Vec<_>>())
            .unwrap_or_default(),
        &after.surface.sections.iter().map(|section| section.name.clone()).collect::<Vec<_>>(),
    );
    if !order.is_empty() {
        walk.ops.push(SidebarOp::Sections { ops: order });
    }
    let surface = &after.surface;
    let prior = before.map(|snapshot| (&snapshot.surface, &snapshot.errors));
    if prior.is_none_or(|(s, _)| s.display_variables != surface.display_variables || s.display_values != surface.display_values) {
        walk.ops.push(SidebarOp::DisplayVariables { variables: surface.display_variables.clone(), values: surface.display_values.clone() });
    }
    if prior.is_none_or(|(s, _)| s.diagnostics != surface.diagnostics) {
        walk.ops.push(SidebarOp::Diagnostics { diagnostics: surface.diagnostics.clone() });
    }
    if prior.is_none_or(|(_, errors)| errors != &after.errors) {
        walk.ops.push(SidebarOp::Errors { errors: after.errors.clone() });
    }
    walk.ops
}

#[derive(Default)]
struct Walk {
    ops: Vec<SidebarOp>,
}

impl Walk {
    /// Compare a parent's children. A node's key is its path from its
    /// section, so a node is the same node only under the same parent.
    fn list(&mut self, parent: Parent, before: &[Rc<PlacementNode>], after: &[Rc<PlacementNode>]) {
        let section = parent.section().to_owned();
        if before.len() == after.len() && before.iter().zip(after).all(|(b, a)| b.key == a.key) {
            for (b, a) in before.iter().zip(after) {
                self.node(&section, b, a);
            }
            return;
        }
        let position: HashMap<&PlacementKey, usize> = before.iter().enumerate().map(|(at, node)| (&node.key, at)).collect();
        for node in after {
            match position.get(&node.key) {
                Some(&at) => self.node(&section, &before[at], node),
                None => self.add(&section, node),
            }
        }
        let kept: HashSet<&PlacementKey> = after.iter().map(|node| &node.key).collect();
        for node in before {
            if !kept.contains(&node.key) {
                self.remove(&section, node);
            }
        }
        let keys = |nodes: &[Rc<PlacementNode>]| nodes.iter().map(|node| node.key.clone()).collect::<Vec<_>>();
        self.ops.push(SidebarOp::Children { parent, ops: list_ops(&keys(before), &keys(after)) });
    }

    fn node(&mut self, section: &str, before: &Rc<PlacementNode>, after: &Rc<PlacementNode>) {
        if Rc::ptr_eq(before, after) {
            return;
        }
        if !same_content(before, after) {
            self.ops.push(SidebarOp::Upsert { section: section.to_owned(), node: Box::new(NodeContent::of(after)) });
        }
        let parent = Parent::Node(NodeId { section: section.to_owned(), key: after.key.clone() });
        self.list(parent, &before.children, &after.children);
    }

    fn add(&mut self, section: &str, node: &PlacementNode) {
        self.ops.push(SidebarOp::Upsert { section: section.to_owned(), node: Box::new(NodeContent::of(node)) });
        let parent = Parent::Node(NodeId { section: section.to_owned(), key: node.key.clone() });
        self.list(parent, &[], &node.children);
    }

    fn remove(&mut self, section: &str, node: &PlacementNode) {
        self.ops.push(SidebarOp::Remove(NodeId { section: section.to_owned(), key: node.key.clone() }));
        for child in &node.children {
            self.remove(section, child);
        }
    }
}
