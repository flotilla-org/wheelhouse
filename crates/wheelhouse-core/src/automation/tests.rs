//! The Semantic Tree has one node per panel and per current View, each
//! where its arrangement puts it, with IDs that round-trip from View Address.
use std::collections::BTreeSet;

use hegel::generators as gs;
use hegel::TestCase;

use super::*;
use crate::{
    slots::{SlotInfo, USER_PREFIX},
    suggested_layout::RebindPolicy,
    Sidebar,
};

const WORKSPACE: &str = "0192f1a2-3b4c-7d5e-8f60-718293a4b5c6";

fn spec(tc: &TestCase, n: usize) -> ViewSpec {
    let content = match tc.draw(gs::integers::<u8>().max_value(4)) {
        0 => Content::Local(LocalRecipe::Command { line: CommandLine::Shell(format!("run {n}")), cwd: Some("/repo".into()) }),
        1 => Content::Local(LocalRecipe::Url { url: "wheelhouse:sessions".into() }),
        2 => Content::Local(LocalRecipe::Jackstay { launcher: "l".into(), endpoint: "e".into() }),
        3 => Content::Local(LocalRecipe::File { path: "/x".into() }),
        _ => Content::ProviderFacet { entity: EntityRef::new("p", "vessel", "v"), facet: "agent".into() },
    };
    let presentation = tc.draw(gs::booleans()).then(|| "table".to_owned());
    ViewSpec { content, presentation, role: None }
}

/// A panel tree over some of `keys`, each placed at most once, with fresh
/// panel IDs and weights (0 for a collapsed panel).
fn draw_panel(tc: &TestCase, keys: &mut Vec<String>, next: &mut u32, depth: u32) -> Panel {
    *next += 1;
    let id = if tc.draw(gs::booleans()) { format!("{next}") } else { format!("p{next}") };
    let weight = if tc.draw(gs::integers::<u8>().max_value(5)) == 0 { 0.0 } else { f64::from(tc.draw(gs::integers::<u8>().min_value(1))) };
    let node = if depth > 0 && tc.draw(gs::booleans()) {
        let count = tc.draw(gs::integers::<usize>().min_value(1).max_value(3));
        let axis = if tc.draw(gs::booleans()) { Axis::Row } else { Axis::Column };
        PanelNode::Split { axis, children: (0..count).map(|_| draw_panel(tc, keys, next, depth - 1)).collect() }
    } else {
        let count = tc.draw(gs::integers::<usize>().max_value(keys.len().min(3)));
        let tabs: Vec<String> = (0..count).map(|_| keys.remove(tc.draw(gs::integers::<usize>().max_value(keys.len() - 1)))).collect();
        let selected = match tabs.len() {
            0 => None,
            n => tc.draw(gs::booleans()).then(|| tabs[tc.draw(gs::integers::<usize>().max_value(n - 1))].clone()),
        };
        PanelNode::Tabs { tabs, selected }
    };
    Panel { id, weight, node }
}

#[hegel::test(test_cases = 300)]
fn one_node_per_panel_and_current_view_with_ids_from_view_address(tc: TestCase) {
    let workspace: WorkspaceId = WORKSPACE.parse().unwrap();
    let count = tc.draw(gs::integers::<usize>().max_value(8));
    let slots: Vec<SlotInfo> = (0..count)
        .map(|n| SlotInfo {
            key: if tc.draw(gs::booleans()) { format!("{USER_PREFIX}{n}") } else { format!("k{n}") },
            spec: spec(&tc, n),
            rebind: RebindPolicy::Replace,
            in_baseline: false,
            detached: false,
            flags: 0,
        })
        .collect();
    let mut keys: Vec<String> = slots.iter().map(|slot| slot.key.clone()).collect();
    // Some tabs name Slots that have gone.
    keys.extend((0..tc.draw(gs::integers::<usize>().max_value(2))).map(|n| format!("gone{n}")));
    let doc = ArrangementDoc { root: tc.draw(gs::booleans()).then(|| draw_panel(&tc, &mut keys, &mut 0, 3)) };
    let node = workspace_node(workspace, Some("W".into()), None, &doc, &slots);

    assert_eq!((node.id.as_str(), node.kind, node.name.as_deref()), (format!("workspace/{WORKSPACE}").as_str(), "workspace", Some("W")));
    let nodes = node.preorder();
    let ids: BTreeSet<&str> = nodes.iter().map(|n| n.id.as_str()).collect();
    assert_eq!(ids.len(), nodes.len(), "IDs are unique");
    // One panel node per panel, in the document's order.
    let panels: Vec<String> = doc.root.iter().flat_map(Panel::preorder).map(|p| format!("workspace/{WORKSPACE}/panel/{}", p.id)).collect();
    let panel_nodes: Vec<&str> = nodes.iter().filter(|n| n.kind == "panel").map(|n| n.id.as_str()).collect();
    assert_eq!(panel_nodes, panels);
    // One view per current Slot, never for a gone tab.
    let views: Vec<&&Node> = nodes.iter().filter(|n| n.kind == "view").collect();
    assert_eq!(views.len(), slots.len());
    for slot in &slots {
        let address = ViewAddress { owner: ViewOwner::Workspace(workspace), key: slot.key.clone(), previous: false };
        let view = views.iter().find(|v| v.id == address.automation_id()).expect("a view per slot");
        assert_eq!(ViewAddress::from_automation_id(&view.id), Some(address));
        assert!(view.durable);
        assert_eq!(view.props["content"], content_kind(&slot.spec));
        assert_eq!(view.props["slot"], slot.key.as_str());
    }
    // Each placed View is in its panel, selected as its panel says.
    for panel in doc.root.iter().flat_map(Panel::preorder) {
        let panel_node = nodes.iter().find(|n| n.id == format!("workspace/{WORKSPACE}/panel/{}", panel.id)).unwrap();
        assert_eq!(panel_node.state.contains(&"collapsed"), panel.weight == 0.0);
        match &panel.node {
            PanelNode::Tabs { tabs, selected } => {
                let shown: Vec<&String> = tabs.iter().filter(|tab| slots.iter().any(|slot| &slot.key == *tab)).collect();
                let children: Vec<String> = panel_node.children.iter().map(|c| c.props["slot"].as_str().unwrap().to_owned()).collect();
                assert_eq!(children.iter().collect::<Vec<_>>(), shown);
                for child in &panel_node.children {
                    assert_eq!(child.state.contains(&"selected"), selected.as_deref() == child.props["slot"].as_str());
                }
            }
            PanelNode::Split { children, .. } => {
                let weights: Vec<f64> = children.iter().map(|c| c.weight).collect();
                assert_eq!(panel_node.props["weights"], serde_json::to_value(weights).unwrap());
                assert!(panel_node.props.contains_key("split"));
            }
        }
    }
    // The serialised node leaves empty fields out.
    fn check(value: &Value) {
        for field in ["state", "props", "children"] {
            assert!(value
                .get(field)
                .is_none_or(|v| v.as_array().is_some_and(|a| !a.is_empty()) || v.as_object().is_some_and(|o| !o.is_empty())));
        }
        value.get("children").and_then(Value::as_array).into_iter().flatten().for_each(check);
    }
    check(&node.to_value());
}

#[test]
fn view_addresses_round_trip() {
    let id: WorkspaceId = WORKSPACE.parse().unwrap();
    for address in [
        ViewAddress { owner: ViewOwner::Workspace(id), key: "u:3".into(), previous: false },
        ViewAddress { owner: ViewOwner::Workspace(id), key: "primary".into(), previous: true },
        ViewAddress { owner: ViewOwner::Window(id), key: "u:1".into(), previous: false },
    ] {
        assert_eq!(ViewAddress::from_automation_id(&address.automation_id()), Some(address));
    }
    assert_eq!(
        ViewAddress { owner: ViewOwner::Workspace(id), key: "k".into(), previous: true }.automation_id(),
        format!("workspace/{WORKSPACE}/view/k@previous")
    );
    for bad in ["", "workspace", format!("workspace/{WORKSPACE}").as_str(), "workspace/nope/view/k", "dashboard/x/view/k"] {
        assert_eq!(ViewAddress::from_automation_id(bad), None, "{bad}");
    }
}

#[test]
fn content_names_what_a_view_shows() {
    let local = |recipe| ViewSpec { content: Content::Local(recipe), presentation: None, role: None };
    let command = LocalRecipe::Command { line: CommandLine::Shell("x".into()), cwd: None };
    assert_eq!(content_kind(&local(command.clone())), "terminal");
    assert_eq!(content_kind(&local(LocalRecipe::Url { url: "wheelhouse:terminal?cwd=/x".into() })), "terminal");
    assert_eq!(content_kind(&local(LocalRecipe::Url { url: "wheelhouse:table".into() })), "table");
    assert_eq!(content_kind(&local(LocalRecipe::Url { url: "https://x".into() })), "web");
    assert_eq!(content_kind(&local(LocalRecipe::Jackstay { launcher: "l".into(), endpoint: "e".into() })), "jackstay");
    assert_eq!(content_kind(&ViewSpec { presentation: Some("sessions".into()), ..local(command) }), "sessions");
}

// Through the core: a workspace record gives its tree, named and with its
// subject.
#[test]
fn the_core_serves_its_workspaces() {
    let mut sidebar = Sidebar::new(include_str!("../../../../data/sidebar/fixture.kdl")).unwrap();
    let record = serde_json::json!({
        "subject": {"entity": {"provider": "p", "kind": "vessel", "id": "v"}, "retained": []},
        "slots": {"baseline": null, "edits": {}, "name": "Mine", "mood": null, "departed": {},
                  "user": [{"key": "u:1", "spec": {"content": {"Local": {"Command": {"line": {"Shell": "top"}, "cwd": null}}}, "presentation": "terminal"}, "rebind": "Replace"},
                           {"key": "u:2", "spec": {"content": {"Local": {"Url": {"url": "wheelhouse:sessions"}}}, "presentation": "sessions"}, "rebind": "Replace"}]},
        "arrangement": {"owned": true, "doc": {"root": {"id": "1", "weight": 1.0, "node": {"Split": {"axis": "Row", "children": [
            {"id": "2", "weight": 2.0, "node": {"Tabs": {"tabs": ["u:1"], "selected": "u:1"}}},
            {"id": "3", "weight": 1.0, "node": {"Tabs": {"tabs": ["u:2"], "selected": null}}}]}}}},
            "placed": [], "soft": {}, "provider_changed": false},
        "resolutions": {},
    });
    sidebar.import_record(&format!("workspace/{WORKSPACE}"), &record.to_string()).unwrap();
    let tree = sidebar.semantic_workspaces();
    assert_eq!(tree.len(), 1);
    let w = format!("workspace/{WORKSPACE}");
    assert_eq!(
        tree[0].to_value(),
        serde_json::json!({
            "id": w, "kind": "workspace", "name": "Mine", "durable": true,
            "props": {"subject": {"provider": "p", "kind": "vessel", "id": "v"}},
            "children": [{"id": format!("{w}/panel/1"), "kind": "panel", "durable": true,
                "props": {"split": "row", "weights": [2.0, 1.0]},
                "children": [
                    {"id": format!("{w}/panel/2"), "kind": "panel", "durable": true, "children": [
                        {"id": format!("{w}/view/u:1"), "kind": "view", "state": ["selected"], "durable": true,
                         "props": {"slot": "u:1", "content": "terminal", "terminal": {"process": "top"}}}]},
                    {"id": format!("{w}/panel/3"), "kind": "panel", "durable": true, "children": [
                        {"id": format!("{w}/view/u:2"), "kind": "view", "durable": true, "props": {"slot": "u:2", "content": "sessions"}}]},
                ]}],
        })
    );
}
