//! A workspace's lifecycle and home (#631): the rules that open, close, keep
//! and home a workspace, as functions over the core's own state. The sidebar
//! applies its lifecycle intents with them (`new-workspace`,
//! `close-workspace`, `set-workspace-home`), and computes each row's close
//! role with [`close_kind`].
//!
//! A workspace closed and kept is a Kept Workspace (CONTEXT.md; native's
//! `detached_workspace`): it stays registered, with its record, and its
//! subject's next activation reopens it with the same Workspace ID. A local
//! workspace's Workspace Home is the local group or project it lives in.
use std::collections::{BTreeMap, BTreeSet};

use serde::{Deserialize, Serialize};

use crate::{presentation::system, workspace::WorkspaceRecord, EntityRef, MetadataValue, WorkspaceId};

/// The lifecycle part of a workspace's record.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Life {
    /// Closed and kept (a Kept Workspace). None in a record written before
    /// this part existed, or for a workspace the host registered itself:
    /// the host seeds it once from what it has ([`Sidebar::seed_life`](crate::Sidebar::seed_life)).
    #[serde(default)]
    pub kept: Option<bool>,
    #[serde(default)]
    pub home: Home,
}

/// Where a local workspace lives. Two fields, as native's `lives_in` and
/// `lives_with` were, because a drop on a project or the Workspaces group
/// leaves `group` as it was: dropped back on the Workspaces group, it goes
/// back to the group it was in before the project.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Home {
    /// A local group's ID; none for the default group.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub group: Option<String>,
    /// A project's ID (its `flotilla.project`), which wins over the group.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub project: Option<String>,
}

/// What a sidebar reads: one home.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "home", content = "id", rename_all = "kebab-case")]
pub enum EffectiveHome {
    Project(String),
    Group(String),
}

/// An edit of a workspace's home, as a row's drop makes it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "home", content = "id", rename_all = "kebab-case")]
pub enum HomeEdit {
    /// Dropped on a group: it lives there, with no project.
    Group(String),
    /// Dropped on a project (`Some`), or on the Workspaces group (`None`):
    /// the project is set or cleared, and the group left as it was.
    Project(Option<String>),
}

/// What a row's close control does.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum CloseKind {
    /// Close and keep: the row stays and reopens the same layout.
    Detach,
    /// Close and destroy: the workspace can't come back.
    Destroy,
    /// A pin's: remove the pin, never its source.
    RemovePin,
}

/// What a close did to the workspace.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Closed {
    Kept,
    Destroyed,
}

/// The ID native gives the default group it makes when no group is marked
/// default (`uishell_sidebar_default_local_id`).
pub const DEFAULT_GROUP: &str = "workspaces";

/// The local groups a home can name, as native reads them
/// (`uishell_sidebar_locals`): a group exists when its section does, and
/// the default is the last group marked `.default` in section and group
/// order, else [`DEFAULT_GROUP`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Groups {
    existing: BTreeSet<String>,
    default: String,
}

impl Groups {
    pub fn of(locals: &BTreeMap<EntityRef, BTreeMap<String, MetadataValue>>) -> Self {
        // Positions are text, as native writes them; a missing one sorts last.
        let position = |facts: &BTreeMap<String, MetadataValue>| match facts.get(".position") {
            Some(MetadataValue::Text(text)) => text.parse::<u64>().unwrap_or(u64::MAX),
            _ => u64::MAX,
        };
        let refers = |facts: &BTreeMap<String, MetadataValue>, key: &str| match facts.get(key) {
            Some(MetadataValue::EntityRefs(refs)) => refs.first().map(|entity| entity.id.clone()),
            _ => None,
        };
        // A stable sort keeps the map's order among equal positions.
        let mut sections: Vec<_> = locals.iter().filter(|(entity, _)| entity.kind == system::SECTION).collect();
        sections.sort_by_key(|(_, facts)| position(facts));
        let mut groups: Vec<_> = locals.iter().filter(|(entity, _)| entity.kind == system::GROUP).collect();
        groups.sort_by_key(|(_, facts)| position(facts));
        let mut existing = BTreeSet::new();
        let mut default = DEFAULT_GROUP.to_owned();
        for (section, _) in &sections {
            for (group, facts) in &groups {
                if refers(facts, system::SECTION).as_deref() != Some(section.id.as_str()) {
                    continue;
                }
                existing.insert(group.id.clone());
                if matches!(facts.get(system::DEFAULT), Some(MetadataValue::Bool(true))) {
                    default = group.id.clone();
                }
            }
        }
        Self { existing, default }
    }

    pub fn exists(&self, id: &str) -> bool {
        self.existing.contains(id)
    }

    /// The group a workspace whose group is gone lives in.
    pub fn default_group(&self) -> &str {
        &self.default
    }
}

/// The one home a local workspace has: its project, else its group while
/// that exists, else the default group. None for a subject workspace.
pub fn effective_home(record: &WorkspaceRecord, groups: &Groups) -> Option<EffectiveHome> {
    if record.subject().is_some() {
        return None;
    }
    let home = &record.life.get().home;
    Some(match (&home.project, &home.group) {
        (Some(project), _) => EffectiveHome::Project(project.clone()),
        (None, Some(group)) if groups.exists(group) => EffectiveHome::Group(group.clone()),
        _ => EffectiveHome::Group(groups.default_group().to_owned()),
    })
}

/// A new workspace's default name: "Workspace N" with the lowest N no
/// workspace, open or kept, uses, so two never share one.
pub fn default_name<'a>(names_in_use: impl IntoIterator<Item = &'a str>) -> String {
    let used: BTreeSet<&str> = names_in_use.into_iter().collect();
    (1u64..).map(|n| format!("Workspace {n}")).find(|name| !used.contains(name.as_str())).expect("a free number")
}

/// The workspace shown after `closing` closes, over the open workspaces in
/// `order`: the one shown now unless that is the one closing, then the
/// next, else the previous; none when it was the last open.
pub fn next_shown(order: &[WorkspaceId], shown: Option<WorkspaceId>, closing: WorkspaceId) -> Option<WorkspaceId> {
    if let Some(shown) = shown.filter(|shown| *shown != closing) {
        return Some(shown);
    }
    let at = order.iter().position(|id| *id == closing)?;
    order.get(at + 1).or_else(|| at.checked_sub(1).and_then(|before| order.get(before))).copied()
}

/// Whether closing with `keep` keeps a workspace: it has a subject, which
/// hasn't ended, so its row can reopen it.
fn keeps(keep: bool, subject: bool, subject_ended: bool) -> bool {
    keep && subject && !subject_ended
}

/// What closing a workspace does: keeps it when `keep`, it has a subject,
/// and the subject hasn't ended; else destroys it.
pub fn close_outcome(record: &WorkspaceRecord, keep: bool, subject_ended: bool) -> Closed {
    match keeps(keep, record.subject().is_some(), subject_ended) {
        true => Closed::Kept,
        false => Closed::Destroyed,
    }
}

/// The open workspace a live row stands for, as its close role reads it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LiveWorkspace {
    pub subject: bool,
    pub subject_ended: bool,
}

/// What a row's close control offers: a pin removes itself; a row bound to
/// an open workspace detaches it when closing would keep it, else destroys
/// it; any other row offers nothing.
pub fn close_kind(entity: &EntityRef, live: Option<LiveWorkspace>) -> Option<CloseKind> {
    if entity.kind == system::REF {
        return Some(CloseKind::RemovePin);
    }
    let live = live?;
    Some(match keeps(true, live.subject, live.subject_ended) {
        true => CloseKind::Detach,
        false => CloseKind::Destroy,
    })
}

impl Home {
    /// The same home with an empty ID read as none, as native's empty
    /// `lives_in` and `lives_with` were.
    pub fn normalized(self) -> Self {
        let some = |id: Option<String>| id.filter(|id| !id.is_empty());
        Self { group: some(self.group), project: some(self.project) }
    }

    /// Apply a home edit.
    pub fn edit(&mut self, edit: HomeEdit) {
        match edit {
            HomeEdit::Group(group) => {
                self.project = None;
                self.group = Some(group);
            }
            HomeEdit::Project(project) => self.project = project,
        }
    }
}

#[cfg(test)]
pub(crate) mod reference;
#[cfg(test)]
mod tests;
