//! The tool strip (docs/core/sidebar-design/tool-strip.md): a subject kind's
//! `strip` lists the subject's related workspaces and linked items by
//! placement loops, with the subject bound by its kind's name.
mod common;
use common::{entity, patch, refs, text, worker, Feed, CONFIG};
use wheelhouse_core::sidebar::{Action, HostEffect, Workspace};
use wheelhouse_core::tool_strip::{StripEntry, StripSection};
use wheelhouse_core::{EntityRef, Sidebar};

const STRIP: &str = r#"
workspace kind="project"
workspace kind="convoy" {
  strip {
    related {
      for "project" kind="project" in="flotilla.project" of="convoy";
      for "vessel" kind="vessel" { match "flotilla.convoy" of="convoy"; order "display.label"; };
    };
    linked {
      for "change_request" kind="change_request" { match "flotilla.subject_of" of="convoy"; order "flotilla.subject.number" natural=true; };
      for "issue" kind="issue" { match "flotilla.subject_of" of="convoy"; };
    };
  }
}
workspace kind="vessel" {
  strip {
    related {
      for "project" kind="project" in="flotilla.project" of="vessel";
      for "convoy" kind="convoy" in="flotilla.convoy" of="vessel";
    };
    linked { for "issue" kind="issue" in="test.tickets" of="vessel"; };
  }
}
"#;

fn sidebar() -> Sidebar {
    let mut sidebar = Sidebar::new(&format!("{CONFIG}\n{STRIP}")).unwrap();
    let label = |value: &str| ("display.label", text(value));
    sidebar.feed(
        100,
        [
            patch(entity("project", "p"), &[label("P")]),
            patch(entity("convoy", "c"), &[label("C"), ("flotilla.project", refs(entity("project", "p")))]),
            patch(entity("convoy", "other"), &[label("Other"), ("flotilla.project", refs(entity("project", "p")))]),
            vessel("v1", "Bravo", "c"),
            vessel("v2", "Alpha", "c"),
            vessel("v3", "Charlie", "other"),
            subject_of("change_request", "pr10", "10", "c"),
            subject_of("change_request", "pr9", "9", "c"),
            subject_of("issue", "i1", "1", "c"),
            subject_of("issue", "i2", "2", "other"),
        ],
    );
    sidebar
}

fn vessel(id: &str, label: &str, convoy: &str) -> wheelhouse_core::MetadataPatch {
    patch(
        entity("vessel", id),
        &[
            ("display.label", text(label)),
            ("flotilla.project", refs(entity("project", "p"))),
            ("flotilla.convoy", refs(entity("convoy", convoy))),
        ],
    )
}

fn subject_of(kind: &str, id: &str, number: &str, convoy: &str) -> wheelhouse_core::MetadataPatch {
    patch(entity(kind, id), &[("flotilla.subject.number", text(number)), ("flotilla.subject_of", refs(entity("convoy", convoy)))])
}

fn listed(sidebar: &Sidebar, subject: EntityRef) -> Vec<(StripSection, String)> {
    sidebar
        .strip(&subject)
        .into_iter()
        .map(|StripEntry { section, entity, .. }| (section, format!("{}/{}", entity.kind, entity.id)))
        .collect()
}

fn related(id: &str) -> (StripSection, String) {
    (StripSection::Related, id.to_owned())
}

fn linked(id: &str) -> (StripSection, String) {
    (StripSection::Linked, id.to_owned())
}

#[test]
fn a_convoy_lists_its_project_vessels_and_linked_items_in_order() {
    let sidebar = sidebar();
    assert_eq!(
        listed(&sidebar, entity("convoy", "c")),
        [
            related("project/p"),
            related("vessel/v2"),
            related("vessel/v1"),
            linked("change_request/pr9"),
            linked("change_request/pr10"),
            linked("issue/i1"),
        ],
        "ancestors, then children by label; change requests by number before issues; the other convoy's are not listed"
    );
}

#[test]
fn a_vessel_lists_its_ancestors_and_not_its_siblings() {
    let sidebar = sidebar();
    assert_eq!(listed(&sidebar, entity("vessel", "v1")), [related("project/p"), related("convoy/c")]);
}

#[test]
fn the_strip_follows_the_facts() {
    let mut sidebar = sidebar();
    sidebar.feed(200, [vessel("v4", "Delta", "c"), vessel("v1", "Bravo", "other")]);
    let vessels: Vec<_> = listed(&sidebar, entity("convoy", "c")).into_iter().filter(|(_, id)| id.starts_with("vessel/")).collect();
    assert_eq!(vessels, [related("vessel/v2"), related("vessel/v4")], "v1 moved to the other convoy, v4 joined");
    assert_eq!(listed(&sidebar, entity("vessel", "v1")), [related("project/p"), related("convoy/other")]);
}

#[test]
fn a_kind_without_a_strip_or_an_unknown_subject_lists_nothing() {
    let sidebar = sidebar();
    assert_eq!(listed(&sidebar, entity("project", "p")), []);
    assert_eq!(listed(&sidebar, entity("convoy", "missing")), []);
    assert_eq!(listed(&sidebar, entity("change_request", "pr9")), []);
}

#[test]
fn an_entity_two_loops_match_is_listed_once_where_it_first_appears() {
    let strip = r#"
workspace kind="project"
workspace kind="convoy" {
  strip {
    related {
      for "project" kind="project" in="flotilla.project" of="convoy";
      for "again" kind="project" in="flotilla.project" of="convoy";
    };
    linked {
      for "project" kind="project" in="flotilla.project" of="convoy";
      for "issue" kind="issue" { match "flotilla.subject_of" of="convoy"; };
      for "again" kind="issue" { match "flotilla.subject_of" of="convoy"; };
    };
  }
}
"#;
    let mut sidebar = Sidebar::new(&format!("{CONFIG}\n{strip}")).unwrap();
    sidebar.feed(
        100,
        [
            patch(entity("project", "p"), &[("display.label", text("P"))]),
            patch(entity("convoy", "c"), &[("display.label", text("C")), ("flotilla.project", refs(entity("project", "p")))]),
            subject_of("issue", "i1", "1", "c"),
        ],
    );
    assert_eq!(listed(&sidebar, entity("convoy", "c")), [related("project/p"), linked("issue/i1")]);
}

#[test]
fn a_strip_read_before_any_snapshot_matches_one_read_after() {
    // The strip builds the placement index itself when no snapshot has.
    let cold = listed(&sidebar(), entity("convoy", "c"));
    let warm = sidebar();
    let _ = warm.snapshot();
    assert_eq!(cold, listed(&warm, entity("convoy", "c")));
    assert_eq!(cold.len(), 6);
}

#[test]
fn an_entity_without_the_ref_lists_nothing_for_it() {
    let mut sidebar = sidebar();
    // A vessel that names no convoy has no convoy to list.
    sidebar.feed(200, [patch(entity("vessel", "loose"), &[("flotilla.project", refs(entity("project", "p")))])]);
    assert_eq!(listed(&sidebar, entity("vessel", "loose")), [related("project/p")]);
}

#[test]
fn invalid_strips_are_rejected() {
    let cases = [
        ("a property on strip", r#"workspace kind="convoy" { strip open=true { }; }"#),
        ("an unknown section", r#"workspace kind="convoy" { strip { pinned { }; }; }"#),
        ("a section twice", r#"workspace kind="convoy" { strip { related { }; related { }; }; }"#),
        ("strip twice", r#"workspace kind="convoy" { strip { }; strip { }; }"#),
        ("a field in a section", r#"workspace kind="convoy" { strip { related { field "x" key="y"; }; }; }"#),
        (
            "a nested loop",
            r#"workspace kind="convoy" { strip { related { for "vessel" kind="vessel" { for "issue" kind="issue"; }; }; }; }"#,
        ),
        ("a field in a loop", r#"workspace kind="convoy" { strip { related { for "vessel" kind="vessel" { field "x" key="y"; }; }; }; }"#),
        ("an unbound of", r#"workspace kind="convoy" { strip { related { for "vessel" kind="vessel" { match "k" of="vessel"; }; }; }; }"#),
        ("the subject's own binding", r#"workspace kind="convoy" { strip { related { for "convoy" kind="convoy"; }; }; }"#),
        (
            "an undeclared visibility policy",
            r#"workspace kind="convoy" { strip { related { for "vessel" kind="vessel" visibility="nope"; }; }; }"#,
        ),
    ];
    for (what, config) in cases {
        assert!(Sidebar::new(&format!("{CONFIG}\n{config}")).is_err(), "{what} should be rejected");
    }
}

#[test]
fn a_removed_subject_keeps_its_strip_in_its_workspace_and_across_a_restart() {
    let mut sidebar = sidebar();
    let v1 = entity("vessel", "v1");
    sidebar.feed(150, [patch(v1.clone(), &[("test.tickets", refs(entity("issue", "i1")))])]);
    let effects = sidebar.dispatch(Action::Activate { entity: v1.clone() }).unwrap();
    let [HostEffect::Materialize { request_id, .. }] = effects[..] else {
        panic!("expected materialize: {effects:?}");
    };
    sidebar.complete(request_id, Ok(Some(worker())));
    sidebar.observe(vec![Workspace { id: worker(), position: 0, name: "Bravo".into(), selected: true }], vec![]);
    // The provider removes the vessel: its workspace keeps the facts its
    // strip reads, as it keeps those the sidebar places it by.
    let mut removal = vessel("v1", "Bravo", "c");
    removal.unset = removal.set.keys().cloned().chain(["test.tickets".to_owned()]).collect();
    removal.set = [("source".to_owned(), common::update(text("flotilla")))].into();
    sidebar.feed(200, [removal]);
    let kept = [related("project/p"), related("convoy/c"), linked("issue/i1")];
    assert_eq!(listed(&sidebar, v1.clone()), kept);

    // After a restart, the workspace's record holds those facts.
    let name = format!("workspace/{}", common::WORKER);
    let record = sidebar.export_record(&name).unwrap();
    let mut restarted = Sidebar::new(&format!("{CONFIG}\n{STRIP}")).unwrap();
    restarted.feed(
        300,
        [
            patch(entity("project", "p"), &[("display.label", text("P"))]),
            patch(entity("convoy", "c"), &[("display.label", text("C")), ("flotilla.project", refs(entity("project", "p")))]),
            subject_of("issue", "i1", "1", "c"),
        ],
    );
    restarted.observe(vec![Workspace { id: worker(), position: 0, name: "Bravo".into(), selected: true }], vec![]);
    restarted.import_record(&name, &record).unwrap();
    assert_eq!(listed(&restarted, v1), kept, "a key only the strip reads is kept too");
}

/// A sidebar placing convoys, their vessels and their change requests, each
/// kind by its own template (a lead vessel by another, which ours isn't);
/// issues it never places, so they take their kind's compact template. A
/// convoy's strip lists all three.
const CHIPS: &str = r#"
region "tree" root-template="title" form="compact" placement="tree"
template "title" slot="compact" node-kind="entity" { field "label" source="literal" value="Tree"; }
placement "tree" {
  for "convoy" kind="convoy" { apply-template "convoy/row"; }
}
template "convoy/row" {
  field "label" key="display.label"
  for "lead" kind="vessel" { match "flotilla.convoy" of="convoy"; match "test.lead" value="true"; apply-template "lead/row"; }
  for "vessel" kind="vessel" { match "flotilla.convoy" of="convoy"; apply-template "vessel/row"; }
  for "change_request" kind="change_request" { match "flotilla.subject_of" of="convoy"; apply-template "subject/row"; }
}
fragment "chip" {
  field "icon" key="presentation.icon" presents="icon"
  field "attention" key="status.attention" presents="attention"
  field "status" key="status.state" presents="status"
}
template "vessel/row" {
  field "label" key="display.label"
  use "chip"
  field "icon-override" source="literal" value="terminal" presents="icon"
}
template "lead/row" {
  field "label" key="display.label"
  field "icon-override" source="literal" value="gear" presents="icon"
}
template "subject/row" {
  field "label" key="display.label"
  use "chip"
}
template "issue/compact" slot="compact" node-kind="entity" {
  field "label" key="display.label"
  use "chip"
  field "icon-override" source="literal" value="code" presents="icon"
}
workspace kind="convoy" {
  strip {
    related { for "vessel" kind="vessel" { match "flotilla.convoy" of="convoy"; }; };
    linked {
      for "change_request" kind="change_request" { match "flotilla.subject_of" of="convoy"; };
      for "issue" kind="issue" { match "flotilla.subject_of" of="convoy"; };
    };
  }
}
"#;

// Behaviour (#447): a strip entry carries what a chip of its entity presents,
// from the same rules and the same template: the producer's
// `presentation.icon`, else the template's `icon-override`, else none; and
// its `status.state` and `status.attention`. Where the sidebar places the
// entity, the entry agrees with its node; where it never does, the entry
// still has its kind's template's presentation.
//
// The generator draws, for a vessel (template with an override, after a
// loop over lead vessels only, which it isn't), a change
// request (template without one) and an issue (never placed; its kind's
// compact template, with an override), each of: no icon, a symbolic one or
// a literal glyph; no status or one of two; attention unset, false or true.
#[hegel::test(test_cases = 100)]
fn a_strip_entry_presents_its_entity_as_its_chip_does(tc: hegel::TestCase) {
    use hegel::generators as gs;
    use wheelhouse_core::MetadataValue;
    let mut sidebar = Sidebar::new(CHIPS).unwrap();
    let convoy = entity("convoy", "c");
    let mut patches = vec![patch(convoy.clone(), &[("display.label", text("C"))])];
    let mut expected = vec![];
    for (kind, link, override_icon) in [
        ("vessel", "flotilla.convoy", Some("terminal")),
        ("change_request", "flotilla.subject_of", None),
        ("issue", "flotilla.subject_of", Some("code")),
    ] {
        let icon = [None, Some("review"), Some("★")][tc.draw(gs::integers::<usize>().max_value(2))];
        let status = [None, Some("running"), Some("ci_failing")][tc.draw(gs::integers::<usize>().max_value(2))];
        let attention = [None, Some(false), Some(true)][tc.draw(gs::integers::<usize>().max_value(2))];
        let mut facts = vec![("display.label", text(kind)), (link, refs(convoy.clone()))];
        facts.extend(icon.map(|icon| ("presentation.icon", text(icon))));
        facts.extend(status.map(|status| ("status.state", text(status))));
        facts.extend(attention.map(|attention| ("status.attention", MetadataValue::Bool(attention))));
        patches.push(patch(entity(kind, "x"), &facts));
        expected.push((format!("{kind}/x"), icon.or(override_icon).map(str::to_owned), status.map(str::to_owned), attention == Some(true)));
    }
    sidebar.feed(100, patches);
    let strip = sidebar.strip(&convoy);
    let presented: Vec<_> = strip
        .iter()
        .map(|entry| {
            let p = &entry.presented;
            (format!("{}/{}", entry.entity.kind, entry.entity.id), p.icon.clone(), p.status.clone(), p.attention)
        })
        .collect();
    assert_eq!(presented, expected);

    let snapshot = sidebar.snapshot();
    let placed = common::nodes(&snapshot.surface);
    for entry in &strip {
        let node = placed.iter().find(|node| node.entity == entry.entity);
        match entry.entity.kind.as_str() {
            "issue" => assert!(node.is_none(), "the sidebar never places an issue"),
            _ => assert_eq!(Some(&entry.presented), node.map(|node| &node.content.presented), "{} as its chip", entry.entity.kind),
        }
    }
}
