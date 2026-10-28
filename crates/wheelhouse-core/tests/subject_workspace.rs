//! A workspace subject finds its workspace by the subject binding, not by the
//! action target it shares with other entities.
mod common;
use common::{entity, nodes, text, Feed, CONFIG};
use wheelhouse_core::presentation::PresentationState;
use wheelhouse_core::sidebar::{Action, HostEffect, Workspace};
use wheelhouse_core::{EntityRef, MetadataPatch, Sidebar, WorkspaceId};

const SHARED: &str = "shared-target";

fn ws(n: u64) -> WorkspaceId {
    wheelhouse_core::tab_workspace_id(n)
}

fn facts(subject: &EntityRef, target: Option<&str>) -> MetadataPatch {
    let mut f = vec![("flotilla.project", text("p")), ("display.label", text(&subject.id)), ("action.primary.recipe", text("printf hi"))];
    if let Some(target) = target {
        f.push(("action.primary.target", text(target)));
    }
    common::patch(subject.clone(), &f)
}

/// Sidebar with `subjects` each materialized into workspace 1, 2, ... in order.
fn session(subjects: &[EntityRef]) -> Sidebar {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.feed(100, [common::patch(entity("project", "p"), &[("flotilla.project", text("p")), ("display.label", text("P"))])]);
    let mut open = vec![];
    for (i, subject) in subjects.iter().enumerate() {
        sidebar.feed(100, [facts(subject, None)]);
        let effects = sidebar.dispatch(Action::Activate { entity: subject.clone() }).unwrap();
        let [HostEffect::Materialize { request_id, .. }] = effects[..] else { panic!("expected materialize: {effects:?}") };
        sidebar.complete(request_id, Ok(Some(ws(i as u64 + 1))));
        open.push(Workspace { id: ws(i as u64 + 1), position: i, name: subject.id.clone(), selected: i == 0 });
        sidebar.observe(open.clone(), vec![]);
    }
    sidebar
}

fn focus(sidebar: &mut Sidebar, entity: &EntityRef) -> WorkspaceId {
    let effects = sidebar.dispatch(Action::Activate { entity: entity.clone() }).unwrap();
    let [HostEffect::Focus { request_id, workspace_id }] = effects[..] else { panic!("expected focus: {effects:?}") };
    sidebar.complete(request_id, Ok(None));
    workspace_id
}

fn row_state(sidebar: &Sidebar, subject: &EntityRef) -> PresentationState {
    nodes(&sidebar.snapshot().surface).into_iter().find(|n| &n.entity == subject).unwrap().state.clone()
}

#[test]
fn subjects_sharing_a_target_resolve_their_own_workspace() {
    let (a, b) = (entity("vessel", "a"), entity("vessel", "b"));
    let mut sidebar = session(&[a.clone(), b.clone()]);
    sidebar.feed(200, [facts(&a, Some(SHARED)), facts(&b, Some(SHARED))]);
    for (subject, id) in [(&a, ws(1)), (&b, ws(2))] {
        assert_eq!(sidebar.detail_card(subject).unwrap().workspace_id, Some(id), "{subject:?} card");
        assert_eq!(focus(&mut sidebar, subject), id, "{subject:?} focus");
        let PresentationState::Live { workspace_id, .. } = row_state(&sidebar, subject) else { panic!("{subject:?} not open") };
        assert_eq!(workspace_id, id, "{subject:?} open state");
    }
}

#[test]
fn a_non_subject_finds_its_workspace_by_target() {
    let a = entity("vessel", "a");
    let other = entity("vessel", "other");
    let mut sidebar = session(&[a.clone()]);
    sidebar.feed(200, [facts(&a, Some(SHARED)), facts(&other, Some(SHARED))]);
    assert_eq!(sidebar.detail_card(&other).unwrap().workspace_id, Some(ws(1)));
    assert_eq!(focus(&mut sidebar, &other), ws(1));
}

#[test]
fn a_subject_without_a_workspace_falls_back_to_target() {
    let a = entity("vessel", "a");
    let b = entity("vessel", "b");
    let mut sidebar = session(&[a.clone()]);
    // b is a subject kind with no bound workspace.
    sidebar.feed(200, [facts(&a, Some(SHARED)), facts(&b, Some(SHARED))]);
    assert_eq!(sidebar.detail_card(&b).unwrap().workspace_id, Some(ws(1)));
    assert_eq!(focus(&mut sidebar, &b), ws(1));
}

#[test]
fn a_subject_open_in_two_workspaces_prefers_the_selected_one() {
    let a = entity("vessel", "a");
    let mut sidebar = session(&[a.clone()]);
    let record = sidebar.export_record(&format!("workspace/{}", ws(1))).unwrap();
    sidebar.import_record(&format!("workspace/{}", ws(2)), &record).unwrap();
    for selected in [2, 1] {
        let open = (1..=2).map(|n| Workspace { id: ws(n), position: n as usize, name: "a".into(), selected: n == selected }).collect();
        sidebar.observe(open, vec![]);
        assert_eq!(sidebar.detail_card(&a).unwrap().workspace_id, Some(ws(selected)));
        assert_eq!(focus(&mut sidebar, &a), ws(selected));
    }
}
