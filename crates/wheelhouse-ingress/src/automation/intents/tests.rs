use hegel::generators as gs;
use hegel::TestCase;
use serde_json::{json, Value};
use wheelhouse_core::{
    arrangement::{self, PanelId},
    sidebar, PlacementKey, WorkspaceId,
};

use super::*;
#[cfg(unix)]
use crate::automation::commands::tests::Endpoint;

const W: &str = "00000000-0000-0000-0000-00000000000a";
const X: &str = "00000000-0000-0000-0000-00000000000b";

fn draw_view(tc: &TestCase, owner: ViewOwner) -> ViewAddress {
    let key = tc.draw(gs::sampled_from(vec!["u:1", "u:2", "primary", "a:b"]));
    ViewAddress { owner, key: key.into(), previous: tc.draw(gs::booleans()) }
}

fn draw_panel(tc: &TestCase) -> String {
    tc.draw(gs::sampled_from(vec!["1", "2", "42", "main"])).into()
}

fn draw_dir(tc: &TestCase) -> Dir {
    Dir::ALL[tc.draw(gs::integers::<usize>().max_value(3))]
}

fn draw_entity(tc: &TestCase) -> EntityRef {
    EntityRef::new(tc.draw(gs::sampled_from(vec!["", "flotilla"])), "vessel", tc.draw(gs::sampled_from(vec!["c/w", "x y"])))
}

/// Any intent the endpoint knows, for a workspace's or a window's owner.
fn draw_intent(tc: &TestCase) -> Intent {
    let id = WorkspaceId::from_bytes(std::array::from_fn(|_| tc.draw(gs::integers::<u8>())));
    let window = tc.draw(gs::booleans());
    let owner = if window { ViewOwner::Window(id) } else { ViewOwner::Workspace(id) };
    let maybe = |tc: &TestCase| tc.draw(gs::booleans());
    let kind = tc.draw(gs::integers::<u8>().max_value(14));
    if kind >= 9 {
        let row = Row {
            region: tc.draw(gs::sampled_from(vec!["projects", "agents"])).into(),
            key: PlacementKey(Vec::new()).encode() + "8:p:x/y:z",
        };
        let intent = match kind {
            9 => Sidebar::CopySubjectUrl { entity: draw_entity(tc) },
            10 => Sidebar::ActivatePlacement { row },
            11 => Sidebar::Activate { entity: draw_entity(tc) },
            12 => Sidebar::TogglePlacement { row },
            13 => Sidebar::SetVariable { row, name: "density".into(), value: maybe(tc).then(|| "compact".into()) },
            _ => Sidebar::ToggleDisplayVariable { name: "show-issues".into() },
        };
        return Intent::Sidebar { owner, intent };
    }
    let other = if maybe(tc) { ViewOwner::Workspace(WorkspaceId::from_bytes([7; 16])) } else { ViewOwner::Window(id) };
    let intent = match kind {
        0 => Arrangement::SelectTab { panel: draw_panel(tc), tab: maybe(tc).then(|| draw_view(tc, owner)) },
        1 => Arrangement::MoveTab { tab: draw_view(tc, owner), to: draw_panel(tc), after: maybe(tc).then(|| draw_view(tc, owner)) },
        2 => Arrangement::CloseTab { tab: draw_view(tc, owner) },
        3 => Arrangement::DropTab {
            tab: draw_view(tc, other),
            to: draw_panel(tc),
            dir: maybe(tc).then(|| draw_dir(tc)),
            after: maybe(tc).then(|| draw_view(tc, owner)),
        },
        4 => Arrangement::SplitPanel { panel: draw_panel(tc), dir: draw_dir(tc) },
        5 => Arrangement::ClosePanel { panel: draw_panel(tc) },
        6 => Arrangement::ReorderPanel { panel: draw_panel(tc), after: maybe(tc).then(|| draw_panel(tc)) },
        7 => Arrangement::ResizePanel {
            panel: draw_panel(tc),
            delta: tc.draw(gs::floats::<f32>().min_value(-1.0).max_value(1.0)),
            floor: tc.draw(gs::floats::<f32>().min_value(0.0).max_value(0.5)),
        },
        _ => Arrangement::EqualizePanel { panel: draw_panel(tc) },
    };
    let owner = Owner { kind: if window { OwnerKind::Sidebar } else { OwnerKind::Workspace }, id };
    Intent::Arrangement { owner, intent }
}

// Every intent maps from its Automation IDs and back, through the JSON a
// client sends, under the Core's own name for it.
#[hegel::test(test_cases = 500)]
fn every_intent_maps_from_automation_ids_and_back(tc: TestCase) {
    let intent = draw_intent(&tc);
    let json = serde_json::to_value(intent.body()).unwrap();
    assert_eq!(json["intent"], intent.name());
    let body: Body = serde_json::from_value(json.clone()).unwrap();
    assert_eq!(Intent::from_body(&body), Ok(intent.clone()), "{json}");
    // Its owner is its Automation ID, and so is every panel and tab in it.
    let owner = json["owner"].as_str().unwrap();
    assert!(ViewOwner::from_automation_id(owner).is_some(), "{owner}");
    for field in ["panel", "to"] {
        if let Some(panel) = json.get(field) {
            assert!(PanelAddress::from_automation_id(panel.as_str().unwrap()).is_some_and(|p| p.owner.automation_id() == owner), "{json}");
        }
    }
    if let Some(tab) = json.get("tab") {
        assert!(ViewAddress::from_automation_id(tab.as_str().unwrap()).is_some(), "{json}");
    }
}

// The names are the Core's.
#[test]
fn the_names_are_the_cores() {
    let id: WorkspaceId = W.parse().unwrap();
    let owner = arrangement::Owner { kind: OwnerKind::Workspace, id };
    let (panel, tab) = (PanelId(1), "t");
    let core: Vec<arrangement::Intent<&str>> = vec![
        arrangement::Intent::SelectTab { owner, panel, tab: None },
        arrangement::Intent::MoveTab { owner, tab, to: panel, after: None },
        arrangement::Intent::CloseTab { owner, tab },
        arrangement::Intent::DropTab { owner, tab, to: panel, dir: None, after: None },
        arrangement::Intent::SplitPanel { owner, panel, dir: Dir::Left },
        arrangement::Intent::ClosePanel { owner, panel },
        arrangement::Intent::ReorderPanel { owner, panel, after: None },
        arrangement::Intent::ResizePanel { owner, panel, delta: 0.0, floor: 0.0 },
        arrangement::Intent::EqualizePanel { owner, panel },
    ];
    let view = ViewAddress { owner: ViewOwner::Workspace(id), key: "t".into(), previous: false };
    let p = || "1".to_owned();
    let ours = [
        Arrangement::SelectTab { panel: p(), tab: None },
        Arrangement::MoveTab { tab: view.clone(), to: p(), after: None },
        Arrangement::CloseTab { tab: view.clone() },
        Arrangement::DropTab { tab: view.clone(), to: p(), dir: None, after: None },
        Arrangement::SplitPanel { panel: p(), dir: Dir::Left },
        Arrangement::ClosePanel { panel: p() },
        Arrangement::ReorderPanel { panel: p(), after: None },
        Arrangement::ResizePanel { panel: p(), delta: 0.0, floor: 0.0 },
        Arrangement::EqualizePanel { panel: p() },
    ];
    assert_eq!(ours.iter().map(Arrangement::name).collect::<Vec<_>>(), core.iter().map(arrangement::Intent::name).collect::<Vec<_>>());
    let entity = EntityRef::new("p", "k", "i");
    let key = PlacementKey(Vec::new());
    let row = Row { region: "r".into(), key: key.encode() };
    let core = [
        sidebar::Intent::CopySubjectUrl { entity: entity.clone() },
        sidebar::Intent::ActivatePlacement { key: key.clone() },
        sidebar::Intent::Activate { entity: entity.clone() },
        sidebar::Intent::TogglePlacement { key: key.clone() },
        sidebar::Intent::SetVariable { key, name: "n".into(), value: None },
        sidebar::Intent::ToggleDisplayVariable { name: "n".into() },
    ];
    let ours = [
        Sidebar::CopySubjectUrl { entity: entity.clone() },
        Sidebar::ActivatePlacement { row: row.clone() },
        Sidebar::Activate { entity },
        Sidebar::TogglePlacement { row: row.clone() },
        Sidebar::SetVariable { row, name: "n".into(), value: None },
        Sidebar::ToggleDisplayVariable { name: "n".into() },
    ];
    assert_eq!(ours.iter().map(Sidebar::name).collect::<Vec<_>>(), core.iter().map(sidebar::Intent::name).collect::<Vec<_>>());
}

fn named(value: Value) -> Result<Intent, String> {
    Intent::from_body(&serde_json::from_value(value).unwrap())
}

// A panel is its owner's, and so is a tab, but a dropped one; an ID that
// names nothing, or the wrong kind of thing, names no intent.
#[test]
fn an_intents_ids_must_be_its_owners() {
    let w = format!("workspace/{W}");
    let x = format!("workspace/{X}");
    let ok = |value| named(value).is_ok();
    assert!(ok(json!({"intent": "close-panel", "owner": w, "panel": format!("{w}/panel/2")})));
    assert!(!ok(json!({"intent": "close-panel", "owner": w, "panel": format!("{x}/panel/2")})));
    assert!(!ok(json!({"intent": "close-panel", "owner": w, "panel": format!("{w}/view/u:1")})));
    assert!(!ok(json!({"intent": "close-panel", "owner": "dashboard/d", "panel": format!("{w}/panel/2")})));
    assert!(ok(json!({"intent": "close-tab", "owner": w, "tab": format!("{w}/view/u:1")})));
    assert!(!ok(json!({"intent": "close-tab", "owner": w, "tab": format!("{x}/view/u:1")})));
    assert!(ok(
        json!({"intent": "drop-tab", "owner": w, "tab": format!("window/{X}/view/u:1"), "to": format!("{w}/panel/1"), "dir": "left"})
    ));
    // A window's arrangement is its sidebar's dock.
    let dock = named(json!({"intent": "equalize-panel", "owner": format!("window/{W}"), "panel": format!("window/{W}/panel/3")})).unwrap();
    assert!(matches!(dock, Intent::Arrangement { owner: Owner { kind: OwnerKind::Sidebar, .. }, .. }));
    // A sidebar's row is a section's.
    assert!(ok(json!({"intent": "toggle-placement", "owner": w, "key": "section/projects/row/4:root"})));
    assert!(!ok(json!({"intent": "toggle-placement", "owner": w, "key": "row/4:root"})));
    let endless = Body::ResizePanel { owner: w.clone(), panel: format!("{w}/panel/2"), delta: f32::INFINITY, floor: 0.0 };
    assert!(Intent::from_body(&endless).is_err());
    let unknown = Body::ResizePanel { owner: w.clone(), panel: format!("{w}/panel/2"), delta: f32::NAN, floor: 0.0 };
    assert!(Intent::from_body(&unknown).is_err());
    let unknown = Body::ResizePanel { owner: w.clone(), panel: format!("{w}/panel/2"), delta: 0.1, floor: f32::NAN };
    assert!(Intent::from_body(&unknown).is_err());
    let sunken = Body::ResizePanel { owner: w.clone(), panel: format!("{w}/panel/2"), delta: 0.1, floor: -0.1 };
    assert!(Intent::from_body(&sunken).is_err());
    // Fields an intent doesn't take, and a name the Core doesn't have.
    assert!(serde_json::from_value::<Body>(json!({"intent": "close-panel", "owner": w, "panel": "p", "tab": "t"})).is_err());
    assert!(serde_json::from_value::<Body>(json!({"intent": "close-everything", "owner": w})).is_err());
}

// An accepted intent answers with its request and the cursor of the frame
// it shows in; a refused one with 422 and why, as a receipt.
#[cfg(unix)]
#[test]
fn an_intent_answers_with_the_cores_receipt() {
    let endpoint = Endpoint::start();
    let w = format!("workspace/{W}");
    let apply = |body: Value| endpoint.request("POST", "/v1/intents/apply", &body.to_string());
    let (status, receipt) = apply(json!({"intent": "close-tab", "owner": w, "tab": format!("{w}/view/u:1")}));
    assert_eq!(status, 200, "{receipt}");
    let receipt: Receipt = serde_json::from_str(&receipt).unwrap();
    let ticket = endpoint.ui.lock().unwrap().applied[0].0;
    assert_eq!((receipt.accepted, receipt.request, receipt.reason), (true, Some(100 + ticket), None));
    assert!(endpoint.resumes_quietly(&receipt.cursor.unwrap()), "the cursor came before what the intent did");
    let refused = |(status, body): (u16, String)| (status, serde_json::from_str::<Receipt>(&body).unwrap());
    let (status, receipt) = refused(apply(json!({"intent": "close-panel", "owner": w, "panel": format!("{w}/panel/9")})));
    assert_eq!((status, receipt.accepted, receipt.reason.as_deref()), (422, false, Some("no panel 9")));
    // One that names nothing, or a field it doesn't take, is refused too.
    let (status, receipt) = refused(apply(json!({"intent": "close-panel", "owner": w, "panel": format!("workspace/{X}/panel/1")})));
    assert_eq!((status, receipt.accepted), (422, false));
    assert_eq!(refused(apply(json!({"intent": "close-panel", "owner": w, "panel": "p", "x": 1}))).0, 422);
    assert_eq!(endpoint.request("POST", "/v1/intents/apply", "{").0, 400);
    endpoint.mode.store(crate::AUTOMATION_OBSERVE, std::sync::atomic::Ordering::SeqCst);
    std::thread::sleep(std::time::Duration::from_millis(50));
    assert_eq!(apply(json!({"intent": "close-tab", "owner": w, "tab": format!("{w}/view/u:1")})).0, 403);
    assert_eq!(endpoint.ui.lock().unwrap().applied.len(), 1);
}
