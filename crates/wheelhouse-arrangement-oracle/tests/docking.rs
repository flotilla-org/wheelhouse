//! Equivalence, docking (#516, test 2): the core's docking rule gives the
//! native shell's C rule's verdict for generated registrations (every
//! combination of the traits the rule reads), hosts of both kinds and of no
//! kind, proposals with counts from 0 to 3, and instance counts or none;
//! and restore over a generated window's documents gives the C's
//! documents, dirty marks and moved sections.
#![cfg(oracle)]
use hegel::generators as gs;
use hegel::TestCase;
use wheelhouse_arrangement_oracle::{Docking, Oracle};
use wheelhouse_core::arrangement::generate::{build_step, tabs, Tab};
use wheelhouse_core::arrangement::Arrangement;
use wheelhouse_core::dock::{
    check, closure, creation, placement, registration, restore, traits, view_level, Document, Documents, Host, HostKind, Proposal,
    Registration, View, SIDEBAR_SECTION,
};

/// The traits the rule reads, in every combination.
const RULE_TRAITS: [u32; 6] = [
    traits::CONTENT,
    traits::SECTION,
    traits::SELECTS_WORKSPACES,
    traits::SINGLETON,
    traits::NEEDS_WORKSPACE_SUBJECT,
    traits::CONTROL_SPLIT_SCOPE,
];
/// Controlled Splits, regions and levels: none, the window's, a child
/// workspace's, and the window's own panels' level.
const IDS: [u64; 4] = [0, 7, 20, !7];
const WIDTHS: [f32; 6] = [0.0, 50.0, 199.0, 200.0, 400.0, 3.402_823_5e38];

fn draw_registration(tc: &TestCase, i: usize, sidebar_section: bool) -> Registration {
    let bits = tc.draw(gs::integers::<u32>().max_value((1 << RULE_TRAITS.len()) - 1));
    let traits = RULE_TRAITS.iter().enumerate().filter(|(b, _)| bits & (1 << b) != 0).fold(0, |t, (_, bit)| t | bit);
    Registration {
        name: if sidebar_section { SIDEBAR_SECTION.into() } else { format!("r{i}") },
        traits,
        minimum_width: tc.draw(gs::sampled_from(&[0.0f32, 100.0, 200.0, 300.0][..])),
        default_host: tc.draw(gs::sampled_from(&HostKind::ALL[..])),
    }
}

/// Up to `max` registrations, at most one of them the sidebar's sections.
fn draw_registrations(tc: &TestCase, max: usize) -> Vec<Registration> {
    let count = tc.draw(gs::integers::<usize>().min_value(1).max_value(max));
    let section = tc.draw(gs::integers::<usize>().max_value(count));
    (0..count).map(|i| draw_registration(tc, i, i == section)).collect()
}

fn draw_id(tc: &TestCase) -> u64 {
    tc.draw(gs::sampled_from(&IDS[..]))
}

fn draw_host(tc: &TestCase) -> Host {
    Host {
        kind: tc.draw(gs::sampled_from(&[Some(HostKind::Sidebar), Some(HostKind::WorkspaceRegion), None][..])),
        controlled_split: draw_id(tc),
        workspace_region: draw_id(tc),
        has_workspace_subject: tc.draw(gs::booleans()),
        available_width: tc.draw(gs::sampled_from(&WIDTHS[..])),
        level: draw_id(tc),
    }
}

fn draw_count(tc: &TestCase) -> u32 {
    tc.draw(gs::integers::<u32>().max_value(3))
}

/// A registration index, or (sometimes) none: unregistered content.
fn draw_index(tc: &TestCase, registrations: &[Registration]) -> Option<usize> {
    (tc.draw(gs::integers::<u8>().max_value(5)) != 0).then(|| tc.draw(gs::integers::<usize>().max_value(registrations.len() - 1)))
}

/// Instance counts for each Controlled Split, or no counter.
fn draw_instances(tc: &TestCase) -> Option<Vec<(u64, u32)>> {
    tc.draw(gs::booleans()).then(|| IDS.iter().map(|&id| (id, draw_count(tc))).collect())
}

#[hegel::test(test_cases = 1000)]
fn every_rule_matches_the_c(tc: TestCase) {
    let registrations = draw_registrations(&tc, 6);
    let instances = draw_instances(&tc);
    let count = |split: u64| instances.as_ref().and_then(|i| i.iter().find(|(id, _)| *id == split)).map_or(0, |(_, n)| *n);
    let counter: Option<&dyn Fn(u64) -> u32> = instances.as_ref().map(|_| &count as &dyn Fn(u64) -> u32);
    let c = Docking::new(&registrations, instances.as_deref());
    let index = draw_index(&tc, &registrations);
    let reg = index.map(|i| &registrations[i]);

    let p = Proposal {
        host: draw_host(&tc),
        selected_workspace_region: draw_id(&tc),
        instances_after: draw_count(&tc),
        control_surfaces_after: draw_count(&tc),
        closing: tc.draw(gs::booleans()),
        view_level: draw_id(&tc),
    };
    assert_eq!(check(reg, &p), c.check(index, &p), "check {p:?}");

    let (source, destination) = (draw_host(&tc), draw_host(&tc));
    let exists = tc.draw(gs::integers::<u8>().max_value(7)) != 0;
    let level = if tc.draw(gs::booleans()) { view_level(reg, &source) } else { draw_id(&tc) };
    assert_eq!(view_level(reg, &source), c.view_level(index, &source), "view level in {source:?}");
    let view = View { exists, registration: reg, source, level };
    assert_eq!(
        placement(&view, &destination, counter),
        c.placement(exists, index, &source, level, &destination),
        "placement into {destination:?}"
    );
    assert_eq!(closure(&view), c.closure(exists, index, &source, level), "closure from {source:?}");
    // Creation looks a View up by name, as the shell does.
    let by_name = index.and_then(|i| registration(&registrations, &c.name(i)));
    assert_eq!(creation(by_name, &destination, counter), c.creation(index, &destination), "creation in {destination:?}");
}

#[hegel::test(test_cases = 300)]
fn restore_matches_the_c(tc: TestCase) {
    // Few kinds, so that singletons have copies in valid and invalid
    // places.
    let registrations = draw_registrations(&tc, 3);
    let count = tc.draw(gs::integers::<usize>().min_value(1).max_value(4));
    let mut rust = Documents {
        documents: vec![],
        sidebar: tc.draw(gs::booleans()).then(|| tc.draw(gs::integers::<usize>().max_value(count - 1))),
        workspace: tc.draw(gs::booleans()).then(|| tc.draw(gs::integers::<usize>().max_value(count - 1))),
    };
    let mut oracles: Vec<Oracle> = vec![];
    let mut next_tab: Tab = 1;
    for _ in 0..count {
        let mut a = Arrangement::new();
        let mut c = Oracle::new();
        for _ in 0..tc.draw(gs::integers::<usize>().max_value(10)) {
            let step = build_step(&tc, &a, &mut next_tab);
            step.apply(&mut a);
            match &step {
                wheelhouse_core::arrangement::generate::Step::Edit { edit, hidden } => {
                    c.apply(edit, hidden);
                }
                wheelhouse_core::arrangement::generate::Step::Name { panel, name } => {
                    if a.panel(*panel).is_some() {
                        c.set_named(*panel, *name);
                    }
                }
            }
        }
        // Restore measures no width; a host is mostly the sidebar or a
        // workspace region.
        let host = Host { available_width: WIDTHS[5], ..draw_host(&tc) };
        rust.documents.push(Document { arrangement: a, host, dirty: false });
        oracles.push(c);
    }
    // Each View's kind; a duplicate's is its original's, since a tab is a
    // View. Singletons are common, so restore has copies to remove.
    let mut kinds: Vec<(Tab, Option<usize>)> = vec![];
    for document in &rust.documents {
        for tab in tabs(&document.arrangement) {
            if !kinds.iter().any(|(t, _)| *t == tab) {
                kinds.push((tab, draw_index(&tc, &registrations)));
            }
        }
    }
    let hosts: Vec<Host> = rust.documents.iter().map(|d| d.host).collect();
    let c = Docking::new(&registrations, None);
    let (c_dirty, c_pending) = c.restore(&mut oracles, &hosts, rust.sidebar, rust.workspace, &kinds);
    let pending = restore(&mut rust, &registrations, &|tab| kinds.iter().find(|(t, _)| t == tab).and_then(|(_, r)| *r));
    assert_eq!(pending, c_pending, "moved sections");
    for (i, document) in rust.documents.iter().enumerate() {
        assert_eq!(document.arrangement, c.dump(&oracles[i]), "document {i}");
        assert_eq!(document.dirty, c_dirty[i], "document {i}'s dirty mark");
    }
}
