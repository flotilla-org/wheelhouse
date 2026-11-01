//! A test-only transcription of the native rules lifecycle.rs replaces
//! (#631), over a plain list of a window's workspace nodes, so the core can
//! be compared with them (lifecycle/tests.rs). Each rule cites the C it
//! transcribes, at `f17979d7`. It goes once #631's operator check passes,
//! as the arrangement oracle did after #518; the invariants in tests.rs
//! stay.
use crate::lifecycle::{CloseKind, EffectiveHome};
use crate::WorkspaceId;

/// A window child that is a workspace: `workspace` (open) or
/// `detached_workspace` (kept), with its label, its subject (and whether
/// that has ended, as its row's status shows), and its `lives_in` and
/// `lives_with` fields (empty: none).
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Node {
    pub id: WorkspaceId,
    pub detached: bool,
    pub label: String,
    pub subject: Option<usize>,
    pub lives_in: String,
    pub lives_with: String,
}

/// A local group, as `uishell_sidebar_locals` reads it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Group {
    pub id: String,
    pub is_default: bool,
}

/// Where a new workspace is made: `uishell_new_workspace` from the + entry
/// or the palette, `uishell_sidebar_local_new_workspace` from a group's
/// footer, or `uishell_sidebar_make` for a project.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum MadeIn {
    Plain,
    Group(String),
    Project(String),
}

/// What closing did: whether the node was kept, and the workspace selected
/// after, or a fresh one made when it was the last.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Close {
    pub kept: bool,
    pub next: Option<WorkspaceId>,
}

/// One window: its workspace nodes in config order, the selected one
/// (`root_controlled_split_selected_workspace_id`), its local groups, and
/// which subjects have ended.
#[derive(Clone, Debug, Default)]
pub(crate) struct Window {
    pub nodes: Vec<Node>,
    pub selected: Option<WorkspaceId>,
    pub groups: Vec<Group>,
    pub ended: Vec<bool>,
}

impl Window {
    /// The open workspaces in order: the split's inventory.
    pub fn open(&self) -> Vec<WorkspaceId> {
        self.nodes.iter().filter(|node| !node.detached).map(|node| node.id).collect()
    }

    fn node(&mut self, id: WorkspaceId) -> &mut Node {
        self.nodes.iter_mut().find(|node| node.id == id).expect("a node")
    }

    /// `uishell_new_workspace` (shell_commands.c:1422): "Workspace N" with the
    /// lowest N no `workspace` or `detached_workspace` node uses, appended
    /// and selected; then `uishell_sidebar_local_new_workspace`
    /// (uishell_local_groups.c:313) gives it `lives_in` unless the group is
    /// the default, `uishell_sidebar_make` (:441) `lives_with` for a project
    /// and the typed name.
    pub fn new_workspace(&mut self, id: WorkspaceId, name: Option<&str>, made_in: &MadeIn) {
        let mut number = 1;
        while self.nodes.iter().any(|node| node.label == format!("Workspace {number}")) {
            number += 1;
        }
        let mut node = Node {
            id,
            detached: false,
            label: format!("Workspace {number}"),
            subject: None,
            lives_in: String::new(),
            lives_with: String::new(),
        };
        match made_in {
            MadeIn::Plain => {}
            MadeIn::Group(group) => {
                if !self.groups.iter().any(|g| &g.id == group && g.is_default) {
                    node.lives_in = group.clone();
                }
            }
            MadeIn::Project(project) => node.lives_with = project.clone(),
        }
        if let Some(name) = name.filter(|name| !name.is_empty()) {
            node.label = name.to_owned();
        }
        self.nodes.push(node);
        self.selected = Some(id);
    }

    /// `uishell_sidebar_close_kind`'s rule for a live row
    /// (uishell_sidebar.c:2887): destroy for an ended subject or a
    /// subjectless workspace, else detach.
    pub fn close_kind(&self, id: WorkspaceId) -> Option<CloseKind> {
        let node = self.nodes.iter().find(|node| node.id == id && !node.detached)?;
        Some(match node.subject {
            Some(subject) if !self.ended[subject] => CloseKind::Detach,
            _ => CloseKind::Destroy,
        })
    }

    /// The `close_workspace`/`detach_workspace` branch of
    /// `uishell_dispatch_window_command` (shell_commands.c:1527-1606). Its
    /// window-backed legacy workspace isn't modelled.
    pub fn close(&mut self, id: WorkspaceId, detach: bool, fresh: WorkspaceId) -> Close {
        let open = self.open();
        let at = open.iter().position(|w| *w == id).expect("closing an open workspace");
        // :1560-1564
        let mut next = self.selected;
        if next.is_none() || next == Some(id) {
            next = open.get(at + 1).or_else(|| at.checked_sub(1).and_then(|before| open.get(before))).copied();
        }
        let node = self.node(id).clone();
        // :1581-1586, with uishell_workspace_cfg_has_subject and
        // uishell_sidebar_workspace_subject_ended.
        let kept = detach && node.subject.is_some_and(|subject| !self.ended[subject]);
        if kept {
            self.node(id).detached = true;
        } else {
            self.nodes.retain(|node| node.id != id);
        }
        match next {
            // :1593-1598
            None => self.new_workspace(fresh, None, &MadeIn::Plain),
            Some(next) => self.selected = Some(next),
        }
        Close { kept, next }
    }

    /// The MATERIALIZE effect's handler (uishell_sidebar.c:2162-2181): the
    /// subject's node, open or detached, re-equipped as `workspace`; else a
    /// new one at the end, labelled with the effect's name. Either is
    /// selected. Returns the workspace.
    pub fn materialize(&mut self, subject: usize, label: &str, fresh: WorkspaceId) -> WorkspaceId {
        let id = match self.nodes.iter_mut().find(|node| node.subject == Some(subject)) {
            Some(node) => {
                node.detached = false;
                node.id
            }
            None => {
                self.nodes.push(Node {
                    id: fresh,
                    detached: false,
                    label: label.to_owned(),
                    subject: Some(subject),
                    lives_in: String::new(),
                    lives_with: String::new(),
                });
                fresh
            }
        };
        self.selected = Some(id);
        id
    }

    /// A row dropped on a group (uishell_detached_cards.c:889-895): one home,
    /// that group.
    pub fn drop_on_group(&mut self, id: WorkspaceId, group: &str) {
        let node = self.node(id);
        node.lives_with.clear();
        node.lives_in = group.to_owned();
    }

    /// A row dropped on a project, or on the Workspaces group (empty), by
    /// `uishell_sidebar_home_claim` (:937-945): `lives_with` cleared, then
    /// set again for a project; `lives_in` as it was.
    pub fn drop_on_home(&mut self, id: WorkspaceId, project: &str) {
        self.node(id).lives_with = project.to_owned();
    }

    /// `uishell_sidebar_local_delete_group` (uishell_local_groups.c:171): its
    /// workspaces, open or detached, lose `lives_in`; the default group
    /// can't be deleted.
    pub fn delete_group(&mut self, group: &str) {
        if self.groups.iter().any(|g| g.id == group && g.is_default) {
            return;
        }
        for node in &mut self.nodes {
            if node.lives_in == group {
                node.lives_in.clear();
            }
        }
        self.groups.retain(|g| g.id != group);
    }

    /// The home `uishell_sidebar_publish_workspaces` resolves (:1813): the
    /// project, else the group if it still exists, else the default group.
    pub fn effective_home(&self, id: WorkspaceId) -> Option<EffectiveHome> {
        let node = self.nodes.iter().find(|node| node.id == id)?;
        if node.subject.is_some() {
            return None;
        }
        if !node.lives_with.is_empty() {
            return Some(EffectiveHome::Project(node.lives_with.clone()));
        }
        let default = self.groups.iter().filter(|g| g.is_default).next_back().map_or("workspaces", |g| g.id.as_str());
        let group = if self.groups.iter().any(|g| g.id == node.lives_in) { node.lives_in.as_str() } else { default };
        Some(EffectiveHome::Group(group.to_owned()))
    }
}
