use super::*;
use crate::suggested_layout::{CommandLine, LocalRecipe};
use crate::EntityRef;

fn shell(command: &str) -> ViewSpec {
    ViewSpec {
        content: Content::Local(LocalRecipe::Command { line: CommandLine::Shell(command.into()), cwd: None }),
        presentation: None,
        role: None,
    }
}

fn facet(id: &str) -> ViewSpec {
    ViewSpec {
        content: Content::ProviderFacet { entity: EntityRef::new("sub-1", "vessel", id), facet: "terminal".into() },
        presentation: Some("terminal".into()),
        role: None,
    }
}

fn def(key: &str, spec: ViewSpec) -> SlotDef {
    SlotDef { key: key.into(), spec, rebind: RebindPolicy::Replace }
}

fn baseline(keys: &[&str]) -> Baseline {
    Baseline {
        version: BaselineVersion::Published(keys.join(",")),
        slots: keys.iter().map(|key| def(key, facet(key))).collect(),
        hint: None,
    }
}

fn tabs(id: &str, keys: &[&str], selected: Option<&str>) -> Panel {
    Panel {
        id: id.into(),
        weight: 1.0,
        node: PanelNode::Tabs { tabs: keys.iter().map(|k| k.to_string()).collect(), selected: selected.map(str::to_owned) },
    }
}

fn split(id: &str, children: Vec<Panel>) -> Panel {
    Panel { id: id.into(), weight: 1.0, node: PanelNode::Split { axis: Axis::Row, children } }
}

fn doc(root: Panel) -> ArrangementDoc {
    ArrangementDoc { root: Some(root) }
}

#[test]
fn user_slots_overrides_and_reattachment() {
    let mut slots = WorkspaceSlots { baseline: Some(baseline(&["a", "b"])), ..Default::default() };
    // Provider keys can't be invented; user keys live in their namespace.
    for (key, spec, reason) in [
        ("c", shell("x"), "no slot \"c\""),
        ("u:", shell("x"), "invalid user slot key"),
        ("u:A", shell("x"), "invalid user slot key"),
        ("u:1", shell(""), "a view spec needs a command"),
    ] {
        let error = slots.set(key, spec, RebindPolicy::Replace).unwrap_err();
        assert!(error.contains(reason), "{key}: {error}");
    }
    assert!(slots.set("u:1", shell("x"), RebindPolicy::Ask).unwrap());
    assert!(!slots.set("u:1", shell("x"), RebindPolicy::Ask).unwrap());
    // Overriding a baseline slot detaches it.
    assert!(slots.set("b", shell("y"), RebindPolicy::Replace).unwrap());
    let all = slots.slots();
    let keys: Vec<_> = all.iter().map(|s| s.key.as_str()).collect();
    assert_eq!(keys, ["a", "b", "u:1"]);
    assert!(all[1].detached && all[1].in_baseline);
    assert_eq!(all[1].spec, shell("y"));
    assert_eq!(slots.edits["b"].content.as_ref().unwrap().against, facet("b"));
    assert!(!all[2].detached && !all[2].in_baseline);
    // Setting the baseline's own spec reattaches it.
    assert!(slots.set("b", facet("b"), RebindPolicy::Replace).unwrap());
    assert!(!slots.slot("b").unwrap().detached);
    assert!(slots.set("b", shell("y"), RebindPolicy::Replace).unwrap());
    assert!(slots.reattach("b"));
    assert!(!slots.reattach("b"));
    // Removing a baseline slot tombstones it; reattaching shows it again.
    assert!(slots.remove("a").unwrap());
    assert!(!slots.remove("a").unwrap());
    assert!(!slots.keys().contains("a"));
    assert_eq!(slots.hidden(), BTreeSet::from(["a".to_owned()]));
    assert!(slots.reattach("a"));
    assert!(slots.keys().contains("a"));
    assert!(slots.remove("u:1").unwrap());
    assert!(!slots.remove("u:1").unwrap());
}

#[test]
fn a_detached_slot_the_baseline_drops_is_kept_as_the_users() {
    let mut slots = WorkspaceSlots { baseline: Some(baseline(&["a", "b"])), ..Default::default() };
    slots.set("b", shell("y"), RebindPolicy::Replace).unwrap();
    assert!(slots.set_baseline(baseline(&["a"])));
    let kept = slots.slot("b").unwrap();
    assert!(kept.detached && !kept.in_baseline);
    // It can still be edited, can't be reattached, and can be removed.
    assert!(slots.set("b", shell("z"), RebindPolicy::Replace).unwrap());
    assert!(!slots.reattach("b"));
    assert!(slots.remove("b").unwrap());
    assert_eq!(slots.keys(), BTreeSet::from(["a".to_owned()]));
}

#[test]
fn documents_are_checked_whole() {
    let ok = doc(split("1", vec![tabs("2", &["a"], Some("a")), tabs("3", &[], None)]));
    assert_eq!(ok.check(), Ok(()));
    let mut bad = vec![
        (doc(split("1", vec![tabs("1", &["a"], None)])), "panel ID \"1\" is used twice"),
        (doc(split("1", vec![])), "split \"1\" has no panels"),
        (doc(split("1", vec![tabs("2", &["a"], None), tabs("3", &["a"], None)])), "slot \"a\" has two tabs"),
        (doc(tabs("1", &["a"], Some("b"))), "selects \"b\", which is not one of its tabs"),
        (doc(tabs("", &["a"], None)), "a panel needs an ID"),
        (doc(tabs("1", &[""], None)), "has a tab with no slot"),
    ];
    for weight in [0.0, -1.0, f64::NAN, f64::INFINITY] {
        let mut panel = tabs("1", &["a"], None);
        panel.weight = weight;
        bad.push((doc(panel), "weights are positive"));
    }
    for (doc, reason) in bad {
        let error = doc.check().unwrap_err();
        assert!(error.contains(reason), "{doc:?}: {error}");
    }
}

#[test]
fn invalid_commits_change_nothing() {
    let slots = WorkspaceSlots { baseline: Some(baseline(&["a", "b"])), ..Default::default() };
    let infos = slots.slots();
    let mut stored = None;
    let first = doc(tabs("1", &["a", "b"], Some("a")));
    let placed = Arrangement::commit(&mut stored, first, slots.baseline.as_ref(), &infos, None).unwrap();
    assert!(placed.is_empty());
    let before = stored.clone();
    // A tab for a slot that never existed, or a malformed document.
    for (invalid, reason) in
        [(doc(tabs("1", &["a", "b", "nope"], None)), "no slot \"nope\""), (doc(tabs("1", &["a", "a"], None)), "slot \"a\" has two tabs")]
    {
        let error = Arrangement::commit(&mut stored, invalid, slots.baseline.as_ref(), &infos, None).unwrap_err();
        assert!(error.contains(reason), "{error}");
        assert_eq!(stored, before);
    }
}

fn arranged(stored: &Option<Arrangement>) -> &Arrangement {
    stored.as_ref().expect("stored")
}

#[test]
fn reconciling_places_new_slots_and_reports_gone_ones() {
    let mut slots = WorkspaceSlots { baseline: Some(baseline(&["a", "b"])), ..Default::default() };
    let mut stored = None;
    // The host left b out: it is placed in the first tab panel.
    let host = doc(split("1", vec![split("2", vec![tabs("3", &[], None)]), tabs("4", &["a"], Some("a"))]));
    let placed = Arrangement::commit(&mut stored, host, slots.baseline.as_ref(), &slots.slots(), None).unwrap();
    assert_eq!(placed, ["b"]);
    assert_eq!(arranged(&stored).doc.tabs(), ["b", "a"]);
    assert_eq!(arranged(&stored).placed, BTreeSet::from(["b".to_owned()]));
    let PanelNode::Tabs { selected, .. } = &arranged(&stored).doc.panels()[2].node else {
        panic!("expected tabs");
    };
    assert_eq!(selected.as_deref(), Some("b"));

    // The baseline adds c and drops a: c is placed, a's tab stays and is
    // reported gone.
    slots.set_baseline(baseline(&["b", "c"]));
    let infos = slots.slots();
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &infos, None);
    assert_eq!(arranged(&stored).doc.tabs(), ["b", "c", "a"]);
    assert_eq!(arranged(&stored).gone(&infos), BTreeSet::from(["a".to_owned()]));
    assert_eq!(arranged(&stored).placed, BTreeSet::from(["b".to_owned(), "c".to_owned()]));
    // Following an unchanged slot set changes nothing.
    let before = stored.clone();
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &infos, None);
    assert_eq!(stored, before);
    // The host may keep the gone tab in its commit; it may not add one.
    let host = doc(tabs("1", &["c", "b", "a"], Some("c")));
    let placed = Arrangement::commit(&mut stored, host, slots.baseline.as_ref(), &infos, None).unwrap();
    assert!(placed.is_empty());
    assert_eq!(arranged(&stored).gone(&infos), BTreeSet::from(["a".to_owned()]));
    assert!(arranged(&stored).placed.is_empty(), "the host has seen every tab");
}

#[test]
fn an_uncommitted_arrangement_follows_the_hint() {
    let mut slots = WorkspaceSlots::default();
    let mut hinted = baseline(&["a", "b"]);
    hinted.hint = Some(doc(tabs("main", &["a"], Some("a"))));
    slots.set_baseline(hinted);
    let mut stored = None;
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &slots.slots(), None);
    assert!(!arranged(&stored).owned);
    assert_eq!(arranged(&stored).doc.tabs(), ["a", "b"]);
    // A new hint replaces it while the host hasn't committed.
    let mut hinted = baseline(&["a", "b"]);
    hinted.hint = Some(doc(split("main", vec![tabs("x", &["b"], None), tabs("y", &["a"], None)])));
    slots.set_baseline(hinted);
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &slots.slots(), None);
    assert_eq!(arranged(&stored).doc.tabs(), ["b", "a"]);
    assert!(arranged(&stored).placed.is_empty());
    // Once committed, the host owns it: a new hint is not applied.
    Arrangement::commit(&mut stored, doc(tabs("1", &["a", "b"], None)), slots.baseline.as_ref(), &slots.slots(), None).unwrap();
    assert!(arranged(&stored).owned);
    let mut hinted = baseline(&["a", "b"]);
    hinted.hint = Some(doc(tabs("main", &["b", "a"], None)));
    slots.set_baseline(hinted);
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &slots.slots(), None);
    assert_eq!(arranged(&stored).doc.tabs(), ["a", "b"]);
}

#[test]
fn hints_convert_from_suggested_layouts() {
    let hint = suggested_layout::Arrangement {
        root: "main".into(),
        panels: BTreeMap::from([
            (
                "main".into(),
                suggested_layout::Panel {
                    weight: 1,
                    node: suggested_layout::PanelNode::Split { axis: Axis::Column, children: vec!["x".into()] },
                },
            ),
            (
                "x".into(),
                suggested_layout::Panel {
                    weight: 3,
                    node: suggested_layout::PanelNode::Tabs { slots: vec!["a".into(), "b".into()], selected: "b".into() },
                },
            ),
        ]),
    };
    let mut expected = tabs("x", &["a", "b"], Some("b"));
    expected.weight = 3.0;
    assert_eq!(
        ArrangementDoc::from_hint(&hint),
        doc(Panel { id: "main".into(), weight: 1.0, node: PanelNode::Split { axis: Axis::Column, children: vec![expected] } })
    );
}

fn weighted(mut panel: Panel, weight: f64) -> Panel {
    panel.weight = weight;
    panel
}

fn weights(doc: &ArrangementDoc) -> Vec<(String, f64)> {
    doc.panels().into_iter().skip(1).map(|panel| (panel.id.clone(), panel.weight)).collect()
}

fn hinted(hint: Panel, keys: &[&str]) -> Baseline {
    let mut hinted = baseline(keys);
    hinted.hint = Some(doc(hint));
    hinted
}

// #477: the host shows weights as shares of their split (3:2 as 0.6 and
// 0.4) and commits them so. A commit that only moves a selection records no
// weight override, and the stored document keeps its own scale; a real drag
// is an override in that scale.
#[test]
fn a_commit_in_shares_is_read_in_the_documents_scale() {
    let hint = split("main", vec![weighted(tabs("x", &["a", "c"], Some("a")), 3.0), weighted(tabs("y", &["b"], Some("b")), 2.0)]);
    let slots = WorkspaceSlots { baseline: Some(hinted(hint, &["a", "b", "c"])), ..Default::default() };
    let mut stored = None;
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &slots.slots(), None);
    let shares = |x: f64, y: f64| {
        doc(split("main", vec![weighted(tabs("x", &["a", "c"], Some("c")), x), weighted(tabs("y", &["b"], Some("b")), y)]))
    };
    Arrangement::commit(&mut stored, shares(0.6, 0.4), slots.baseline.as_ref(), &slots.slots(), None).unwrap();
    let arrangement = arranged(&stored);
    assert!(!arrangement.owned);
    assert_eq!(arrangement.soft, BTreeMap::from([("x".to_owned(), SoftOverride { weight: None, selected: Some("c".into()) })]));
    assert_eq!(weights(&arrangement.doc), [("x".to_owned(), 3.0), ("y".to_owned(), 2.0)]);
    // The host's shares are quantized: near enough is unchanged.
    Arrangement::commit(&mut stored, shares(0.60001, 0.39999), slots.baseline.as_ref(), &slots.slots(), None).unwrap();
    assert!(arranged(&stored).soft["x"].weight.is_none());
    // A drag to halves is an override of each, in the document's scale.
    Arrangement::commit(&mut stored, shares(0.5, 0.5), slots.baseline.as_ref(), &slots.slots(), None).unwrap();
    let arrangement = arranged(&stored);
    assert_eq!((arrangement.soft["x"].weight, arrangement.soft["y"].weight), (Some(2.5), Some(2.5)));
    // Following again keeps the halves.
    Arrangement::follow(&mut stored, slots.baseline.as_ref(), &slots.slots(), None);
    assert_eq!(weights(&arranged(&stored).doc), [("x".to_owned(), 2.5), ("y".to_owned(), 2.5)]);
}

// A tab panel with no tabs takes no space in what the host is shown while a
// sibling has tabs; the document keeps it, at its weight, for slots to land
// in. The host's commit of it at no weight changes nothing, and once it has
// a tab it is shown at its weight again.
#[test]
fn an_empty_panel_is_shown_without_space() {
    let hint =
        |files: &[&str]| split("main", vec![weighted(tabs("agents", &["a"], Some("a")), 3.0), weighted(tabs("files", files, None), 2.0)]);
    let mut slots = WorkspaceSlots { baseline: Some(hinted(hint(&[]), &["a"])), ..Default::default() };
    let mut stored = StoredArrangement::default();
    let mut clock = crate::versioned::Clock::default();
    stored
        .edit(&mut clock, |a| {
            Arrangement::follow(a, slots.baseline.as_ref(), &slots.slots(), None);
            Ok(())
        })
        .unwrap();
    let view = Arrangement::view(&stored, &slots.slots(), None);
    assert_eq!(weights(&view.doc), [("agents".to_owned(), 3.0), ("files".to_owned(), 0.0)]);
    // The host shows agents whole and commits it so.
    let host = doc(split("main", vec![weighted(tabs("agents", &["a"], Some("a")), 1.0), weighted(tabs("files", &[], None), 0.0)]));
    stored.edit(&mut clock, |a| Arrangement::commit(a, host, slots.baseline.as_ref(), &slots.slots(), None).map(|_| ())).unwrap();
    let arrangement = stored.get().as_ref().unwrap();
    assert!(!arrangement.owned && arrangement.soft.is_empty(), "{arrangement:?}");
    assert_eq!(weights(&arrangement.doc), [("agents".to_owned(), 3.0), ("files".to_owned(), 2.0)]);
    // A slot lands in it: it is shown at its weight again.
    slots.set_baseline(hinted(hint(&["f"]), &["a", "f"]));
    stored
        .edit(&mut clock, |a| {
            Arrangement::follow(a, slots.baseline.as_ref(), &slots.slots(), None);
            Ok(())
        })
        .unwrap();
    let view = Arrangement::view(&stored, &slots.slots(), None);
    assert_eq!(weights(&view.doc), [("agents".to_owned(), 3.0), ("files".to_owned(), 2.0)]);
}

// Empty panels with no sibling to give their space to keep their weights.
#[test]
fn panels_that_are_all_empty_keep_their_weights() {
    let shown = doc(split("main", vec![weighted(tabs("x", &[], None), 3.0), weighted(tabs("y", &[], None), 2.0)])).shown(|_| true);
    assert_eq!(weights(&shown), [("x".to_owned(), 3.0), ("y".to_owned(), 2.0)]);
    let nested = doc(split("main", vec![tabs("x", &["a"], Some("a")), split("s", vec![tabs("p", &[], None), tabs("q", &[], None)])]))
        .shown(|_| true);
    assert_eq!(weights(&nested), [("x".to_owned(), 1.0), ("s".to_owned(), 0.0), ("p".to_owned(), 1.0), ("q".to_owned(), 1.0)]);
    // Only the panels that collapse.
    let some = doc(split("main", vec![tabs("x", &["a"], Some("a")), tabs("p", &[], None), tabs("q", &[], None)])).shown(|id| id == "p");
    assert_eq!(weights(&some), [("x".to_owned(), 1.0), ("p".to_owned(), 0.0), ("q".to_owned(), 1.0)]);
}

// An empty panel committed at none gets its stored weight back, even with no
// sibling left with tabs (it was shown so until the last tab went); one the
// host gives a weight keeps it.
#[test]
fn an_empty_panel_committed_at_none_gets_its_weight_back() {
    let stored = doc(split("main", vec![weighted(tabs("x", &[], None), 3.0), weighted(tabs("y", &[], None), 2.0)]));
    let mut committed = doc(split("main", vec![weighted(tabs("x", &[], None), 0.5), weighted(tabs("y", &[], None), 0.0)]));
    committed.rescale_to(&stored, false);
    assert_eq!(weights(&committed), [("x".to_owned(), 0.5), ("y".to_owned(), 2.0 / 6.0)]);
    let mut committed = doc(split("main", vec![weighted(tabs("x", &[], None), 0.0), weighted(tabs("y", &[], None), 0.0)]));
    committed.rescale_to(&stored, false);
    assert_eq!(weights(&committed), [("x".to_owned(), 3.0), ("y".to_owned(), 2.0)]);
    committed.check().unwrap();
    // A host showing a slot in a panel it was shown at none gives it the tab
    // and commits the 0 it had.
    let mut committed = doc(split("main", vec![weighted(tabs("x", &["a"], Some("a")), 1.0), weighted(tabs("y", &["b"], Some("b")), 0.0)]));
    committed.rescale_to(&stored, false);
    assert_eq!(weights(&committed), [("x".to_owned(), 1.0), ("y".to_owned(), 2.0 / 3.0)]);
    committed.check().unwrap();
}
