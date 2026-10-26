//! The projection, natively: a model built from web deltas, as the page
//! builds it, equals the web view of a fresh snapshot.
use hegel::generators as gs;
use hegel::TestCase;
use std::collections::BTreeMap;
use wheelhouse_core::{sidebar::IntentReceipt, stream::SubscriptionId, Sidebar};

use crate::patches;
use crate::types::{Delta, Intent, ListOp, Op, Receipt, SectionView, View};

const CONFIG: &str = include_str!("../../../data/sidebar/daily-driver.kdl");
const FACTS: &str = include_str!("../../../data/sidebar/fixture.jsonl");
const PROVIDER: &str = "fixture";

fn apply_list(list: &mut Vec<String>, ops: &[ListOp]) {
    for op in ops {
        match op {
            ListOp::Insert { at, key } => list.insert(*at, key.clone()),
            ListOp::Remove { at } => {
                list.remove(*at);
            }
            ListOp::Move { from, to } => {
                let key = list.remove(*from);
                list.insert(*to, key);
            }
        }
    }
}

/// A page's model: the view, built only from deltas.
#[derive(Default)]
struct Model {
    order: Vec<String>,
    sections: BTreeMap<String, SectionView>,
    view: Option<View>,
}

impl Model {
    fn apply(&mut self, delta: &Delta) {
        let view = self.view.get_or_insert_with(|| View {
            generation: 0,
            sections: vec![],
            variables: vec![],
            diagnostics: vec![],
            errors: vec![],
        });
        for op in &delta.ops {
            match op {
                Op::Section { section } => {
                    let entry = self.sections.entry(section.name.clone()).or_insert_with(|| SectionView {
                        section: section.clone(),
                        nodes: BTreeMap::new(),
                        children: BTreeMap::new(),
                    });
                    entry.section = section.clone();
                }
                Op::RemoveSection { name } => {
                    // Its nodes went by their own ops; what is left is its list.
                    let removed = self.sections.remove(name).expect("a removed section was there");
                    assert!(removed.nodes.is_empty(), "a section's nodes are removed with it");
                }
                Op::Sections { ops } => apply_list(&mut self.order, ops),
                Op::Upsert { section, node } => {
                    self.sections.get_mut(section).expect("a node's section is there").nodes.insert(node.key.clone(), node.clone());
                }
                Op::Remove { section, key } => {
                    let section = self.sections.get_mut(section).expect("a removed node's section is there");
                    assert!(section.nodes.remove(key).is_some(), "a removed node was there");
                    section.children.remove(key);
                }
                Op::Children { section, parent, ops } => {
                    let section = self.sections.get_mut(section).expect("a list's section is there");
                    let parent = parent.clone().unwrap_or_default();
                    let list = section.children.entry(parent.clone()).or_default();
                    apply_list(list, ops);
                    if list.is_empty() {
                        section.children.remove(&parent);
                    }
                }
                Op::DisplayVariables { variables } => view.variables = variables.clone(),
                Op::Diagnostics { diagnostics } => view.diagnostics = diagnostics.clone(),
                Op::Errors { errors } => view.errors = errors.clone(),
            }
        }
        view.generation = delta.generation;
        view.sections = self.order.iter().map(|name| self.sections[name].clone()).collect();
    }
}

fn follow(sidebar: &mut Sidebar, id: SubscriptionId, model: &mut Model) -> Vec<u64> {
    let Some(delta) = sidebar.poll(id) else {
        return vec![];
    };
    let delta = Delta::from(&delta);
    // What crosses to the page is what serde gives; it must come back whole.
    let json = serde_json::to_string(&delta).unwrap();
    assert_eq!(serde_json::from_str::<Delta>(&json).unwrap(), delta);
    model.apply(&delta);
    let mut fresh = View::from(&wheelhouse_core::stream::StreamView::of(sidebar.snapshot_shared()));
    fresh.generation = model.view.as_ref().unwrap().generation;
    assert!(model.view.as_ref() == Some(&fresh), "the page's model differs from a fresh view");
    delta.intents
}

fn keys(model: &Model) -> Vec<String> {
    model.sections.values().flat_map(|section| section.nodes.keys().cloned()).collect()
}

/// For any session of facts, renames, staleness, collapse, display
/// variables and activations (some failing), a model built from web deltas
/// is the web view of a fresh snapshot, and lists every accepted intent.
#[hegel::test(test_cases = 100)]
fn a_model_built_from_web_deltas_is_the_fresh_view(tc: TestCase) {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    let id = sidebar.subscribe();
    let mut model = Model::default();
    sidebar.apply(0, PROVIDER, patches(FACTS).unwrap()).unwrap();
    follow(&mut sidebar, id, &mut model);
    let ids = ["v", "multi", "chip-v", "build"];
    for now in 1..tc.draw(gs::integers::<u64>().max_value(16)) {
        let mut accepted = vec![];
        let mut intend = |sidebar: &mut Sidebar, intent: Intent| match Receipt::from(sidebar.apply_intent(intent.read().unwrap())) {
            Receipt::Accepted { request, effects } => {
                accepted.push(request);
                effects
            }
            Receipt::Refused { .. } => vec![],
        };
        let keys = keys(&model);
        let key = keys[tc.draw(gs::integers::<usize>().max_value(keys.len() - 1))].clone();
        match tc.draw(gs::integers::<u8>().max_value(5)) {
            0 => {
                let id = ids[tc.draw(gs::integers::<usize>().max_value(ids.len() - 1))];
                let kind = if id == "build" { "convoy" } else { "vessel" };
                let label = ["~last", "!first", "Middle"][tc.draw(gs::integers::<usize>().max_value(2))];
                let rename = format!(
                    r#"{{"target":{{"kind":"entity","value":{{"kind":"{kind}","id":"{id}"}}}},"source_id":"fixture","set":{{"display.label":{{"value":{{"type":"text","value":"{label}"}}}}}},"unset":[]}}"#
                );
                sidebar.apply(now, PROVIDER, patches(&rename).unwrap()).unwrap();
            }
            1 => {
                sidebar.set_provider_stale(PROVIDER, tc.draw(gs::booleans()));
            }
            2 => {
                intend(&mut sidebar, Intent::TogglePlacement { key });
            }
            3 => {
                let names: Vec<String> = model.view.as_ref().unwrap().variables.iter().map(|v| v.name.clone()).collect();
                let name = names[tc.draw(gs::integers::<usize>().max_value(names.len() - 1))].clone();
                intend(&mut sidebar, Intent::ToggleDisplayVariable { name });
            }
            4 => {
                for effect in intend(&mut sidebar, Intent::ActivatePlacement { key }) {
                    let request = match effect {
                        crate::types::Effect::Focus { request, .. } | crate::types::Effect::Materialize { request, .. } => request,
                        _ => continue,
                    };
                    let result = if tc.draw(gs::booleans()) { Err("it failed".into()) } else { Ok(None) };
                    sidebar.complete(request, result);
                }
            }
            _ => sidebar.tick(now * 60_000),
        }
        assert_eq!(follow(&mut sidebar, id, &mut model), accepted, "a delta lists the intents accepted since the last");
    }
}

/// An intent naming a key no delta gave is refused or rejected, never
/// misapplied.
#[test]
fn intents_with_bad_keys_are_rejected() {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.apply(0, PROVIDER, patches(FACTS).unwrap()).unwrap();
    assert!(Intent::TogglePlacement { key: "9:short".into() }.read().is_err());
    let unknown = Intent::TogglePlacement { key: "1:a1:b1:c1:d".into() }.read().unwrap();
    assert!(matches!(sidebar.apply_intent(unknown), IntentReceipt::Refused { .. }));
}
