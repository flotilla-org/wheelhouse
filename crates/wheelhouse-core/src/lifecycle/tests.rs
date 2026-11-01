//! A workspace's lifecycle and home (#631), as Hegel properties over
//! generated window histories: a host that carries out the core's effects as
//! native does, checked after every step against the lifecycle's invariants
//! and against a transcription of the C rules it replaces
//! (lifecycle/reference.rs, until that goes).
use std::collections::{BTreeMap, BTreeSet};

use hegel::generators as gs;
use hegel::TestCase;

use super::reference::{self, MadeIn};
use super::*;
use crate::presentation::{PlacementNode, PresentationState};
use crate::scaling_tests::{patch, text, CONFIG, PROVIDER};
use crate::sidebar::{HostEffect, Intent, IntentReceipt, Sidebar, Workspace};
use crate::versioned::Clock;

const SUBJECTS: usize = 3;
const PROJECTS: [&str; 2] = ["p", "q"];

fn subject(s: usize) -> EntityRef {
    EntityRef::new(PROVIDER, "vessel", format!("v{s}"))
}

fn subject_label(s: usize) -> String {
    format!("Vessel {s}")
}

fn local(kind: &str, id: &str) -> EntityRef {
    EntityRef::local(kind, id)
}

/// A group's facts: in section `main`, at `position`, maybe the default.
fn group_facts(position: usize, default: bool) -> BTreeMap<String, MetadataValue> {
    let mut facts = BTreeMap::from([
        (system::SECTION.to_owned(), MetadataValue::EntityRefs(vec![local(system::SECTION, "main")])),
        (".position".to_owned(), text(position.to_string())),
    ]);
    if default {
        facts.insert(system::DEFAULT.to_owned(), MetadataValue::Bool(true));
    }
    facts
}

/// A workspace node of the host's window, as native keeps it: open or kept,
/// its label, and the subject it was opened for.
#[derive(Clone, Debug)]
struct HostNode {
    id: WorkspaceId,
    kept: bool,
    label: String,
    subject: Option<usize>,
}

/// A host carrying out the core's effects on its own window, as native
/// does, and the reference window given the same gestures.
struct World {
    sidebar: Sidebar,
    nodes: Vec<HostNode>,
    shown: Option<WorkspaceId>,
    next_id: u64,
    now: u64,
    groups: Vec<String>,
    /// The subjects the host's provider ended.
    ended: BTreeSet<usize>,
    reference: Option<reference::Window>,
}

impl World {
    fn new(with_reference: bool) -> Self {
        let mut sidebar = Sidebar::new(CONFIG).unwrap();
        let mut facts = vec![patch("project", "p", vec![("flotilla.project".into(), text("p")), ("display.label".into(), text("P"))])];
        for s in 0..SUBJECTS {
            facts.push(patch(
                "vessel",
                &format!("v{s}"),
                vec![
                    ("flotilla.project".into(), text("p")),
                    ("display.label".into(), text(subject_label(s))),
                    ("action.primary.recipe".into(), text("true")),
                    ("flotilla.convoy.phase".into(), text("active")),
                    ("status.state".into(), text("waiting")),
                ],
            ));
        }
        sidebar.apply(1, PROVIDER, facts).unwrap();
        // The default group native makes (uishell_sidebar_local_ensure_default).
        sidebar.set_local(local(system::SECTION, "main"), BTreeMap::from([(".position".into(), text("0"))])).unwrap();
        sidebar.set_local(local(system::GROUP, DEFAULT_GROUP), group_facts(0, true)).unwrap();
        let reference = with_reference.then(|| reference::Window {
            groups: vec![reference::Group { id: DEFAULT_GROUP.into(), is_default: true }],
            ended: vec![false; SUBJECTS],
            ..Default::default()
        });
        let mut world = Self {
            sidebar,
            nodes: vec![],
            shown: None,
            next_id: 1,
            now: 1,
            groups: vec![DEFAULT_GROUP.into()],
            ended: BTreeSet::new(),
            reference,
        };
        // A window opens on a fresh workspace.
        let id = world.fresh();
        world.new_workspace(id, None, Home::default(), &MadeIn::Plain);
        world
    }

    fn fresh(&mut self) -> WorkspaceId {
        self.next_id += 1;
        let mut bytes = [0; 16];
        bytes[8..].copy_from_slice(&self.next_id.to_be_bytes());
        WorkspaceId::from_bytes(bytes)
    }

    fn open(&self) -> Vec<WorkspaceId> {
        self.nodes.iter().filter(|node| !node.kept).map(|node| node.id).collect()
    }

    /// What native's next frame does: observe the window's open workspaces.
    fn observe(&mut self) {
        let open: Vec<&HostNode> = self.nodes.iter().filter(|node| !node.kept).collect();
        let workspaces = open
            .iter()
            .enumerate()
            .map(|(position, node)| Workspace { id: node.id, position, name: node.label.clone(), selected: Some(node.id) == self.shown })
            .collect();
        self.sidebar.observe(workspaces, vec![]);
    }

    /// Native's one entry point: observe, apply the intent, carry out its
    /// effects. Returns them, or the refusal.
    fn intend(&mut self, intent: Intent) -> Result<Vec<HostEffect>, String> {
        self.observe();
        let revision = self.sidebar.revision();
        let generation = self.sidebar.life_generation();
        match self.sidebar.apply_intent(intent) {
            IntentReceipt::Accepted { effects, .. } => {
                self.carry_out(&effects);
                self.observe();
                Ok(effects)
            }
            IntentReceipt::Refused { reason } => {
                assert_eq!(self.sidebar.revision(), revision, "a refused intent ({reason}) moved the revision");
                assert_eq!(self.sidebar.life_generation(), generation, "a refused intent ({reason}) changed a life");
                Err(reason)
            }
        }
    }

    fn carry_out(&mut self, effects: &[HostEffect]) {
        for effect in effects {
            match effect {
                HostEffect::OpenWorkspace { workspace, name } => {
                    self.nodes.push(HostNode { id: *workspace, kept: false, label: name.clone(), subject: None });
                    self.shown = Some(*workspace);
                }
                HostEffect::CloseWorkspace { workspace, kept } => {
                    if *kept {
                        self.nodes.iter_mut().find(|node| node.id == *workspace).unwrap().kept = true;
                    } else {
                        self.nodes.retain(|node| node.id != *workspace);
                    }
                }
                HostEffect::ShowWorkspace { workspace } => self.shown = Some(*workspace),
                other => panic!("a lifecycle intent returned {other:?}"),
            }
        }
    }

    fn new_workspace(&mut self, id: WorkspaceId, name: Option<String>, home: Home, made_in: &MadeIn) {
        let effects = self.intend(Intent::NewWorkspace { workspace: id, name: name.clone(), home }).unwrap();
        assert!(matches!(&effects[..], [HostEffect::OpenWorkspace { workspace, .. }] if *workspace == id), "{effects:?}");
        if let Some(reference) = &mut self.reference {
            reference.new_workspace(id, name.as_deref(), made_in);
        }
    }

    /// Close `id`, keeping it if `keep` allows; the last close leaves a fresh
    /// workspace, which native asks for.
    fn close(&mut self, id: WorkspaceId, keep: bool) {
        let kind = self.close_role(id);
        let subject = self.node(id).and_then(|node| node.subject);
        let effects = self.intend(Intent::CloseWorkspace { workspace: id, keep }).unwrap();
        let kept = matches!(effects.first(), Some(HostEffect::CloseWorkspace { workspace, kept: true }) if *workspace == id);
        let next = match &effects[1..] {
            [] => None,
            [HostEffect::ShowWorkspace { workspace }] => Some(*workspace),
            other => panic!("close returned {other:?}"),
        };
        // A row offers detach exactly when close-workspace with keep would
        // keep its workspace.
        if keep {
            assert_eq!(kind == Some(CloseKind::Detach), kept, "{id} offered {kind:?}");
        }
        // A close keeps the workspace exactly when asked to, it has a subject
        // and the subject hasn't ended; a destroyed one's record goes.
        assert_eq!(kept, keep && subject.is_some_and(|s| !self.ended.contains(&s)), "closing {id} of {subject:?}");
        assert_eq!(self.sidebar.registered_workspaces().contains(&id), kept);
        let fresh = self.fresh();
        if let Some(reference) = &mut self.reference {
            let closed = reference.close(id, keep, fresh);
            assert_eq!((closed.kept, closed.next), (kept, next), "close of {id}: the reference's, then the core's");
        }
        if next.is_none() {
            assert!(self.open().is_empty(), "no next only after the last close");
            // The reference's close made it already, as the C does.
            let reference = self.reference.take();
            self.new_workspace(fresh, None, Home::default(), &MadeIn::Plain);
            self.reference = reference;
        }
    }

    /// The close role of every live row bound to `id`, which must agree.
    fn close_role(&self, id: WorkspaceId) -> Option<CloseKind> {
        let roles: BTreeSet<Option<CloseKind>> = live_rows(&self.sidebar).into_iter().filter(|(w, _)| *w == id).map(|(_, c)| c).collect();
        assert!(roles.len() <= 1, "rows of {id} differ: {roles:?}");
        roles.into_iter().next().flatten()
    }

    /// Activate a subject: focus its open workspace, or materialize it,
    /// reopening its Kept Workspace.
    fn activate(&mut self, s: usize) {
        self.observe();
        let kept_before =
            self.sidebar.registered_workspaces().into_iter().find(|id| self.node(*id).is_some_and(|n| n.kept && n.subject == Some(s)));
        let effects = self.sidebar.dispatch(Intent::Activate { entity: subject(s) }).unwrap();
        let fresh = self.fresh();
        let opened = match &effects[..] {
            [HostEffect::Focus { request_id, workspace_id }] => {
                self.sidebar.complete(*request_id, Ok(None));
                *workspace_id
            }
            [HostEffect::Materialize { request_id, name, kept, .. }] => {
                // Activating a subject with a Kept Workspace reopens it, with
                // the same ID.
                assert_eq!(*kept, kept_before, "the materialize names the Kept Workspace");
                let id = match kept {
                    Some(kept) => {
                        self.nodes.iter_mut().find(|node| node.id == *kept).unwrap().kept = false;
                        *kept
                    }
                    None => {
                        self.nodes.push(HostNode { id: fresh, kept: false, label: name.clone(), subject: Some(s) });
                        fresh
                    }
                };
                self.sidebar.complete(*request_id, Ok(Some(id)));
                id
            }
            other => panic!("activating {s} gave {other:?}"),
        };
        self.shown = Some(opened);
        self.observe();
        if let Some(reference) = &mut self.reference {
            assert_eq!(reference.materialize(s, &subject_label(s), fresh), opened, "activating {s}");
        }
    }

    fn end(&mut self, s: usize) {
        self.now += 1;
        self.sidebar
            .apply(self.now, PROVIDER, [patch("vessel", &format!("v{s}"), vec![("flotilla.convoy.phase".into(), text("landed"))])])
            .unwrap();
        self.ended.insert(s);
        if let Some(reference) = &mut self.reference {
            reference.ended[s] = true;
        }
    }

    fn node(&self, id: WorkspaceId) -> Option<&HostNode> {
        self.nodes.iter().find(|node| node.id == id)
    }

    /// One step of a window's history.
    fn step(&mut self, tc: &TestCase) {
        let open = self.open();
        let pick = |len: usize| tc.draw(gs::integers::<usize>().max_value(len.max(1) - 1));
        let locals: Vec<WorkspaceId> = open.iter().copied().filter(|id| self.node(*id).unwrap().subject.is_none()).collect();
        match tc.draw(gs::integers::<u8>().max_value(9)) {
            0 => {
                // New, with or without a name, in the default group, another
                // group or a project.
                let id = self.fresh();
                let name = tc.draw(gs::booleans()).then(|| ["Build logs", "Workspace 2", "Notes"][pick(3)].to_owned());
                let (home, made_in) = match tc.draw(gs::integers::<u8>().max_value(2)) {
                    0 => (Home::default(), MadeIn::Plain),
                    1 => {
                        let group = self.groups[pick(self.groups.len())].clone();
                        (Home { group: Some(group.clone()), project: None }, MadeIn::Group(group))
                    }
                    _ => {
                        let project = PROJECTS[pick(2)].to_owned();
                        (Home { group: None, project: Some(project.clone()) }, MadeIn::Project(project))
                    }
                };
                self.new_workspace(id, name, home, &made_in);
            }
            1 => {
                // The host's own gestures: reorder the open workspaces (kept
                // ones keep their places) and select one, or none.
                let mut order = open.clone();
                let by = pick(order.len());
                order.rotate_left(by);
                let mut slots = order.into_iter();
                let mut nodes = self.nodes.clone();
                for node in nodes.iter_mut().filter(|node| !node.kept) {
                    let id = slots.next().unwrap();
                    *node = self.node(id).unwrap().clone();
                }
                self.nodes = nodes;
                self.shown = tc.draw(gs::weighted_booleans(0.9)).then(|| open[pick(open.len())]);
                if let Some(reference) = &mut self.reference {
                    let mut by_id: BTreeMap<_, _> = reference.nodes.iter().map(|node| (node.id, node.clone())).collect();
                    for (node, host) in reference.nodes.iter_mut().zip(&self.nodes) {
                        *node = by_id.remove(&host.id).unwrap();
                    }
                    reference.selected = self.shown;
                }
                self.observe();
            }
            2 | 3 => {
                // Close or detach the shown workspace or another.
                let id = match self.shown.filter(|_| tc.draw(gs::booleans())) {
                    Some(shown) => shown,
                    None => open[pick(open.len())],
                };
                self.close(id, tc.draw(gs::booleans()));
            }
            4 => self.end(pick(SUBJECTS)),
            5 | 6 => self.activate(pick(SUBJECTS)),
            7 => {
                // Re-home a local workspace by each edit.
                let Some(&id) = locals.get(pick(locals.len())) else { return };
                let edit = match tc.draw(gs::integers::<u8>().max_value(2)) {
                    0 => HomeEdit::Group(self.groups[pick(self.groups.len())].clone()),
                    1 => HomeEdit::Project(Some(PROJECTS[pick(2)].to_owned())),
                    _ => HomeEdit::Project(None),
                };
                assert_eq!(self.intend(Intent::SetWorkspaceHome { workspace: id, edit: edit.clone() }), Ok(vec![]));
                if let Some(reference) = &mut self.reference {
                    match &edit {
                        HomeEdit::Group(group) => reference.drop_on_group(id, group),
                        HomeEdit::Project(project) => reference.drop_on_home(id, project.as_deref().unwrap_or("")),
                    }
                }
            }
            8 => {
                let id = format!("g{}", self.next_id);
                self.next_id += 1;
                self.sidebar.set_local(local(system::GROUP, &id), group_facts(self.groups.len(), false)).unwrap();
                self.groups.push(id.clone());
                if let Some(reference) = &mut self.reference {
                    reference.groups.push(reference::Group { id, is_default: false });
                }
            }
            _ => {
                // Remove a group other than the default.
                if self.groups.len() < 2 {
                    return;
                }
                let group = self.groups.remove(1 + pick(self.groups.len() - 1));
                let homed: BTreeSet<WorkspaceId> =
                    self.registered().filter(|id| self.life(*id).0.home.group.as_deref() == Some(group.as_str())).collect();
                let before: BTreeMap<WorkspaceId, _> = self.registered().map(|id| (id, self.life(id))).collect();
                assert!(self.sidebar.remove_local(&local(system::GROUP, &group)));
                // Removing a group moves exactly its workspaces, open and
                // kept, to the default group.
                for (id, (life, home)) in before {
                    let (after, home_after) = self.life(id);
                    if homed.contains(&id) {
                        assert_eq!(after.home.group, None);
                        assert_eq!(after.home.project, life.home.project);
                        if life.home.project.is_none() {
                            assert_eq!(home_after, Some(EffectiveHome::Group(DEFAULT_GROUP.into())));
                        }
                    } else {
                        assert_eq!((after, home_after), (life, home), "{id} wasn't in {group}");
                    }
                }
                if let Some(reference) = &mut self.reference {
                    reference.delete_group(&group);
                }
            }
        }
    }

    fn registered(&self) -> impl Iterator<Item = WorkspaceId> {
        self.sidebar.registered_workspaces().into_iter()
    }

    fn life(&self, id: WorkspaceId) -> (Life, Option<EffectiveHome>) {
        self.sidebar.workspace_life(id).unwrap()
    }

    /// The name the core gives a workspace: the user's, else its subject's,
    /// else the host's.
    fn name(&self, id: WorkspaceId) -> String {
        let overlay = self.sidebar.overlay(id).unwrap();
        overlay.name.or(overlay.subject_name).unwrap_or_else(|| self.node(id).unwrap().label.clone())
    }

    /// What holds after every step.
    fn check(&self) {
        let registered: BTreeSet<WorkspaceId> = self.registered().collect();
        let nodes: BTreeSet<WorkspaceId> = self.nodes.iter().map(|node| node.id).collect();
        assert_eq!(registered, nodes, "the core keeps exactly the window's workspaces, open and kept");
        let mut subjects = BTreeSet::new();
        for node in &self.nodes {
            let (life, home) = self.life(node.id);
            assert_eq!(life.kept, Some(node.kept), "{} is kept exactly when its node is", node.id);
            // A Dashboard has at most one workspace per subject, open or kept.
            if let Some(s) = node.subject {
                assert!(subjects.insert(s), "two workspaces of subject {s}");
            }
            // Every local workspace has exactly one effective home, and a
            // subject workspace has none.
            assert_eq!(home.is_some(), node.subject.is_none(), "{}'s home {home:?}", node.id);
            if let Some(EffectiveHome::Group(group)) = &home {
                assert!(self.groups.contains(group), "{} lives in {group}, which is gone", node.id);
            }
        }
        // Names: a host label is the core's name for it.
        for node in &self.nodes {
            assert_eq!(self.name(node.id), node.label, "{}'s name", node.id);
        }
        // Each open workspace's rows offer one close role.
        let rows = live_rows(&self.sidebar);
        for id in self.open() {
            assert!(rows.iter().any(|(w, _)| *w == id), "{id} has no live row");
            self.close_role(id);
        }
        if let Some(reference) = &self.reference {
            self.check_reference(reference);
        }
    }

    /// The core and the reference agree.
    fn check_reference(&self, reference: &reference::Window) {
        assert_eq!(self.open(), reference.open(), "open workspaces in order");
        assert_eq!(self.shown, reference.selected, "the shown workspace");
        for (node, theirs) in self.nodes.iter().zip(&reference.nodes) {
            assert_eq!((node.id, node.kept, &node.label), (theirs.id, theirs.detached, &theirs.label), "a node");
            let (life, home) = self.life(node.id);
            let some = |text: &str| (!text.is_empty()).then(|| text.to_owned());
            assert_eq!(life.home, Home { group: some(&theirs.lives_in), project: some(&theirs.lives_with) }, "{}'s home", node.id);
            assert_eq!(home, reference.effective_home(node.id), "{}'s effective home", node.id);
            if !node.kept {
                assert_eq!(self.close_role(node.id), reference.close_kind(node.id), "{}'s close role", node.id);
            }
        }
        assert_eq!(self.nodes.len(), reference.nodes.len());
    }
}

/// Every live row's workspace and close role.
fn live_rows(sidebar: &Sidebar) -> Vec<(WorkspaceId, Option<CloseKind>)> {
    fn add(nodes: &[std::rc::Rc<PlacementNode>], out: &mut Vec<(WorkspaceId, Option<CloseKind>)>) {
        for node in nodes {
            if let PresentationState::Live { workspace_id, .. } = node.state {
                out.push((workspace_id, node.close));
            }
            add(&node.children, out);
        }
    }
    let mut out = vec![];
    for section in &sidebar.snapshot_shared().surface.sections {
        add(&section.nodes, &mut out);
    }
    out
}

fn history(tc: TestCase, with_reference: bool) {
    let mut world = World::new(with_reference);
    world.check();
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(30)) {
        world.step(&tc);
        world.check();
    }
}

/// Equivalence with the C rules: after every step of any window history the
/// core and the reference agree on the open workspaces and their order, the
/// shown one, which are kept, every name, every home and effective home,
/// each row's close role, and each close's effects.
#[hegel::test(test_cases = 300)]
fn histories_match_the_native_rules(tc: TestCase) {
    history(tc, true);
}

/// The lifecycle's invariants, which stay once the reference goes (World::check
/// and World::close): one effective home per local workspace, at most one
/// workspace per subject, a close keeping exactly what it may and the row
/// offering detach exactly then, a destroyed record gone, removing a group
/// moving exactly its workspaces, and every name the host shows the core's.
#[hegel::test(test_cases = 300)]
fn histories_keep_the_lifecycle_invariants(tc: TestCase) {
    history(tc, false);
}

/// A new workspace's default name is unused by every open and kept
/// workspace, whatever names are in use (with gaps, duplicates and others).
#[hegel::test(test_cases = 300)]
fn default_names_are_unused(tc: TestCase) {
    let names: Vec<String> = tc
        .draw(gs::vecs(gs::integers::<u8>().max_value(12)).max_size(12))
        .into_iter()
        .map(|n| match n {
            0 => "Notes".to_owned(),
            n => format!("Workspace {n}"),
        })
        .collect();
    let name = default_name(names.iter().map(String::as_str));
    assert!(!names.contains(&name), "{name} is in use");
    // The lowest: every lower number is in use.
    let n: u64 = name.strip_prefix("Workspace ").unwrap().parse().unwrap();
    assert!((1..n).all(|lower| names.contains(&format!("Workspace {lower}"))), "{name} isn't the lowest free");
}

/// Closing a workspace that isn't shown leaves the shown one; closing the
/// shown one shows the next, else the previous; the last close shows none.
#[hegel::test(test_cases = 300)]
fn the_next_shown_follows_the_close(tc: TestCase) {
    let count = tc.draw(gs::integers::<u64>().min_value(1).max_value(5));
    let order: Vec<WorkspaceId> = (1..=count).map(crate::tab_workspace_id).collect();
    let pick = || order[tc.draw(gs::integers::<usize>().max_value(order.len() - 1))];
    let closing = pick();
    let shown = tc.draw(gs::booleans()).then(pick);
    let at = order.iter().position(|id| *id == closing).unwrap();
    let expected = match shown {
        Some(shown) if shown != closing => Some(shown),
        _ if at + 1 < order.len() => Some(order[at + 1]),
        _ if at > 0 => Some(order[at - 1]),
        _ => None,
    };
    assert_eq!(next_shown(&order, shown, closing), expected);
    assert_eq!(expected.is_none(), order.len() == 1);
}

/// A row offers detach exactly when closing with keep would keep its
/// workspace, and a close keeps it exactly when asked, with a subject that
/// hasn't ended.
#[hegel::test(test_cases = 100)]
fn the_close_role_matches_the_close(tc: TestCase) {
    let mut record = WorkspaceRecord::default();
    let subject = tc.draw(gs::booleans());
    if subject {
        record.follow_subject(&mut Clock::default(), crate::workspace::Subject { entity: Some(self::subject(0)), ..Default::default() });
    }
    let (keep, ended) = (tc.draw(gs::booleans()), tc.draw(gs::booleans()));
    let kept = close_outcome(&record, keep, ended) == Closed::Kept;
    assert_eq!(kept, keep && subject && !ended);
    let role = close_kind(&local(system::WORKSPACE, "w"), Some(LiveWorkspace { subject, subject_ended: ended }));
    assert_eq!(role == Some(CloseKind::Detach), close_outcome(&record, true, ended) == Closed::Kept);
    assert_eq!(close_kind(&local(system::REF, "r"), None), Some(CloseKind::RemovePin));
    assert_eq!(close_kind(&self::subject(0), None), None, "a row not bound to an open workspace offers nothing");
}

/// `Life` survives a record's encode and decode, and a record written before
/// it existed decodes with `kept: None`.
#[hegel::test(test_cases = 100)]
fn life_survives_its_record(tc: TestCase) {
    let mut clock = Clock::default();
    let mut record = WorkspaceRecord::default();
    let id = |tc: &TestCase| tc.draw(gs::sampled_from(vec![None, Some(String::new()), Some("g1".to_owned()), Some("p".to_owned())]));
    let life =
        Life { kept: tc.draw(gs::sampled_from(vec![None, Some(false), Some(true)])), home: Home { group: id(&tc), project: id(&tc) } };
    record.life.follow(&mut clock, |current| *current = life.clone());
    let read = WorkspaceRecord::decode(&record.encode(), &mut clock).unwrap();
    assert_eq!(read.life.get(), &life);
    assert!(read.generation() > record.generation());
    // A record main wrote has no life.
    let mut old: serde_json::Value = serde_json::from_str(&record.encode()).unwrap();
    old.as_object_mut().unwrap().remove("life");
    let read = WorkspaceRecord::decode(&old.to_string(), &mut clock).unwrap();
    assert_eq!(read.life.get().kept, None);
}

/// A record with no life is seeded once from the host, and then is the
/// core's: a second seed changes nothing.
#[test]
fn a_life_is_seeded_once() {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    let id = crate::tab_workspace_id(1);
    sidebar.register_workspace(id);
    assert_eq!(sidebar.workspace_life(id).unwrap().0.kept, None);
    let home = Home { group: Some("g".into()), project: Some(String::new()) };
    assert!(sidebar.seed_life(id, false, home).unwrap());
    let seeded = sidebar.workspace_life(id).unwrap().0;
    assert_eq!(seeded, Life { kept: Some(false), home: Home { group: Some("g".into()), project: None } });
    assert!(!sidebar.seed_life(id, true, Home::default()).unwrap());
    assert_eq!(sidebar.workspace_life(id).unwrap().0, seeded);
}

/// A row dropped on a project, then back on the Workspaces group, goes back
/// to the group it lived in before the project; a drop on a group clears
/// the project.
#[test]
fn a_drop_back_on_workspaces_returns_to_the_group() {
    let mut home = Home { group: Some("builds".into()), project: None };
    home.edit(HomeEdit::Project(Some("p".into())));
    assert_eq!(home, Home { group: Some("builds".into()), project: Some("p".into()) });
    home.edit(HomeEdit::Project(None));
    assert_eq!(home, Home { group: Some("builds".into()), project: None });
    home.edit(HomeEdit::Project(Some("p".into())));
    home.edit(HomeEdit::Group("tests".into()));
    assert_eq!(home, Home { group: Some("tests".into()), project: None });
}
