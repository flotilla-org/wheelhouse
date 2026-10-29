//! Docking validity (#516): the rule's cases and restore's repairs, named
//! (once native/src/shell/tests/docking.c's), and restore's promise as a
//! property. The hosts are one window, Controlled Split 7, as the native
//! shell makes them: its sidebar, its own panels (a level apart) and a child
//! workspace's panels (level 20).
use hegel::generators as gs;
use hegel::TestCase;

use super::*;
use crate::arrangement::{Arrangement, Panel};

const UNMEASURED: f32 = 3.402_823_5e38;

fn host(kind: HostKind, level: u64, subject: bool, width: f32) -> Host {
    Host {
        kind: Some(kind),
        controlled_split: 7,
        workspace_region: if kind == HostKind::WorkspaceRegion { 7 } else { 0 },
        has_workspace_subject: subject,
        available_width: width,
        level,
    }
}

fn sidebar(width: f32) -> Host {
    host(HostKind::Sidebar, 7, false, width)
}

fn own(width: f32) -> Host {
    host(HostKind::WorkspaceRegion, !7, false, width)
}

fn workspace(width: f32) -> Host {
    host(HostKind::WorkspaceRegion, 20, false, width)
}

/// Some of the native Renderers, with their docking declarations.
fn registry() -> Vec<Registration> {
    use traits::*;
    let r =
        |name: &str, traits: u32, minimum_width: f32, default_host| Registration { name: name.into(), traits, minimum_width, default_host };
    vec![
        r("text", CONTENT | VISUALIZER | LISTED, 0.0, HostKind::WorkspaceRegion),
        r("terminal", CONTENT | VISUALIZER | LISTED, 0.0, HostKind::WorkspaceRegion),
        r("jackstay", CONTENT | VISUALIZER, 0.0, HostKind::WorkspaceRegion),
        r("scroll_region_fixture", CONTENT, 128.0, HostKind::WorkspaceRegion),
        r(SIDEBAR_SECTION, CONTENT | SECTION | CONTROL_SPLIT_SCOPE | CONTROL, 0.0, HostKind::Sidebar),
        r("workspace_selector", CONTENT | SECTION | SELECTS_WORKSPACES | SINGLETON | CONTROL_SPLIT_SCOPE | CONTROL, 0.0, HostKind::Sidebar),
    ]
}

fn find<'a>(registrations: &'a [Registration], name: &str) -> &'a Registration {
    registration(registrations, name).expect("registered")
}

/// `name` as docking finds a View of it in `source`.
fn view<'a>(registrations: &'a [Registration], name: &str, source: Host) -> View<'a> {
    let registration = registration(registrations, name);
    View { exists: true, registration, source, level: view_level(registration, &source) }
}

#[test]
fn the_rule_judges_a_proposed_layout() {
    let registrations = registry();
    let selector = find(&registrations, "workspace_selector");
    let custom = Registration {
        name: "subject_section".into(),
        traits: traits::SECTION | traits::NEEDS_WORKSPACE_SUBJECT | traits::SINGLETON,
        minimum_width: 100.0,
        default_host: HostKind::Sidebar,
    };
    let mut p = Proposal {
        host: host(HostKind::Sidebar, 0, true, 100.0),
        selected_workspace_region: 7,
        instances_after: 1,
        control_surfaces_after: 1,
        closing: false,
        view_level: 0,
    };
    // Width is inclusive at the boundary.
    assert_eq!(check(Some(&custom), &p), Rule::Valid);
    p.host.available_width = 99.0;
    assert_eq!(check(Some(&custom), &p), Rule::MinimumWidth);
    p.host.available_width = 100.0;
    // Missing subjects, duplicate or missing singleton instances, and zero
    // or two Control Surfaces are invalid layouts.
    p.host.has_workspace_subject = false;
    assert_eq!(check(Some(&custom), &p), Rule::WorkspaceSubject);
    p.host.has_workspace_subject = true;
    p.host.kind = Some(HostKind::WorkspaceRegion);
    p.host.workspace_region = 0;
    assert_eq!(check(Some(&custom), &p), Rule::HostAcceptance);
    p.host.kind = Some(HostKind::Sidebar);
    for instances in [0, 2] {
        assert_eq!(check(Some(&custom), &Proposal { instances_after: instances, ..p }), Rule::Singleton);
    }
    for surfaces in [0, 2] {
        assert_eq!(check(Some(selector), &Proposal { control_surfaces_after: surfaces, ..p }), Rule::OneControlSurface);
    }
    // The selecting View may move, but not close.
    assert_eq!(check(Some(selector), &p), Rule::Valid);
    assert_eq!(check(Some(selector), &Proposal { closing: true, ..p }), Rule::SelectorCannotClose);
    // Unregistered content can't be placed, but can close.
    assert_eq!(check(None, &p), Rule::RegisteredView);
    assert_eq!(check(None, &Proposal { closing: true, ..p }), Rule::Valid);
    p.host.kind = None;
    assert_eq!(check(Some(selector), &p), Rule::HostAcceptance);
    // A View of the root Controlled Split's own scope belongs to its level.
    let section = find(&registrations, SIDEBAR_SECTION);
    p.host = Host { kind: Some(HostKind::WorkspaceRegion), level: 7, ..p.host };
    p.view_level = 7;
    assert_eq!(check(Some(section), &p), Rule::Valid);
    p.host.level = 8;
    assert_eq!(check(Some(section), &p), Rule::ControlSplitLevel);
    // A selector can't sit in the Workspace Region it selects; another
    // region is fine.
    let p = Proposal { host: host(HostKind::WorkspaceRegion, 7, true, 640.0), view_level: 7, ..p };
    assert_eq!(check(Some(selector), &p), Rule::SelectorOutsideSelectedRegion);
    assert_eq!(check(Some(selector), &Proposal { host: Host { workspace_region: 8, ..p.host }, ..p }), Rule::Valid);
    // Every refusal has words for the user.
    assert!(Rule::ALL.iter().all(|rule| !rule.message().is_empty()));
}

#[test]
fn hosts_present_and_classify_by_kind() {
    assert_eq!(presentation(Some(HostKind::Sidebar), 1), Presentation::SectionHeader);
    assert_eq!(presentation(Some(HostKind::Sidebar), 2), Presentation::SectionHeader);
    assert_eq!(presentation(Some(HostKind::WorkspaceRegion), 1), Presentation::Tabs);
    assert_eq!(host_kind_from_owner(true), HostKind::Sidebar);
    assert_eq!(host_kind_from_owner(false), HostKind::WorkspaceRegion);
}

#[test]
fn placement_creation_and_closure_ask_the_rule_what_drop_sites_and_commands_ask() {
    let registrations = registry();
    let one = |_split: u64| 1;
    let instances: Option<Instances> = Some(&one);
    let selector = view(&registrations, "workspace_selector", workspace(640.0));
    let terminal = view(&registrations, "terminal", workspace(640.0));
    let text = view(&registrations, "text", sidebar(640.0));
    // The selector is the window's one Control Surface.
    assert_ne!(placement(&selector, &workspace(640.0), instances), Rule::Valid);
    assert_ne!(placement(&selector, &own(640.0), instances), Rule::Valid);
    assert_eq!(placement(&selector, &sidebar(640.0), instances), Rule::Valid);
    assert_eq!(placement(&terminal, &workspace(640.0), instances), Rule::Valid);
    assert_ne!(placement(&text, &sidebar(640.0), instances), Rule::Valid);
    assert_eq!(closure(&selector), Rule::SelectorCannotClose);
    assert_eq!(closure(&terminal), Rule::Valid);
    // Creating content is allowed; creating the selector would add a
    // second Control Surface.
    assert_eq!(creation(registration(&registrations, "terminal"), &workspace(UNMEASURED), instances), Rule::Valid);
    assert_ne!(creation(registration(&registrations, "workspace_selector"), &sidebar(UNMEASURED), instances), Rule::Valid);
    // Sections belong to the root Controlled Split, wherever they are shown.
    let section = view(&registrations, SIDEBAR_SECTION, sidebar(640.0));
    assert_eq!(placement(&section, &sidebar(640.0), instances), Rule::Valid);
    assert_ne!(placement(&section, &workspace(640.0), instances), Rule::Valid);
    assert_ne!(placement(&section, &own(640.0), instances), Rule::Valid);
    assert_ne!(creation(registration(&registrations, SIDEBAR_SECTION), &workspace(UNMEASURED), instances), Rule::Valid);
    assert_ne!(placement(&terminal, &host(HostKind::WorkspaceRegion, 30, false, 640.0), instances), Rule::Valid);
    // Neither a section nor the selector crosses into another Controlled
    // Split: that would leave one without its Control Surface.
    let other_sidebar = Host { controlled_split: 9, level: 9, ..sidebar(640.0) };
    assert_ne!(placement(&section, &other_sidebar, instances), Rule::Valid);
    assert_eq!(placement(&selector, &other_sidebar, instances), Rule::OneControlSurface);
    // A second copy of a singleton makes a move invalid.
    let two = |_split: u64| 2;
    let in_sidebar = view(&registrations, "workspace_selector", sidebar(640.0));
    assert_eq!(placement(&in_sidebar, &sidebar(640.0), Some(&two)), Rule::Singleton);
    // Widths are inclusive.
    for (width, valid) in [(0.0, false), (127.0, false), (128.0, true), (129.0, true), (640.0, true)] {
        let fixture = view(&registrations, "scroll_region_fixture", workspace(640.0));
        assert_eq!(placement(&fixture, &workspace(width), instances) == Rule::Valid, valid, "{width}");
    }
    // Unknown saved content stays removable, but can't be created or
    // placed; no View, and no host, is nothing.
    let unknown = view(&registrations, "unknown_saved_view", workspace(640.0));
    assert_eq!(closure(&unknown), Rule::Valid);
    assert_ne!(placement(&unknown, &workspace(640.0), instances), Rule::Valid);
    assert_eq!(closure(&View::default()), Rule::RegisteredView);
    assert_eq!(creation(None, &workspace(UNMEASURED), instances), Rule::RegisteredView);
    assert_ne!(placement(&View::default(), &workspace(640.0), instances), Rule::Valid);
    assert_eq!(placement(&terminal, &Host::default(), instances), Rule::RegisteredView);
}

// ---------------------------------------------------------------------------
// Restore. Fixtures are written as arrangement fixtures are: a number is a
// panel of that weight, a word a View of that kind (`kind:{selected}`
// selected).

type Tab = String;

struct Window {
    registrations: Vec<Registration>,
    documents: Documents<Tab>,
}

fn arrangement(text: &str, count: &mut u32) -> Arrangement<Tab> {
    fn panel(words: &mut std::iter::Peekable<std::vec::IntoIter<String>>, count: &mut u32) -> (Vec<Panel<Tab>>, Vec<Tab>, Option<Tab>) {
        let (mut children, mut tabs, mut selected) = (vec![], vec![], None);
        while let Some(word) = words.next() {
            match word.as_str() {
                "}" => break,
                w if w.starts_with(|c: char| c.is_ascii_digit()) => {
                    assert_eq!(words.next().as_deref(), Some("{"));
                    let (grand, child_tabs, child_selected) = panel(words, count);
                    let mut child = Panel::new(PanelId(0));
                    child.weight = w.parse().expect("a weight");
                    (child.children, child.tabs, child.selected) = (grand, child_tabs, child_selected);
                    children.push(child);
                }
                kind => {
                    *count += 1;
                    let tab = format!("{kind}#{count}");
                    if words.peek().map(String::as_str) == Some("{") {
                        words.next();
                        assert_eq!(words.next().as_deref(), Some("selected"));
                        assert_eq!(words.next().as_deref(), Some("}"));
                        selected = Some(tab.clone());
                    }
                    tabs.push(tab);
                }
            }
        }
        (children, tabs, selected)
    }
    fn number(p: &mut Panel<Tab>, next: &mut u64) {
        p.id = PanelId(*next);
        *next += 1;
        for c in &mut p.children {
            number(c, next);
        }
    }
    let mut a = Arrangement::new();
    let spaced = text.replace('{', " { ").replace('}', " } ").replace(':', " ");
    let mut words: Vec<String> = spaced.split_whitespace().map(String::from).collect();
    if words.first().map(String::as_str) != Some("panels") {
        return a;
    }
    words.drain(..2);
    let mut words = words.into_iter().peekable();
    let (children, tabs, selected) = panel(&mut words, count);
    let mut root = Panel::new(PanelId(0));
    root.weight = 1.0;
    (root.children, root.tabs, root.selected) = (children, tabs, selected);
    number(&mut root, &mut a.next_id);
    a.root = Some(root);
    a
}

/// A window of a sidebar and a child workspace, as restore reads them.
fn window(sidebar_text: &str, workspace_text: &str) -> Window {
    let mut count = 0;
    let documents = vec![
        Document { arrangement: arrangement(sidebar_text, &mut count), host: sidebar(UNMEASURED), dirty: false },
        Document { arrangement: arrangement(workspace_text, &mut count), host: workspace(UNMEASURED), dirty: false },
    ];
    Window { registrations: registry(), documents: Documents { documents, sidebar: Some(0), workspace: Some(1) } }
}

impl Window {
    fn restore(&mut self) -> Vec<Tab> {
        let names: Vec<String> = self.registrations.iter().map(|r| r.name.clone()).collect();
        let kind = move |tab: &Tab| names.iter().position(|n| tab.split('#').next() == Some(n.as_str()));
        restore(&mut self.documents, &self.registrations, &kind)
    }
    fn sidebar(&self) -> &Arrangement<Tab> {
        &self.documents.documents[0].arrangement
    }
    fn workspace(&self) -> &Arrangement<Tab> {
        &self.documents.documents[1].arrangement
    }
    fn dirty(&self) -> bool {
        self.documents.documents.iter().any(|d| d.dirty)
    }
    fn clean(&mut self) {
        for d in &mut self.documents.documents {
            d.dirty = false;
        }
    }
}

fn find_tab(a: &Arrangement<Tab>, kind: &str) -> Option<Tab> {
    a.panels().flat_map(|p| p.tabs.iter()).find(|t| t.split('#').next() == Some(kind)).cloned()
}

fn count(a: &Arrangement<Tab>, kind: &str) -> usize {
    a.panels().flat_map(|p| p.tabs.iter()).filter(|t| t.split('#').next() == Some(kind)).count()
}

fn panel_of(a: &Arrangement<Tab>, tab: &Tab) -> Option<PanelId> {
    a.panel_with(tab).map(|p| p.id)
}

fn root(a: &Arrangement<Tab>) -> &Panel<Tab> {
    a.root.as_ref().expect("a root")
}

#[test]
fn restore_moves_a_cross_level_placement_to_its_default_host_keeping_its_identity() {
    let mut w = window("panels:{1:{sidebar_section}}", "panels:{workspace_selector terminal sidebar_section:{selected}}");
    let section = find_tab(w.workspace(), SIDEBAR_SECTION).expect("there");
    let pending = w.restore();
    // A section moves to the sidebar, which places it by its hints.
    assert_eq!(pending, [section.clone()]);
    assert_eq!(panel_of(w.sidebar(), &section), Some(root(w.sidebar()).children[0].id));
    assert_eq!(root(w.sidebar()).children[0].tabs.last(), Some(&section));
    assert!(panel_of(w.workspace(), &section).is_none() && count(w.sidebar(), SIDEBAR_SECTION) == 2);
    // The panel it left goes with it; the selector, refused in the
    // workspace, goes to the sidebar too.
    assert!(root(w.workspace()).restore_emptied && count(w.sidebar(), "workspace_selector") == 1);
    assert!(count(w.workspace(), "terminal") == 1 && w.dirty());
    // Repair is idempotent.
    w.clean();
    assert!(w.restore().is_empty() && !w.dirty());
}

#[test]
fn restore_returns_a_hand_edited_placement_to_its_default_and_leaves_valid_views() {
    let mut w = window("panels:{workspace_selector text}", "panels:{terminal}");
    let (selector, text, terminal) = (
        find_tab(w.sidebar(), "workspace_selector").expect("there"),
        find_tab(w.sidebar(), "text").expect("there"),
        find_tab(w.workspace(), "terminal").expect("there"),
    );
    w.restore();
    assert_eq!(panel_of(w.sidebar(), &selector), Some(root(w.sidebar()).id));
    assert_eq!(
        (panel_of(w.workspace(), &text), panel_of(w.workspace(), &terminal)),
        (Some(root(w.workspace()).id), Some(root(w.workspace()).id))
    );
    assert_eq!(root(w.workspace()).tabs.last(), Some(&text));
    w.clean();
    w.restore();
    assert!(!w.dirty());
}

#[test]
fn restore_keeps_one_singleton_preferring_a_valid_copy_even_when_an_invalid_one_comes_first() {
    // The workspace's copy comes first in the window's tree order here.
    let mut count = 0;
    let workspace_doc = arrangement("panels:{workspace_selector terminal}", &mut count);
    let sidebar_doc = arrangement("panels:{workspace_selector}", &mut count);
    let mut w = Window {
        registrations: registry(),
        documents: Documents {
            documents: vec![
                Document { arrangement: workspace_doc, host: workspace(UNMEASURED), dirty: false },
                Document { arrangement: sidebar_doc, host: sidebar(UNMEASURED), dirty: false },
            ],
            sidebar: Some(1),
            workspace: Some(0),
        },
    };
    let duplicate = find_tab(&w.documents.documents[0].arrangement, "workspace_selector").expect("there");
    let kept = find_tab(&w.documents.documents[1].arrangement, "workspace_selector").expect("there");
    w.restore();
    let (workspace_doc, sidebar_doc) = (&w.documents.documents[0].arrangement, &w.documents.documents[1].arrangement);
    assert_eq!((count_of(workspace_doc), count_of(sidebar_doc)), (0, 1));
    assert!(sidebar_doc.panel_with(&kept).is_some());
    // The duplicate goes: setting its arrangement releases its View.
    assert!(workspace_doc.removed.contains(&duplicate));
}

fn count_of(a: &Arrangement<Tab>) -> usize {
    count(a, "workspace_selector")
}

#[test]
fn restore_moves_invalid_content_to_the_first_leaf_of_a_split_workspace() {
    let mut w = window("panels:{terminal}", "panels:{0.5:{text} 0.5:{jackstay}}");
    let misplaced = find_tab(w.sidebar(), "terminal").expect("there");
    w.restore();
    let first = &root(w.workspace()).children[0];
    assert_eq!((panel_of(w.workspace(), &misplaced), first.tabs.last()), (Some(first.id), Some(&misplaced)));
}

#[test]
fn restore_breaks_ties_in_tree_order_and_gives_an_empty_sidebar_a_panel() {
    let mut w = window("", "panels:{workspace_selector workspace_selector}");
    let first = root(w.workspace()).tabs[0].clone();
    w.restore();
    assert_eq!(root(w.sidebar()).tabs.first(), Some(&first));
    assert_eq!((count(w.workspace(), "workspace_selector"), count(w.sidebar(), "workspace_selector")), (0, 1));
}

#[test]
fn restore_moving_a_view_between_arrangements_keeps_the_rest_and_removes_nothing() {
    let mut w = window("panels:{0.5:{sidebar_section:{selected} terminal} 0.5:{sidebar_section}}", "panels:{0.5:{terminal} 0.5:{text}}");
    let source = root(w.sidebar()).children[0].id;
    let misplaced = root(w.sidebar()).children[0].tabs[1].clone();
    w.restore();
    let first = &root(w.workspace()).children[0];
    assert_eq!((panel_of(w.workspace(), &misplaced), first.tabs.last()), (Some(first.id), Some(&misplaced)));
    assert!(w.sidebar().panel(source).is_some_and(|p| p.tabs.len() == 1) && count(w.sidebar(), "terminal") == 0);
    assert!(w.sidebar().removed.is_empty() && w.workspace().removed.is_empty());
    assert_eq!((crate::arrangement::problem(w.sidebar()), crate::arrangement::problem(w.workspace())), (None, None));
    w.clean();
    w.restore();
    assert!(!w.dirty());
}

#[test]
fn restore_selects_where_a_view_lands_only_if_it_was_selected_where_it_was() {
    for marked in [false, true] {
        let sidebar_text =
            if marked { "panels:{1:{sidebar_section jackstay:{selected}}}" } else { "panels:{1:{sidebar_section:{selected} jackstay}}" };
        let mut w = window(sidebar_text, "panels:{terminal:{selected} text}");
        let (jackstay, terminal, section) = (
            find_tab(w.sidebar(), "jackstay").expect("there"),
            find_tab(w.workspace(), "terminal").expect("there"),
            find_tab(w.sidebar(), SIDEBAR_SECTION).expect("there"),
        );
        w.restore();
        assert_eq!((panel_of(w.workspace(), &jackstay), root(w.workspace()).tabs.last()), (Some(root(w.workspace()).id), Some(&jackstay)));
        assert_eq!(root(w.workspace()).selected, Some(if marked { jackstay.clone() } else { terminal }));
        assert_eq!(root(w.sidebar()).children[0].selected, (!marked).then_some(section));
    }
}

#[test]
fn restore_leaves_empty_layouts_empty() {
    let mut w = window("", "");
    assert!(w.restore().is_empty() && !w.dirty() && w.sidebar().root.is_none() && w.workspace().root.is_none());
}

/// After a restore, a window has at most one of each singleton, and every
/// registered View is valid where it is, or sits in its default host's
/// first leaf (a default can still lack context, such as a subject).
/// Generated windows: the native kinds above, on up to four documents with
/// hosts of every kind, a sidebar and a workspace default or none.
#[hegel::test(test_cases = 300)]
fn after_restore_singletons_are_single_and_views_valid_or_at_their_default(tc: TestCase) {
    let registrations = registry();
    let count = tc.draw(gs::integers::<usize>().min_value(1).max_value(4));
    let mut documents = Documents {
        documents: vec![],
        sidebar: tc.draw(gs::booleans()).then(|| tc.draw(gs::integers::<usize>().max_value(count - 1))),
        workspace: tc.draw(gs::booleans()).then(|| tc.draw(gs::integers::<usize>().max_value(count - 1))),
    };
    let mut kinds: Vec<(u64, usize)> = vec![];
    let mut next_tab = 1;
    for _ in 0..count {
        let (a, _, _) = crate::arrangement::generate::build(&tc, 12);
        let host = Host {
            kind: tc.draw(gs::sampled_from(&[Some(HostKind::Sidebar), Some(HostKind::WorkspaceRegion), None][..])),
            controlled_split: 7,
            workspace_region: tc.draw(gs::sampled_from(&[0u64, 7, 8][..])),
            has_workspace_subject: tc.draw(gs::booleans()),
            available_width: UNMEASURED,
            level: tc.draw(gs::sampled_from(&[7u64, 20, !7][..])),
        };
        // Tabs are numbered apart per document.
        let offset = next_tab;
        let mut numbered = a.clone();
        fn shift(p: &mut Panel<u64>, by: u64) {
            p.tabs.iter_mut().for_each(|t| *t += by);
            p.selected = p.selected.map(|t| t + by);
            p.children.iter_mut().for_each(|c| shift(c, by));
        }
        if let Some(root) = &mut numbered.root {
            shift(root, offset);
        }
        for tab in crate::arrangement::generate::tabs(&numbered) {
            next_tab = next_tab.max(tab + 1);
            kinds.push((tab, tc.draw(gs::integers::<usize>().max_value(registrations.len() - 1))));
        }
        documents.documents.push(Document { arrangement: numbered, host, dirty: false });
    }
    let kind = |tab: &u64| kinds.iter().find(|(t, _)| t == tab).map(|(_, k)| *k);
    restore(&mut documents, &registrations, &kind);
    let mut singletons = vec![0; registrations.len()];
    for document in &documents.documents {
        let first_leaf = document.arrangement.root.as_ref().map(|r| r.first_leaf().id);
        for panel in document.arrangement.panels() {
            for tab in &panel.tabs {
                let Some(k) = kind(tab) else { continue };
                let r = &registrations[k];
                if r.traits & traits::SINGLETON != 0 {
                    singletons[k] += 1;
                }
                let default = match r.default_host {
                    HostKind::Sidebar => documents.sidebar,
                    HostKind::WorkspaceRegion => documents.workspace,
                };
                let at_default = default.is_some_and(|d| std::ptr::eq(&documents.documents[d], document)) && first_leaf == Some(panel.id);
                let valid = valid_where_it_is(Some(r), &document.host);
                assert!(
                    valid || at_default || default.is_none(),
                    "{tab} ({}) is invalid in {:?}, not at its default",
                    r.name,
                    document.host
                );
            }
        }
    }
    assert!(singletons.iter().all(|&n| n <= 1), "singletons {singletons:?}");
}
