//! The overview's logical layout: the sidebar's live workspaces, grouped by
//! the hierarchy the sidebar places them in.
use std::collections::{BTreeMap, BTreeSet};
use std::rc::Rc;

use crate::presentation::{PlacementNode, PresentationState, Presented, SurfaceSnapshot, UNPLACED_WORKSPACES_SECTION};
use crate::{EntityRef, PlacementKey, WorkspaceId};

/// The heading of the group holding workspaces nothing places.
pub const OTHER_WORKSPACES: &str = "Other workspaces";

/// One place the sidebar shows a live workspace.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OverviewEntry {
    pub section: String,
    pub key: PlacementKey,
    /// The label of each node on `key`'s path, outermost first.
    pub labels: Vec<String>,
    pub workspace_id: WorkspaceId,
    pub presented: Presented,
}

/// Every place `surface` shows a live workspace, in sidebar order: sections
/// in order, each walked depth first. Collapsed nodes' children count too.
pub fn entries(surface: &SurfaceSnapshot) -> Vec<OverviewEntry> {
    fn walk(section: &str, nodes: &[Rc<PlacementNode>], labels: &mut Vec<String>, out: &mut Vec<OverviewEntry>) {
        for node in nodes {
            labels.push(node.label.clone());
            if let PresentationState::Live { workspace_id, .. } = node.state {
                out.push(OverviewEntry {
                    section: section.to_owned(),
                    key: node.key.clone(),
                    labels: labels.clone(),
                    workspace_id,
                    presented: node.content.presented.clone(),
                });
            }
            walk(section, &node.children, labels, out);
            labels.pop();
        }
    }
    let mut out = vec![];
    for section in &surface.sections {
        walk(&section.name, &section.nodes, &mut vec![], &mut out);
    }
    out
}

/// The kind of a local section's entity. It holds groups, so a path through
/// one is grouped by what comes after it.
pub const LOCAL_SECTION_KIND: &str = ".section";

/// What a group is: the entity at the top of a workspace's placement path,
/// below any local sections, whichever section places it, or none for
/// workspaces nothing places.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct GroupKey {
    pub head: Option<EntityRef>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct LogicalLayout {
    pub groups: Vec<Group>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Group {
    pub key: GroupKey,
    pub heading: String,
    pub clusters: Vec<Cluster>,
}

/// A group's workspaces under one of its children: a convoy's workspace and
/// its vessels', say. The group head's own workspace is a cluster of one.
#[derive(Debug, Clone, PartialEq)]
pub struct Cluster {
    pub key: PlacementKey,
    /// The cluster is the group head's own workspace, not a subtree under it.
    pub is_group_head: bool,
    /// Shown in the scene, in sidebar order.
    pub tiles: Vec<Tile>,
    /// Left out while collapsed, in sidebar order.
    pub hidden: Vec<Tile>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Tile {
    pub workspace_id: WorkspaceId,
    /// What tells the workspace apart in its group: its own node's label.
    pub title: String,
    /// The kind of the entity its node presents.
    pub kind: String,
    pub presented: Presented,
    pub current: bool,
}

/// Group `entries` into the overview's logical layout. A local group is a
/// group, like a project: the local sections above it only arrange the
/// sidebar. Each workspace
/// appears once, at its deepest place (the first, among equals), so a
/// workspace the sidebar also lists flat, under attention say, is grouped
/// by its hierarchy. Groups, clusters and tiles keep sidebar order.
///
/// A cluster in `collapsed` shows only its first workspace, unless it holds
/// `current`: the current workspace is always shown.
pub fn logical_layout(entries: &[OverviewEntry], current: Option<WorkspaceId>, collapsed: &BTreeSet<PlacementKey>) -> LogicalLayout {
    let mut chosen: BTreeMap<WorkspaceId, usize> = BTreeMap::new();
    for (index, entry) in entries.iter().enumerate() {
        let deeper = chosen.get(&entry.workspace_id).is_none_or(|&at| entry.key.0.len() > entries[at].key.0.len());
        if deeper {
            chosen.insert(entry.workspace_id, index);
        }
    }
    let mut order: Vec<usize> = chosen.into_values().collect();
    order.sort_unstable();

    let mut layout = LogicalLayout::default();
    for index in order {
        let entry = &entries[index];
        let segments = &entry.key.0;
        let unplaced =
            entry.section == UNPLACED_WORKSPACES_SECTION || segments.first().is_some_and(|s| s.loop_name == UNPLACED_WORKSPACES_SECTION);
        let start = segments.iter().take_while(|s| s.entity.kind == LOCAL_SECTION_KIND).count();
        let (key, heading, cluster_key) = if unplaced || segments.len() == start {
            (GroupKey { head: None }, OTHER_WORKSPACES.to_owned(), entry.key.clone())
        } else {
            let heading = entry.labels.get(start).cloned().unwrap_or_default();
            // The cluster keeps the sections, so it is the sidebar node's own
            // key, the one collapsing goes by.
            let cluster = PlacementKey(segments[..segments.len().min(start + 2)].to_vec());
            (GroupKey { head: Some(segments[start].entity.clone()) }, heading, cluster)
        };
        let is_group_head = key.head.is_some() && segments.len() == start + 1;
        let tile = Tile {
            workspace_id: entry.workspace_id,
            title: entry.labels.last().cloned().unwrap_or_default(),
            kind: segments.last().map(|s| s.entity.kind.clone()).unwrap_or_default(),
            presented: entry.presented.clone(),
            current: Some(entry.workspace_id) == current,
        };
        let group = match layout.groups.iter_mut().position(|g| g.key == key) {
            Some(at) => &mut layout.groups[at],
            None => {
                layout.groups.push(Group { key, heading, clusters: vec![] });
                layout.groups.last_mut().expect("just pushed")
            }
        };
        match group.clusters.iter_mut().find(|c| c.key == cluster_key) {
            Some(cluster) => cluster.tiles.push(tile),
            None => group.clusters.push(Cluster { key: cluster_key, is_group_head, tiles: vec![tile], hidden: vec![] }),
        }
    }
    for cluster in layout.groups.iter_mut().flat_map(|g| g.clusters.iter_mut()) {
        if collapsed.contains(&cluster.key) && !cluster.tiles.iter().any(|t| t.current) && cluster.tiles.len() > 1 {
            cluster.hidden = cluster.tiles.split_off(1);
        }
    }
    layout
}
