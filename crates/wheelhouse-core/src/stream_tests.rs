//! The change stream (docs/design/change-stream.md): a frontend that applies
//! every delta it is given has what a fresh snapshot presents, however often
//! it polls.
use std::collections::{BTreeMap, BTreeSet};
use std::rc::Rc;

use hegel::generators as gs;
use hegel::TestCase;

use crate::presentation::PlacementNode;
use crate::scaling_tests::{catalog, fleet, patch, text, Shape, CONFIG, PROVIDER};
use crate::sidebar::{HostEffect, Intent, IntentReceipt, Sidebar, Workspace};
use crate::stream::{apply_list, list_ops, ListOp, StreamView, SubscriptionId, UnknownSubscription};
use crate::{tab_workspace_id, EntityRef, MetadataPatch, MetadataTarget, MetadataValue};

const DAILY_DRIVER: &str = include_str!("../../../data/sidebar/daily-driver.kdl");

/// A small fleet with every kind in it: two projects, roles, change
/// requests, attention, and convoys both active and landed.
const SHAPE: Shape =
    Shape { projects: 2, convoys_per_project: 5, change_request_every: 3, role_every: 4, active_every: 2, attention_every: 3 };
const CONVOYS: usize = 10;

fn convoy(c: usize) -> EntityRef {
    EntityRef::new(PROVIDER, "convoy", format!("fleet/convoy-{c:08x}"))
}

/// A patch changing only `facts` and `unset` of convoy `c`'s `kind` entity.
fn change(kind: &str, c: usize, facts: Vec<(&str, MetadataValue)>, unset: &[&str]) -> MetadataPatch {
    let mut patch = patch(kind, &format!("fleet/convoy-{c:08x}"), facts.into_iter().map(|(k, v)| (k.to_owned(), v)).collect());
    patch.set.retain(|key, _| !matches!(key.as_str(), "entity.kind" | "entity.id" | "source"));
    patch.unset = unset.iter().map(|key| (*key).to_owned()).collect();
    patch
}

fn nodes(sidebar: &Sidebar) -> Vec<Rc<PlacementNode>> {
    fn add(nodes: &[Rc<PlacementNode>], out: &mut Vec<Rc<PlacementNode>>) {
        for node in nodes {
            out.push(node.clone());
            add(&node.children, out);
        }
    }
    let mut out = vec![];
    for section in &sidebar.snapshot_shared().surface.sections {
        add(&section.nodes, &mut out);
    }
    out
}

/// Apply an intent, noting its request if accepted. A refused one must
/// change nothing.
fn intend(sidebar: &mut Sidebar, accepted: &mut Vec<u64>, intent: Intent) -> Vec<HostEffect> {
    let revision = sidebar.revision();
    match sidebar.apply_intent(intent) {
        IntentReceipt::Accepted { request, effects } => {
            accepted.push(request);
            effects
        }
        IntentReceipt::Refused { reason } => {
            assert_eq!(sidebar.revision(), revision, "a refused intent ({reason}) moved the revision");
            vec![]
        }
    }
}

/// One change of the kinds a session makes: facts, the catalog, time,
/// staleness, workspaces, collapse, display variables, sibling order and
/// local entities. Returns the intents accepted.
fn step(tc: &TestCase, sidebar: &mut Sidebar, now: &mut u64) -> Vec<u64> {
    let mut accepted = vec![];
    *now += 1;
    let now = *now;
    let c = tc.draw(gs::integers::<usize>().max_value(CONVOYS - 1));
    let pick = |len: usize| tc.draw(gs::integers::<usize>().max_value(len.max(1) - 1));
    match tc.draw(gs::integers::<u8>().max_value(14)) {
        0 => {
            let phase = if tc.draw(gs::booleans()) { "active" } else { "landed" };
            sidebar.apply(now, PROVIDER, [change("convoy", c, vec![("flotilla.convoy.phase", text(phase))], &[])]).unwrap();
        }
        1 => {
            let label = ["renamed", "zeta", "alpha"][pick(3)];
            sidebar.apply(now, PROVIDER, [change("convoy", c, vec![("display.label", text(label))], &[])]).unwrap();
        }
        2 => {
            let attention = tc.draw(gs::booleans());
            sidebar.apply(now, PROVIDER, [change("vessel", c, vec![("status.attention", MetadataValue::Bool(attention))], &[])]).unwrap();
        }
        3 => {
            let project = format!("fleet/p{}@fleet", pick(2));
            sidebar.apply(now, PROVIDER, [change("convoy", c, vec![("flotilla.project", text(project))], &[])]).unwrap();
        }
        4 => {
            let kind = ["convoy", "vessel"][pick(2)];
            sidebar.apply(now, PROVIDER, [change(kind, c, vec![], &["entity.id", "entity.kind"])]).unwrap();
        }
        5 => sidebar.apply(now, PROVIDER, fleet(SHAPE, CONVOYS)).unwrap(),
        6 => {
            // A connector reconnecting: its snapshot replaces what it published.
            sidebar.retract_provider(PROVIDER);
            sidebar.apply(now, PROVIDER, fleet(SHAPE, CONVOYS)).unwrap();
        }
        7 => sidebar.tick(now + tc.draw(gs::integers::<u64>().max_value(100_000))),
        8 => {
            sidebar.set_provider_stale(PROVIDER, tc.draw(gs::booleans()));
        }
        9 => {
            let open = tc.draw(gs::integers::<usize>().max_value(3));
            let selected = pick(open);
            let workspaces = (0..open)
                .map(|position| Workspace {
                    id: tab_workspace_id(position as u64 + 1),
                    position,
                    name: format!("w{position}"),
                    selected: position == selected,
                })
                .collect();
            sidebar.observe(workspaces, vec![]);
            if open > 0 {
                let entity = convoy(c);
                let mut bind = patch(&entity.kind, &entity.id, vec![]);
                bind.target = MetadataTarget::Tab(tab_workspace_id(pick(open) as u64 + 1));
                bind.source_id = "host".into();
                sidebar.apply(now, PROVIDER, [bind]).unwrap();
            }
        }
        10 => {
            let nodes = nodes(sidebar);
            if !nodes.is_empty() {
                let key = nodes[pick(nodes.len())].key.clone();
                intend(sidebar, &mut accepted, Intent::TogglePlacement { key });
            }
        }
        11 => {
            let names: Vec<String> =
                sidebar.snapshot_shared().surface.display_variables.iter().map(|variable| variable.name.clone()).collect();
            if !names.is_empty() {
                // Some aren't toggles, and are refused.
                intend(sidebar, &mut accepted, Intent::ToggleDisplayVariable { name: names[pick(names.len())].clone() });
            }
        }
        12 => {
            let parents: Vec<_> = nodes(sidebar).into_iter().filter(|node| node.children.len() > 1).collect();
            if !parents.is_empty() {
                let parent = &parents[pick(parents.len())];
                let mut order: Vec<EntityRef> = parent.children.iter().map(|child| child.entity.clone()).collect();
                let by = 1 + pick(order.len() - 1);
                order.rotate_left(by);
                let _ = sidebar.set_sibling_order(parent.children[0].loop_key.clone(), order);
            }
        }
        13 => {
            // Activate a node and finish the host's request, failing or not:
            // a failed activation is an error the sidebar presents.
            let nodes = nodes(sidebar);
            if !nodes.is_empty() {
                let node = &nodes[pick(nodes.len())];
                let intent = if tc.draw(gs::booleans()) {
                    Intent::ActivatePlacement { key: node.key.clone() }
                } else {
                    Intent::CopySubjectUrl { entity: node.entity.clone() }
                };
                for effect in intend(sidebar, &mut accepted, intent) {
                    let request = match effect {
                        HostEffect::Focus { request_id, .. } | HostEffect::Materialize { request_id, .. } => request_id,
                        _ => continue,
                    };
                    let result = if tc.draw(gs::booleans()) { Err("it failed".into()) } else { Ok(None) };
                    sidebar.complete(request, result);
                }
            }
        }
        _ => {
            let (section, group) = (EntityRef::local(".section", "s"), EntityRef::local(".group", "g"));
            let reference = EntityRef::local(".ref", "r");
            let refs = |entity: EntityRef| MetadataValue::EntityRefs(vec![entity]);
            if tc.draw(gs::booleans()) {
                sidebar.set_local(section.clone(), BTreeMap::from([("display.label".into(), text("Pinned"))])).unwrap();
                sidebar.set_local(group.clone(), BTreeMap::from([(".section".into(), refs(section))])).unwrap();
                sidebar
                    .set_local(reference, BTreeMap::from([(".group".into(), refs(group)), (".target".into(), refs(convoy(c)))]))
                    .unwrap();
            } else {
                sidebar.remove_local(&reference);
            }
        }
    }
    accepted
}

/// A frontend following one subscription.
struct Follower {
    id: SubscriptionId,
    view: Option<StreamView>,
    /// Intents accepted that no delta has listed yet.
    awaited: Vec<u64>,
}

impl Follower {
    fn poll(&mut self, sidebar: &mut Sidebar) {
        let delta = sidebar.poll(self.id).unwrap();
        match (&mut self.view, delta) {
            (None, Some(delta)) => {
                assert_eq!(delta.intents, std::mem::take(&mut self.awaited));
                let mut view = StreamView::default();
                view.apply(&delta).unwrap();
                self.view = Some(view);
            }
            (None, None) => panic!("a first poll gives everything"),
            (Some(view), Some(delta)) => {
                assert!(!delta.ops.is_empty() || !delta.intents.is_empty(), "a delta after the first changes or answers something");
                assert!(delta.generation >= view.generation, "generations never go back");
                assert!(delta.ops.is_empty() || delta.generation > view.generation, "a change moves the generation");
                assert_eq!(delta.intents, std::mem::take(&mut self.awaited), "a delta lists the intents accepted since the last");
                view.apply(&delta).unwrap();
            }
            (Some(_), None) => assert!(self.awaited.is_empty(), "an accepted intent is listed by the next poll"),
        }
        let view = self.view.as_ref().unwrap();
        let mut fresh = StreamView::of(sidebar.snapshot_shared());
        // A change that presents nothing new delivers nothing, so the view
        // keeps the generation of the last change it saw.
        fresh.generation = view.generation;
        assert!(view == &fresh, "the followed view differs from a fresh snapshot's");
        assert!(sidebar.poll(self.id).unwrap().is_none(), "a poll straight after another has nothing");
    }
}

fn followed_views_match_fresh_snapshots(tc: TestCase, config: &str) {
    let mut sidebar = Sidebar::new(config).unwrap();
    sidebar.apply(0, PROVIDER, fleet(SHAPE, CONVOYS)).unwrap();
    let mut every = Follower { id: sidebar.subscribe(), view: None, awaited: vec![] };
    let mut rarely = Follower { id: sidebar.subscribe(), view: None, awaited: vec![] };
    let mut now = 0;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(24)) {
        let accepted = step(&tc, &mut sidebar, &mut now);
        every.awaited.extend(&accepted);
        rarely.awaited.extend(&accepted);
        every.poll(&mut sidebar);
        if tc.draw(gs::weighted_booleans(0.2)) {
            rarely.poll(&mut sidebar);
        }
    }
    rarely.poll(&mut sidebar);
}

/// Following the stream for any session gives what a fresh snapshot does,
/// for a frontend that polls after every change and one that polls rarely.
#[hegel::test(test_cases = 200)]
fn followed_views_match_fresh_snapshots_for_the_fixture(tc: TestCase) {
    followed_views_match_fresh_snapshots(tc, CONFIG);
}

#[hegel::test(test_cases = 200)]
fn followed_views_match_fresh_snapshots_for_the_daily_driver(tc: TestCase) {
    followed_views_match_fresh_snapshots(tc, DAILY_DRIVER);
}

/// Two regions sharing a placement place the same nodes, by the same keys,
/// in two sections; each section's are its own.
#[hegel::test(test_cases = 200)]
fn followed_views_match_fresh_snapshots_with_a_shared_placement(tc: TestCase) {
    let shared = CONFIG.replace(r#"placement="attention""#, r#"placement="tree""#);
    assert_ne!(shared, CONFIG);
    followed_views_match_fresh_snapshots(tc, &shared);
}

/// `changed_since` and `poll` fail exactly for a subscription that isn't
/// live, never given or since unsubscribed, and a failed call changes
/// nothing: the revision stays, and every live subscription goes on as it
/// would have without it, as a twin sidebar given only the calls that
/// succeed shows. No ID is ever given twice.
#[hegel::test(test_cases = 300)]
fn only_live_subscriptions_are_followed(tc: TestCase) {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    let mut twin = Sidebar::new(CONFIG).unwrap();
    for sidebar in [&mut sidebar, &mut twin] {
        sidebar.apply(0, PROVIDER, fleet(SHAPE, CONVOYS)).unwrap();
    }
    let mut given: Vec<SubscriptionId> = vec![];
    let mut live = BTreeSet::new();
    let mut now = 0;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(30)) {
        // An ID given before, live or not, or one not given yet.
        let drawn = || {
            if !given.is_empty() && tc.draw(gs::booleans()) {
                given[tc.draw(gs::integers::<usize>().max_value(given.len() - 1))]
            } else {
                let next = given.iter().map(|id| id.0 + 1).max().unwrap_or(0);
                SubscriptionId(next + tc.draw(gs::integers::<u64>().max_value(3)))
            }
        };
        match tc.draw(gs::integers::<u8>().max_value(4)) {
            0 => {
                let id = sidebar.subscribe();
                assert!(!given.contains(&id), "{id:?} was given before");
                assert_eq!(twin.subscribe(), id);
                given.push(id);
                live.insert(id);
            }
            1 => {
                let id = drawn();
                assert_eq!(sidebar.unsubscribe(id), live.remove(&id), "unsubscribing {id:?}");
                twin.unsubscribe(id);
            }
            2 | 3 => {
                let id = drawn();
                let revision = sidebar.revision();
                let polling = tc.draw(gs::booleans());
                let result = if polling { sidebar.poll(id).map(|_| ()) } else { sidebar.changed_since(id).map(|_| ()) };
                if live.contains(&id) {
                    assert!(result.is_ok(), "a live subscription {id:?} failed");
                    // The twin is given every call that succeeds.
                    if polling {
                        twin.poll(id).unwrap();
                    }
                } else {
                    assert_eq!(result, Err(UnknownSubscription(id)));
                    assert_eq!(sidebar.revision(), revision, "a failed call moved the revision");
                }
            }
            _ => {
                now += 1;
                let c = tc.draw(gs::integers::<usize>().max_value(CONVOYS - 1));
                let label = ["renamed", "zeta", "alpha"][tc.draw(gs::integers::<usize>().max_value(2))];
                for sidebar in [&mut sidebar, &mut twin] {
                    sidebar.apply(now, PROVIDER, [change("convoy", c, vec![("display.label", text(label))], &[])]).unwrap();
                }
            }
        }
    }
    assert_eq!(sidebar.revision(), twin.revision());
    for &id in &live {
        assert_eq!(sidebar.changed_since(id), twin.changed_since(id), "{id:?} changed since");
        assert_eq!(sidebar.poll(id), twin.poll(id), "{id:?} polled");
    }
}

/// A quiet live subscription has nothing to deliver; the same ID once
/// unsubscribed is an error from both calls.
#[test]
fn an_unsubscribed_id_is_an_error_not_quiet() {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.apply(1, PROVIDER, catalog(20)).unwrap();
    let id = sidebar.subscribe();
    sidebar.poll(id).unwrap().unwrap();
    assert_eq!(sidebar.changed_since(id), Ok(false));
    assert_eq!(sidebar.poll(id), Ok(None));
    assert!(sidebar.unsubscribe(id));
    assert_eq!(sidebar.changed_since(id), Err(UnknownSubscription(id)));
    assert_eq!(sidebar.poll(id), Err(UnknownSubscription(id)));
    assert!(UnknownSubscription(id).to_string().contains(&id.0.to_string()));
}

/// An intent's name is its serialized tag, so the two can't drift apart.
#[test]
fn intent_names_are_their_tags() {
    let key = crate::PlacementKey::default();
    let entity = EntityRef::local(".ref", "r");
    for intent in [
        Intent::CopySubjectUrl { entity: entity.clone() },
        Intent::ActivatePlacement { key: key.clone() },
        Intent::Activate { entity },
        Intent::TogglePlacement { key: key.clone() },
        Intent::SetVariable { key, name: "density".into(), value: None },
        Intent::ToggleDisplayVariable { name: "show-finished".into() },
    ] {
        assert_eq!(serde_json::to_value(&intent).unwrap()["action"], intent.name());
    }
}

/// A refused intent changes nothing and no delta lists it; an accepted one
/// that changes nothing presented is still listed, in a delta of no changes.
#[test]
fn deltas_list_accepted_intents_only() {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.apply(1, PROVIDER, catalog(20)).unwrap();
    let id = sidebar.subscribe();
    sidebar.poll(id).unwrap().unwrap();
    let refused = sidebar.apply_intent(Intent::TogglePlacement { key: crate::PlacementKey::default() });
    assert!(matches!(refused, IntentReceipt::Refused { .. }), "{refused:?}");
    assert!(!sidebar.changed_since(id).unwrap() && sidebar.poll(id).unwrap().is_none());
    let name = sidebar.snapshot_shared().surface.display_variables[0].name.clone();
    let IntentReceipt::Accepted { request, .. } = sidebar.apply_intent(Intent::ToggleDisplayVariable { name: name.clone() }) else {
        panic!("a display variable toggles");
    };
    let IntentReceipt::Accepted { request: again, .. } = sidebar.apply_intent(Intent::ToggleDisplayVariable { name }) else {
        panic!("and toggles back");
    };
    let delta = sidebar.poll(id).unwrap().expect("accepted intents are listed");
    assert_eq!(delta.intents, [request, again]);
    assert!(delta.ops.is_empty(), "toggled and back presents nothing new: {:?}", delta.ops);
}

/// An accepted intent that changes nothing at all, not even the revision
/// (activating what is already opening), is still listed.
#[test]
fn an_intent_that_changes_nothing_is_listed() {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.apply(1, PROVIDER, catalog(20)).unwrap();
    let id = sidebar.subscribe();
    sidebar.poll(id).unwrap().unwrap();
    let project = EntityRef::new(PROVIDER, "project", "fleet/p0@fleet");
    let IntentReceipt::Accepted { effects, .. } = sidebar.apply_intent(Intent::Activate { entity: project.clone() }) else {
        panic!("a project opens");
    };
    assert!(matches!(effects.as_slice(), [HostEffect::Materialize { .. }]), "{effects:?}");
    sidebar.poll(id).unwrap().unwrap();
    let revision = sidebar.revision();
    let IntentReceipt::Accepted { request, effects } = sidebar.apply_intent(Intent::Activate { entity: project }) else {
        panic!("activating it again is accepted");
    };
    assert!(effects.is_empty() && sidebar.revision() == revision, "and changes nothing");
    assert!(sidebar.changed_since(id).unwrap());
    assert_eq!(sidebar.poll(id).unwrap().expect("it is listed").intents, [request]);
}

/// List operations turn any list of distinct keys into any other, and move
/// only keys outside the longest run already in order, each at most twice.
#[hegel::test(test_cases = 1000)]
fn list_ops_turn_one_list_into_another(tc: TestCase) {
    let distinct = |keys: Vec<u8>| {
        let mut seen = std::collections::HashSet::new();
        keys.into_iter().filter(|key| seen.insert(*key)).collect::<Vec<_>>()
    };
    let keys = || gs::vecs(gs::integers::<u8>().max_value(30)).max_size(25);
    let before = distinct(tc.draw(keys()));
    let after = distinct(tc.draw(keys()));
    let ops = list_ops(&before, &after);
    let mut list = before.clone();
    apply_list(&mut list, &ops).unwrap();
    assert_eq!(list, after);
    // The kept keys' old positions, in their new order; those outside its
    // longest increasing run are what has to move.
    let old: Vec<usize> = after.iter().filter_map(|key| before.iter().position(|k| k == key)).collect();
    let mut run = vec![1; old.len()];
    for i in 0..old.len() {
        for j in 0..i {
            if old[j] < old[i] {
                run[i] = run[i].max(run[j] + 1);
            }
        }
    }
    let moving = old.len() - run.iter().copied().max().unwrap_or(0);
    let moves = ops.iter().filter(|op| matches!(op, ListOp::Move { .. })).count();
    assert!(moves <= 2 * moving, "{moves} moves for {moving} keys out of order");
    let inserts = ops.iter().filter(|op| matches!(op, ListOp::Insert { .. })).count();
    assert_eq!(inserts, after.len() - old.len());
    let removes = ops.iter().filter(|op| matches!(op, ListOp::Remove { .. })).count();
    assert_eq!(removes, before.len() - old.len());
}

/// A list op past a list's end fails rather than panicking.
#[test]
fn an_out_of_range_list_op_fails() {
    let mut list = vec![1, 2];
    assert!(apply_list(&mut list, &[ListOp::Move { from: 2, to: 0 }]).is_err());
    assert!(apply_list(&mut list, &[ListOp::Insert { at: 3, key: 9 }]).is_err());
    assert!(apply_list(&mut list, &[ListOp::Remove { at: 2 }]).is_err());
}

/// One vessel's status changing in a fleet-sized catalog is a delta of a
/// few operations: the walk skips everything that didn't change.
#[test]
fn a_small_patch_at_fleet_scale_is_a_small_delta() {
    for config in [CONFIG, DAILY_DRIVER] {
        let mut sidebar = Sidebar::new(config).unwrap();
        sidebar.apply(1, PROVIDER, catalog(580)).unwrap();
        let id = sidebar.subscribe();
        let first = sidebar.poll(id).unwrap().unwrap();
        assert!(first.ops.len() > nodes(&sidebar).len(), "the first poll gives everything");
        for (i, status) in ["running", "done", "running"].into_iter().enumerate() {
            sidebar.apply(2 + i as u64, PROVIDER, [change("vessel", 5, vec![("status.state", text(status))], &[])]).unwrap();
            // Evaluate first, to time only the walk.
            sidebar.snapshot_shared();
            let started = std::time::Instant::now();
            let delta = sidebar.poll(id).unwrap().expect("a status change presents something");
            let elapsed = started.elapsed();
            assert!(delta.ops.len() <= 12, "a status change gave {} operations: {:?}", delta.ops.len(), delta.ops);
            println!("status change: {} operations, walked in {elapsed:?}", delta.ops.len());
        }
    }
}

/// A change to what nothing presents (the fixture shows no convoy's phase)
/// moves the revision but delivers nothing.
#[test]
fn a_change_nothing_presents_delivers_nothing() {
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.apply(1, PROVIDER, catalog(60)).unwrap();
    let id = sidebar.subscribe();
    sidebar.poll(id).unwrap().unwrap();
    sidebar.apply(2, PROVIDER, [crate::scaling_tests::delta(5, "landed")]).unwrap();
    assert!(sidebar.changed_since(id).unwrap(), "the revision moved");
    assert!(sidebar.poll(id).unwrap().is_none());
    assert!(!sidebar.changed_since(id).unwrap(), "and the poll caught up with it");
}

/// A connector that reconnects resends its whole catalog. When what it
/// sends is what it had, followers are given nothing.
#[test]
fn a_reconnect_resending_the_same_catalog_delivers_nothing() {
    for config in [CONFIG, DAILY_DRIVER] {
        let mut sidebar = Sidebar::new(config).unwrap();
        sidebar.apply(1, PROVIDER, catalog(120)).unwrap();
        let id = sidebar.subscribe();
        sidebar.poll(id).unwrap().unwrap();
        sidebar.retract_provider(PROVIDER);
        sidebar.apply(2, PROVIDER, catalog(120)).unwrap();
        let delta = sidebar.poll(id).unwrap();
        assert!(delta.is_none(), "a reconnect gave {} operations", delta.map_or(0, |delta| delta.ops.len()));
    }
}

/// Typed presented values come from `presents` fields, not from the flat
/// fields, and the first field presenting a value wins.
#[test]
fn presents_fields_fill_typed_values() {
    let config = r#"
        region "tree" root-template="title" form="compact" placement="tree"
        template "title" slot="compact" node-kind="entity" { field "label" source="literal" value="Tree"; }
        placement "tree" {
          for "convoy" kind="convoy" { apply-template "row"; }
        }
        template "row" slot="compact" node-kind="entity" {
          field "label" key="display.label"
          field "icon" key="presentation.icon" presents="icon"
          field "icon-fallback" source="literal" value="review" presents="icon"
          field "status" key="status.state" presents="status" prefix="ignored:"
          field "attention" key="status.attention" presents="attention"
          field "short" source="metadata-text" key="display.label.short" presents="short-label"
          field "medium" source="metadata-text" key="display.label.medium" presents="medium-label"
        }
    "#;
    let mut sidebar = Sidebar::new(config).unwrap();
    let mut facts = crate::scaling_tests::labels("Convoy");
    facts.extend([("status.state".into(), text("running")), ("status.attention".into(), MetadataValue::Bool(true))]);
    sidebar.apply(1, PROVIDER, [patch("convoy", "c", facts)]).unwrap();
    let node = sidebar.snapshot_shared().surface.sections[0].nodes[0].clone();
    let presented = &node.content.presented;
    assert_eq!(presented.icon.as_deref(), Some("review"), "the fallback icon fills an unset one");
    assert_eq!(presented.status.as_deref(), Some("running"), "a presented value has no prefix");
    assert!(presented.attention);
    assert_eq!((presented.short_label.as_deref(), presented.medium_label.as_deref()), (Some("C"), Some("Convoy")));
    let values: Vec<&str> = node.content.fields.iter().map(|field| field.value.as_str()).collect();
    assert_eq!(values, ["Convoy"], "presents fields stay out of the flat fields");
    sidebar
        .apply(
            2,
            PROVIDER,
            [{
                let mut icon = patch("convoy", "c", vec![("presentation.icon".into(), text("terminal"))]);
                icon.set.retain(|key, _| key == "presentation.icon");
                icon
            }],
        )
        .unwrap();
    let node = &sidebar.snapshot_shared().surface.sections[0].nodes[0];
    assert_eq!(node.content.presented.icon.as_deref(), Some("terminal"), "the producer's icon comes first");
}

/// Whether a subject has a URL is its node's content: forge facts that
/// arrive after the subject's give its node a URL by an Upsert, and the
/// snapshot and the stream agree on it.
#[test]
fn a_url_that_arrives_later_reaches_the_stream() {
    let fixture: Vec<MetadataPatch> =
        include_str!("../../../data/sidebar/fixture.jsonl").lines().map(|line| serde_json::from_str(line).unwrap()).collect();
    let is_forge = |patch: &MetadataPatch| patch.set.contains_key("flotilla.forge.web_url");
    let (forge, rest): (Vec<_>, Vec<_>) = fixture.into_iter().partition(is_forge);
    assert_eq!(forge.len(), 1, "the fixture has one forge");
    let mut sidebar = Sidebar::new(DAILY_DRIVER).unwrap();
    sidebar.apply(1, "fixture", rest).unwrap();
    let id = sidebar.subscribe();
    let mut followed = StreamView::default();
    followed.apply(&sidebar.poll(id).unwrap().unwrap()).unwrap();
    let with_url = |view: &StreamView| view.nodes.values().filter(|n| n.has_url).count();
    assert_eq!(with_url(&followed), 0);
    sidebar.apply(2, "fixture", forge).unwrap();
    let delta = sidebar.poll(id).unwrap().expect("the forge gave subjects URLs");
    followed.apply(&delta).unwrap();
    assert!(with_url(&followed) > 0);
    assert_eq!(followed, StreamView::of(sidebar.snapshot_shared()));
}
