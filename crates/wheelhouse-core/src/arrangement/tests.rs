//! The arrangement rules (#516): invariants every edit keeps, as Hegel
//! properties over generated sessions, and named cases for the rules'
//! documented behaviour (once native/src/shell/tests/arrangement.c's).
use hegel::generators as gs;
use hegel::TestCase;

use super::generate::{draw_panel, edit_step, tabs, Step, Tab};
use super::*;

// ---------------------------------------------------------------------------
// Invariants

/// Whether the rules promise `edit` keeps a valid `a` valid. The others are
/// allowed to break it, as the C did and its callers rely on:
/// - an add with a positive weight leaves its siblings' weights for the
///   sidebar to size; one into an empty leaf makes a split of one child,
///   which the next add completes;
/// - remove is the sidebar's reconciliation: siblings keep their weights,
///   and a split it leaves with one child stays a split;
/// - a tab inserted or dropped into a split panel, or readdressed to a View
///   already there, is the caller's mistake to avoid;
/// - a resize whose two sides can't both keep the floor moves the boundary
///   by the whole delta, which may pass zero.
fn promised_valid(a: &Arrangement<Tab>, edit: &Edit<Tab>) -> bool {
    let is_split = |panel: &PanelId| a.panel(*panel).is_some_and(|p| !p.children.is_empty());
    match edit {
        Edit::Add { parent, weight } => {
            let parent = parent.and_then(|p| a.panel(p));
            *weight <= 0.0 && parent.is_some_and(|p| p.children.len() >= 2 || (p.children.is_empty() && !p.tabs.is_empty()))
        }
        Edit::Remove { .. } => false,
        Edit::InsertTab { to, .. } => !is_split(to),
        Edit::Drop { to, dir: None, tab: Some(_), .. } => !is_split(to),
        Edit::Readdress { to, .. } => !tabs(a).contains(to),
        Edit::Resize { panel, floor, .. } => {
            let Some(path) = a.path(panel.0) else { return true };
            let Some((&i, parent)) = path.split_last() else { return true };
            let siblings = &a.at(parent).children;
            let total: f32 = siblings.iter().map(|c| c.weight.max(0.0)).sum();
            siblings.get(i + 1).is_none_or(|next| (siblings[i].weight + next.weight) / total.max(f32::MIN_POSITIVE) >= 2.0 * floor)
        }
        _ => true,
    }
}

/// A valid arrangement grown from a clear: splits of any panel, new tabs in
/// leaves, provider names. `next_tab` is the next tab never used.
fn valid_build(tc: &TestCase, next_tab: &mut Tab) -> Arrangement<Tab> {
    let mut a = Arrangement::new();
    apply(&mut a, &Edit::Clear, &|_| false);
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(24)) {
        let ids: Vec<PanelId> = a.panels().map(|p| p.id).collect();
        let leaves: Vec<PanelId> = a.panels().filter(|p| p.children.is_empty()).map(|p| p.id).collect();
        let step = match tc.draw(gs::integers::<u8>().max_value(9)) {
            0..=3 => {
                Step::Edit { edit: Edit::Split { panel: tc.draw(gs::sampled_from(&ids)), dir: generate::draw_dir(tc) }, hidden: vec![] }
            }
            4 => Step::Name { panel: tc.draw(gs::sampled_from(&ids)), name: tc.draw(gs::integers::<u64>().max_value(2)) },
            _ => {
                let to = tc.draw(gs::sampled_from(&leaves));
                let tab = *next_tab;
                *next_tab += 1;
                let after = a.panel(to).and_then(|p| p.tabs.last().copied());
                Step::Edit { edit: Edit::InsertTab { tab, to, after, select: tc.draw(gs::booleans()) }, hidden: vec![] }
            }
        };
        step.apply(&mut a);
    }
    a
}

/// What every edit of a valid arrangement keeps, checked after each:
/// validity, a split's weights summing to one within `problem`'s
/// tolerance, each tab in at most one panel, a close's heir an existing
/// leaf, a named panel never closed by a settle, and `removed` listing
/// exactly the tabs that left (a detached tab leaves without its View
/// going; a readdressed one is renamed).
#[hegel::test(test_cases = 400)]
fn edits_keep_their_promises(tc: TestCase) {
    let mut next_tab = 1;
    let mut a = valid_build(&tc, &mut next_tab);
    assert_eq!(problem(&a), None, "the build is valid");
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(20)) {
        let Step::Edit { edit, hidden } = edit_step(&tc, &a, next_tab + 1) else { unreachable!("edit steps are edits") };
        if !promised_valid(&a, &edit) {
            continue;
        }
        let before = a.clone();
        let out = apply(&mut a, &edit, &|t| hidden.contains(t));
        let what = format!("{edit:?} on {before:?}");
        assert_eq!(problem(&a), None, "{what}");
        for p in a.panels().filter(|p| !p.children.is_empty()) {
            let total: f32 = p.children.iter().map(|c| c.weight).sum();
            assert!((total - 1.0).abs() <= 0.00001 * p.children.len() as f32, "split {:?} weighs {total}: {what}", p.id);
        }
        let mut seen = tabs(&a);
        seen.sort_unstable();
        assert!(seen.windows(2).all(|w| w[0] != w[1]), "a tab in two panels: {what}");
        if let Some(heir) = out.heir {
            assert!(a.panel(heir).is_some_and(|p| p.children.is_empty()), "heir {heir:?} is not a leaf: {what}");
        }
        if let Edit::Settle { panel, .. } | Edit::Drop { to: panel, .. } = &edit {
            let source = if let Edit::Drop { .. } = edit { out.source } else { Some(*panel) };
            if let Some(named) = source.and_then(|s| before.panel(s)).filter(|p| !p.closes_when_emptied()) {
                assert!(a.panel(named.id).is_some(), "named panel {:?} closed: {what}", named.id);
            }
        }
        let mut left: Vec<Tab> = tabs(&before);
        for tab in tabs(&a) {
            if let Some(i) = left.iter().position(|t| *t == tab) {
                left.remove(i);
            }
        }
        match &edit {
            Edit::DetachTab { tab } => assert!(out.removed.is_empty() && left == [*tab].repeat(out.applied as usize), "{what}"),
            Edit::Readdress { .. } => assert!(out.removed.is_empty(), "{what}"),
            _ => assert_eq!(out.removed, left, "removed: {what}"),
        }
        if !matches!(edit, Edit::Readdress { .. }) {
            assert_eq!(a.removed, [before.removed.clone(), out.removed.clone()].concat(), "the removed list grows: {what}");
        }
    }
}

/// A grown valid arrangement, and a leaf with at least one tab, if any.
fn with_a_tab(tc: &TestCase) -> Option<(Arrangement<Tab>, PanelId, Tab)> {
    let mut next_tab = 1;
    let a = valid_build(tc, &mut next_tab);
    let leaves: Vec<(PanelId, Vec<Tab>)> = a.panels().filter(|p| !p.tabs.is_empty()).map(|p| (p.id, p.tabs.clone())).collect();
    if leaves.is_empty() {
        return None;
    }
    let (panel, tabs) = tc.draw(gs::sampled_from(&leaves));
    let tab = tc.draw(gs::sampled_from(&tabs));
    Some((a, panel, tab))
}

/// A drop into its own panel without a direction only reorders: every
/// panel stays where it was with its weight, and the panel keeps its tabs.
#[hegel::test(test_cases = 300)]
fn a_drop_into_its_own_panel_only_reorders(tc: TestCase) {
    let Some((mut a, panel, tab)) = with_a_tab(&tc) else { return };
    let before = a.clone();
    let mine = a.panel(panel).expect("there").tabs.clone();
    let after = tc.draw(gs::booleans()).then(|| tc.draw(gs::sampled_from(&mine)));
    let out = apply(&mut a, &Edit::Drop { tab: Some(tab), to: panel, dir: None, after }, &|_| false);
    assert_eq!(out.heir, None);
    let shape = |a: &Arrangement<Tab>| a.panels().map(|p| (p.id, p.weight.to_bits(), p.children.len())).collect::<Vec<_>>();
    assert_eq!(shape(&a), shape(&before));
    let (mut now, mut was) = (a.panel(panel).expect("kept").tabs.clone(), mine);
    now.sort_unstable();
    was.sort_unstable();
    assert_eq!(now, was);
    for p in before.panels().filter(|p| p.id != panel) {
        assert_eq!(a.panel(p.id).map(|q| &q.tabs), Some(&p.tabs));
    }
}

/// A resize keeps both sides at least the floor when they can be, and
/// keeps their sum; equalize gives both sides equal weights (to the weight
/// format's precision).
#[hegel::test(test_cases = 300)]
fn resizing_respects_the_floor_and_equalizing_evens(tc: TestCase) {
    let mut next_tab = 1;
    let mut a = valid_build(&tc, &mut next_tab);
    let panel = draw_panel(&tc, &a);
    let Some(path) = a.path(panel.0) else { return };
    let Some((&i, parent)) = path.split_last() else { return };
    if i + 1 >= a.at(parent).children.len() {
        return;
    }
    let (w, next) = (a.at(parent).children[i].weight, a.at(parent).children[i + 1].weight);
    let delta = tc.draw(gs::floats::<f32>().min_value(-1.5).max_value(1.5).allow_nan(false).allow_infinity(false));
    let floor = tc.draw(gs::floats::<f32>().min_value(0.0).max_value(0.6).allow_nan(false).allow_infinity(false));
    let mut equalized = a.clone();
    apply(&mut a, &Edit::Resize { panel, delta, floor }, &|_| false);
    let (w2, next2) = (a.at(parent).children[i].weight, a.at(parent).children[i + 1].weight);
    if w + next >= 2.0 * floor {
        assert!(w2 >= floor - 1e-6 && next2 >= floor - 1e-6, "{w}+{next} by {delta} over {floor}: {w2}, {next2}");
    }
    assert!((w2 + next2 - (w + next)).abs() <= 2e-6, "the sum moved: {w}+{next} became {w2}+{next2}");
    apply(&mut equalized, &Edit::Equalize { panel }, &|_| false);
    let (e, e_next) = (equalized.at(parent).children[i].weight, equalized.at(parent).children[i + 1].weight);
    assert!((e - e_next).abs() <= 1e-6, "{w} and {next} equalized to {e} and {e_next}");
}

// ---------------------------------------------------------------------------
// Named cases. Fixtures are written as the config syntax wrote them: a
// number is a panel of that weight, any other word a tab of that kind
// (`kind:{selected}` selected), and `split_x` makes the root's axis a row.

/// A tab of `kind`, numbered so each is its own View.
type View = String;

struct Fixture {
    a: Arrangement<View>,
    count: u32,
}

impl Fixture {
    fn view(&mut self, kind: &str) -> View {
        self.count += 1;
        format!("{kind}#{}", self.count)
    }
}

fn tokens(text: &str) -> Vec<String> {
    let mut out = vec![];
    let mut word = String::new();
    for c in text.chars() {
        if c.is_whitespace() || "{}:".contains(c) {
            if !word.is_empty() {
                out.push(std::mem::take(&mut word));
            }
            if !c.is_whitespace() {
                out.push(c.to_string());
            }
        } else {
            word.push(c);
        }
    }
    if !word.is_empty() {
        out.push(word);
    }
    out
}

fn is_weight(word: &str) -> bool {
    word.starts_with(|c: char| c.is_ascii_digit() || c == '.')
}

/// Builds `panel`'s contents from `tokens` up to the matching `}`.
fn build_panel(f: &mut Fixture, panel: PanelId, tokens: &[String], at: &mut usize) {
    while *at < tokens.len() && tokens[*at] != "}" {
        let word = tokens[*at].clone();
        *at += 1;
        let has_body = tokens.get(*at).map(String::as_str) == Some(":");
        if is_weight(&word) {
            let out = apply(&mut f.a, &Edit::Add { parent: Some(panel), weight: 1.0 }, &|_| false);
            let child = out.panel.expect("added");
            f.a.panel_mut(child).expect("there").weight = word.parse().expect("a weight");
            if has_body {
                *at += 2;
                build_panel(f, child, tokens, at);
                *at += 1;
            }
        } else {
            let mut selected = false;
            if has_body {
                *at += 2;
                selected = tokens.get(*at).map(String::as_str) == Some("selected");
                while tokens[*at] != "}" {
                    *at += 1;
                }
                *at += 1;
            }
            let tab = f.view(&word);
            let after = f.a.panel(panel).and_then(|p| p.tabs.last().cloned());
            apply(&mut f.a, &Edit::InsertTab { tab, to: panel, after, select: selected }, &|_| false);
        }
    }
}

fn build(text: &str) -> Fixture {
    let mut f = Fixture { a: Arrangement::new(), count: 0 };
    let tokens = tokens(text);
    if tokens.iter().any(|t| t == "split_x") {
        f.a.root_axis = Axis::Row;
    }
    if let Some(start) = tokens.iter().position(|t| t == "panels") {
        let root = apply(&mut f.a, &Edit::Clear, &|_| false).panel.expect("a root");
        let mut at = start + 3;
        build_panel(&mut f, root, &tokens, &mut at);
    }
    f
}

fn ed(a: &mut Arrangement<View>, edit: Edit<View>) -> Outcome<View> {
    apply(a, &edit, &|_| false)
}

fn root(a: &Arrangement<View>) -> &Panel<View> {
    a.root.as_ref().expect("a root")
}

fn child(a: &Arrangement<View>, i: usize) -> &Panel<View> {
    &root(a).children[i]
}

fn last(a: &Arrangement<View>) -> &Panel<View> {
    root(a).children.last().expect("children")
}

fn removed(a: &Arrangement<View>, tab: &View) -> bool {
    a.removed.contains(tab)
}

fn valid(a: &Arrangement<View>) {
    assert_eq!(problem(a), None, "{a:?}");
}

#[test]
fn a_built_arrangement_numbers_its_panels_in_tree_order_and_alternates_axes() {
    let f = build("split_x panels:{0.72:{text:{selected}} 0.28:{0.5:{terminal text:{selected}} 0.5:{jackstay}}}");
    let a = &f.a;
    valid(a);
    assert_eq!((a.root_axis, root(a).children.len()), (Axis::Row, 2));
    let nested = &last(a).children[0];
    assert_eq!(nested.selected.as_ref(), Some(&nested.tabs[1]));
    assert_eq!(a.panel_with(&nested.tabs[0]).map(|p| p.id), Some(nested.id));
    assert_eq!(a.split_axis(nested.id), Some(Axis::Row));
    assert_eq!(a.split_axis(last(a).id), Some(Axis::Column));
    assert_eq!(a.panels().map(|p| p.id.0).collect::<Vec<_>>(), [1, 2, 3, 4, 5]);
}

#[test]
fn splitting_inserts_beside_or_bisects_and_closing_reverses_both() {
    let mut f = build("split_x panels:{0.5:{text} 0.5:{terminal}}");
    let a = &mut f.a;
    let left = child(a, 0).id;
    let middle = ed(a, Edit::Split { panel: left, dir: Dir::Right }).panel.expect("made");
    assert_eq!(child(a, 1).id, middle);
    assert!(root(a).children.iter().all(|c| c.weight == 0.333333));
    let below = ed(a, Edit::Split { panel: middle, dir: Dir::Down }).panel.expect("made");
    let column = &child(a, 1);
    assert_eq!((column.weight, column.children[0].id, column.children[0].weight), (0.333333, middle, 0.5));
    valid(a);
    // Closing one of two collapses the split into the survivor.
    assert_eq!(ed(a, Edit::Close { panel: below }).heir, Some(middle));
    assert_eq!((child(a, 1).id, child(a, 1).weight), (middle, 0.333333));
    valid(a);
    // Closing one of three gives its share to the others; focus goes to
    // the one before it.
    assert_eq!(ed(a, Edit::Close { panel: middle }).heir, Some(left));
    assert!((child(a, 0).weight - 0.5).abs() < 0.00001 && child(a, 0).weight == last(a).weight);
    // The root's survivor becomes the root, and the root's axis is its own.
    let right = last(a).id;
    let left_tab = child(a, 0).tabs[0].clone();
    ed(a, Edit::Split { panel: right, dir: Dir::Down });
    assert_eq!(ed(a, Edit::Close { panel: left }).heir, Some(right));
    assert_eq!((a.root_axis, root(a).children.len(), root(a).weight), (Axis::Column, 2, 1.0));
    assert!(removed(a, &left_tab));
    valid(a);
    let first = child(a, 0).id;
    ed(a, Edit::Close { panel: first });
    assert!(root(a).children.is_empty());
    let out = ed(a, Edit::Close { panel: root(a).id });
    assert!(!out.applied && out.heir.is_none(), "the root doesn't close");
    // A split left inside a same-axis parent merges into it, scaled.
    let mut f = build("split_x panels:{0.4:{text} 0.6:{0.5:{0.3:{text} 0.7:{text}} 0.5:{text}}}");
    let a = &mut f.a;
    let inner_first = last(a).children[0].children[0].id;
    let closing = last(a).children[1].id;
    assert_eq!(ed(a, Edit::Close { panel: closing }).heir, Some(inner_first));
    assert_eq!((root(a).children.len(), child(a, 1).id), (3, inner_first));
    assert_eq!((child(a, 1).weight, last(a).weight), (0.18, 0.42));
    valid(a);
}

#[test]
fn hand_edited_weights_are_kept_and_a_panel_that_held_its_parent_leaves_equal_shares() {
    let mut f = build("split_x panels:{0.3333333333:{text} 1.1:{terminal} 0:{text}}");
    let a = &mut f.a;
    assert!(problem(a).is_some());
    let kept = child(a, 0).weight;
    let first = child(a, 0).id;
    let down = ed(a, Edit::Split { panel: first, dir: Dir::Down }).panel.expect("made");
    assert_eq!(a.panel(first).expect("there").weight, 0.5);
    assert_eq!((child(a, 0).weight, child(a, 0).children[1].id, child(a, 1).weight), (kept, down, 1.1));
    let middle = child(a, 1).id;
    ed(a, Edit::Close { panel: middle });
    assert_eq!((child(a, 0).weight, last(a).weight), (0.5, 0.5));
    valid(a);
}

#[test]
fn moving_a_tab_selects_it_where_it_lands_and_leaves_its_old_panel_unselected() {
    let mut f = build("split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay:{selected}}}");
    let (left, right) = (child(&f.a, 0).id, last(&f.a).id);
    let (text, terminal, jackstay) = (child(&f.a, 0).tabs[0].clone(), child(&f.a, 0).tabs[1].clone(), last(&f.a).tabs[0].clone());
    let a = &mut f.a;
    assert!(ed(a, Edit::InsertTab { tab: text.clone(), to: right, after: Some(jackstay.clone()), select: true }).applied);
    assert!(!ed(a, Edit::InsertTab { tab: text.clone(), to: right, after: Some(text.clone()), select: true }).applied);
    assert!(!ed(a, Edit::InsertTab { tab: text.clone(), to: PanelId(99), after: None, select: true }).applied);
    assert_eq!(a.panel(left).expect("there").selected, None);
    assert_eq!(a.panel(right).expect("there").selected.as_ref(), Some(&text));
    assert_eq!(a.panel(right).expect("there").tabs, [jackstay.clone(), text.clone()]);
    // First place, and a View from elsewhere joins as any tab does.
    assert!(ed(a, Edit::InsertTab { tab: text.clone(), to: right, after: None, select: true }).applied);
    assert_eq!(a.panel(right).expect("there").tabs[0], text);
    let sessions = f.view("sessions");
    let a = &mut f.a;
    assert!(ed(a, Edit::InsertTab { tab: sessions.clone(), to: left, after: Some(terminal.clone()), select: true }).applied);
    assert_eq!(a.panel(left).expect("there").tabs, [terminal.clone(), sessions.clone()]);
    valid(a);
    // Closing a panel removes its Views; a copy is edited on its own.
    let mut copy = a.clone();
    ed(&mut copy, Edit::Close { panel: left });
    assert_eq!(a.panel(left).expect("there").tabs.len(), 2);
    assert!(copy.panel(left).is_none() && removed(&copy, &terminal) && removed(&copy, &sessions) && !removed(a, &terminal));
}

#[test]
fn selecting_removing_reordering_and_readdressing() {
    let mut f = build("split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay}}");
    let (left, right) = (child(&f.a, 0).id, last(&f.a).id);
    let (text, terminal) = (child(&f.a, 0).tabs[0].clone(), child(&f.a, 0).tabs[1].clone());
    let moved = f.view("text");
    let a = &mut f.a;
    // Selecting is within a panel; none selects none.
    assert!(!ed(a, Edit::Select { panel: right, tab: Some(text.clone()) }).applied);
    assert!(!ed(a, Edit::Select { panel: PanelId(99), tab: None }).applied);
    assert!(ed(a, Edit::Select { panel: left, tab: Some(terminal.clone()) }).applied);
    assert!(ed(a, Edit::Select { panel: left, tab: None }).applied && child(a, 0).selected.is_none());
    ed(a, Edit::Select { panel: left, tab: Some(terminal.clone()) });
    // Removing a tab names its View for setting to release.
    let out = ed(a, Edit::RemoveTab { tab: terminal.clone() });
    assert!(out.applied && out.removed == [terminal.clone()]);
    assert!(!ed(a, Edit::RemoveTab { tab: terminal.clone() }).applied);
    assert!(child(a, 0).selected.is_none() && child(a, 0).tabs.len() == 1 && removed(a, &terminal));
    // Reordering moves a panel, with its weight, among its siblings.
    ed(a, Edit::Split { panel: right, dir: Dir::Right });
    for (i, w) in [0.2, 0.3, 0.5].into_iter().enumerate() {
        a.root.as_mut().expect("root").children[i].weight = w;
    }
    let last_id = last(a).id;
    assert!(ed(a, Edit::Reorder { panel: left, after: Some(last_id) }).applied);
    assert_eq!((child(a, 0).id, last(a).id, last(a).weight), (right, left, 0.2));
    assert!(ed(a, Edit::Reorder { panel: left, after: None }).applied && child(a, 0).id == left);
    assert!(!ed(a, Edit::Reorder { panel: left, after: Some(left) }).applied);
    assert!(!ed(a, Edit::Reorder { panel: root(a).id, after: None }).applied);
    ed(a, Edit::Split { panel: left, dir: Dir::Down });
    assert!(!ed(a, Edit::Reorder { panel: left, after: Some(right) }).applied, "not siblings");
    valid(a);
    // A View's tabs follow it to a new address, and its selection.
    ed(a, Edit::Select { panel: left, tab: Some(text.clone()) });
    assert!(ed(a, Edit::Readdress { from: text.clone(), to: moved.clone() }).applied);
    assert!(!ed(a, Edit::Readdress { from: text.clone(), to: moved.clone() }).applied);
    assert_eq!(a.panel_with(&moved).map(|p| p.id), Some(left));
    assert!(a.panel_with(&text).is_none() && a.panel(left).expect("there").selected == Some(moved));
}

#[test]
fn a_drop_settles_the_panel_its_tab_left_by_the_callers_rule() {
    let text = "split_x panels:{0.3:{text:{selected} terminal binary} 0.3:{jackstay} 0.4:{sessions}}";
    let mut f = build(text);
    let (source, middle, last_id) = (child(&f.a, 0).id, child(&f.a, 1).id, last(&f.a).id);
    let [a_tab, hidden, b]: [View; 3] = child(&f.a, 0).tabs.clone().try_into().expect("three");
    let hide = |t: &View| *t == hidden;
    let a = &mut f.a;
    // Moving an unselected tab still selects the first shown one; the
    // hidden tab is passed over.
    ed(a, Edit::Select { panel: source, tab: Some(b.clone()) });
    let out = apply(a, &Edit::Drop { tab: Some(b.clone()), to: last_id, dir: None, after: None }, &hide);
    assert_eq!((out.panel, out.source, out.heir), (Some(last_id), Some(source), None));
    assert_eq!(a.panel(source).expect("there").selected.as_ref(), Some(&a_tab));
    // Moving the last shown tab closes the source, removing the hidden
    // View; its next sibling is the heir.
    let out = apply(a, &Edit::Drop { tab: Some(a_tab.clone()), to: middle, dir: None, after: None }, &hide);
    assert_eq!((out.panel, out.source, out.heir), (Some(middle), Some(source), Some(middle)));
    assert!(a.panel(source).is_none() && a.panel(middle).expect("there").selected == Some(a_tab.clone()));
    assert!(removed(a, &hidden) && !removed(a, &a_tab) && out.removed == [hidden.clone()]);
    valid(a);
    // Moving within a panel settles nothing.
    let out = apply(a, &Edit::Drop { tab: Some(a_tab.clone()), to: middle, dir: None, after: None }, &hide);
    assert_eq!((out.panel, out.heir, root(a).children.len()), (Some(middle), None, 2));

    // A split keeps a source with only a hidden tab, without selecting it.
    let mut f = build(text);
    let (source, middle) = (child(&f.a, 0).id, child(&f.a, 1).id);
    let [a_tab, hidden, b]: [View; 3] = child(&f.a, 0).tabs.clone().try_into().expect("three");
    let hide = |t: &View| *t == hidden;
    let a = &mut f.a;
    ed(a, Edit::Select { panel: source, tab: Some(b.clone()) });
    let out = apply(a, &Edit::Drop { tab: Some(a_tab.clone()), to: middle, dir: Some(Dir::Down), after: None }, &hide);
    let made = out.panel.expect("made");
    assert_eq!((out.source, out.heir), (Some(source), None));
    assert_eq!(a.panel(source).expect("there").selected.as_ref(), Some(&b));
    assert_eq!(a.panel(made).expect("there").selected.as_ref(), Some(&a_tab));
    apply(a, &Edit::Drop { tab: Some(b.clone()), to: middle, dir: Some(Dir::Up), after: None }, &hide);
    let left = a.panel(source).expect("kept");
    assert!(left.selected.is_none() && left.tabs.len() == 1);
    valid(a);
    // Splitting its last tab off a panel keeps that panel, now empty.
    let landed = a.panel_with(&b).expect("there").id;
    let out = apply(a, &Edit::Drop { tab: Some(b.clone()), to: landed, dir: Some(Dir::Right), after: None }, &hide);
    assert_eq!((out.source, out.heir), (Some(landed), None));
    assert!(a.panel(landed).expect("kept").tabs.is_empty());
    // A split takes an emptied source away and names its heir.
    let emptied = a.panel_with(&b).expect("there").id;
    let out = apply(a, &Edit::Drop { tab: Some(b.clone()), to: source, dir: Some(Dir::Left), after: None }, &hide);
    assert!(out.source == Some(emptied) && out.heir.is_some() && a.panel(emptied).is_none());
    valid(a);
    // No tab only splits; settling without closing leaves an emptied panel.
    let before = a.panels().count();
    let out = apply(a, &Edit::Drop { tab: None, to: source, dir: Some(Dir::Down), after: None }, &hide);
    let made = out.panel.expect("made");
    assert!(out.source.is_none() && a.panel(made).expect("there").tabs.is_empty());
    assert!(!apply(a, &Edit::Settle { panel: made, dir: None, may_close: false }, &hide).applied);
    assert!(apply(a, &Edit::Settle { panel: made, dir: None, may_close: true }, &hide).applied);
    assert!(a.panel(made).is_none() && a.panels().count() == before);
    valid(a);
}

#[test]
fn a_panel_a_provider_named_stays_when_a_move_empties_it() {
    let mut f = build("split_x panels:{0.3:{text:{selected}} 0.3:{terminal:{selected}} 0.4:{sessions}}");
    let a = &mut f.a;
    a.root.as_mut().expect("root").children[0].named = Some("files".into());
    let (named, unnamed, last_id) = (child(a, 0).id, child(a, 1).id, last(a).id);
    let tab = child(a, 0).tabs[0].clone();
    let out = ed(a, Edit::Drop { tab: Some(tab), to: last_id, dir: None, after: None });
    assert_eq!((out.source, out.heir), (Some(named), None));
    assert!(a.panel(named).expect("kept").tabs.is_empty());
    // Its unnamed sibling, emptied in turn, goes.
    let tab = child(a, 1).tabs[0].clone();
    let out = ed(a, Edit::Drop { tab: Some(tab), to: last_id, dir: None, after: None });
    assert!(out.source == Some(unnamed) && out.heir.is_some() && a.panel(unnamed).is_none());
    assert_eq!(root(a).children.len(), 2);
    valid(a);
}

#[test]
fn resizing_moves_one_boundary_and_equalizing_evens_it() {
    let mut f = build("split_x panels:{0.2:{text} 0.3:{text} 0.5:{text}}");
    let a = &mut f.a;
    let first = child(a, 0).id;
    ed(a, Edit::Resize { panel: first, delta: 0.25, floor: 0.05 });
    assert_eq!((child(a, 0).weight, child(a, 1).weight), (0.45, 0.05));
    ed(a, Edit::Resize { panel: first, delta: -0.6, floor: 0.05 });
    assert_eq!((child(a, 0).weight, child(a, 1).weight), (0.05, 0.45));
    let last_id = last(a).id;
    assert!(!ed(a, Edit::Resize { panel: last_id, delta: 0.1, floor: 0.05 }).applied, "no boundary after the last");
    valid(a);
    // Drifted siblings are put right first.
    let mut f = build("panels:{0.4:{text} 0.4:{text} 0.4:{text}}");
    let first = child(&f.a, 0).id;
    ed(&mut f.a, Edit::Resize { panel: first, delta: 0.0, floor: 0.05 });
    valid(&f.a);
    assert_eq!(child(&f.a, 0).weight, 0.333333);
    let mut f = build("split_x panels:{0.2:{text} 0.3:{text} 0.5:{text}}");
    let middle = child(&f.a, 1).id;
    ed(&mut f.a, Edit::Equalize { panel: middle });
    assert_eq!(root(&f.a).children.iter().map(|c| c.weight).collect::<Vec<_>>(), [0.2, 0.4, 0.4]);
    let mut f = build("panels:{0.6:{text} 0.6:{text}}");
    let (first, last_id) = (child(&f.a, 0).id, last(&f.a).id);
    ed(&mut f.a, Edit::Equalize { panel: first });
    assert_eq!((child(&f.a, 0).weight, last(&f.a).weight), (0.5, 0.5));
    ed(&mut f.a, Edit::Equalize { panel: last_id });
    valid(&f.a);
}

#[test]
fn a_view_moves_between_two_floating_panels_by_detach_move_and_settle() {
    let mut into = build("panels:{text:{selected} terminal}");
    let mut from = build("panels:{jackstay:{selected} text}");
    let (log, jackstay, text) = (root(&into.a).tabs[1].clone(), root(&from.a).tabs[0].clone(), root(&from.a).tabs[1].clone());
    let (into_root, from_root) = (root(&into.a).id, root(&from.a).id);
    assert!(ed(&mut from.a, Edit::DetachTab { tab: jackstay.clone() }).applied);
    assert!(ed(&mut into.a, Edit::InsertTab { tab: jackstay.clone(), to: into_root, after: Some(log), select: true }).applied);
    assert_eq!((root(&into.a).tabs[2].clone(), root(&into.a).selected.clone()), (jackstay.clone(), Some(jackstay.clone())));
    assert!(root(&from.a).tabs.len() == 1 && root(&from.a).selected.is_none() && !removed(&from.a, &jackstay));
    assert!(ed(&mut from.a, Edit::Settle { panel: from_root, dir: None, may_close: true }).applied);
    assert_eq!(root(&from.a).selected.as_ref(), Some(&text));
    // Settling never closes a root; emptied, it stays until discarded.
    ed(&mut from.a, Edit::RemoveTab { tab: text.clone() });
    assert!(!ed(&mut from.a, Edit::Settle { panel: from_root, dir: None, may_close: true }).applied);
    assert!(root(&from.a).tabs.is_empty() && removed(&from.a, &text));
    ed(&mut from.a, Edit::Discard);
    assert!(from.a.root.is_none());
    valid(&from.a);
}

#[test]
fn a_drop_from_another_arrangement_settles_the_panel_it_left_there() {
    let mut workspace = build("split_x panels:{0.5:{text:{selected}} 0.5:{terminal}}");
    let mut sidebar = build("panels:{0.5:{sessions:{selected} log} 0.5:{jackstay:{selected}}}");
    let (to, jackstay, sessions) = (child(&workspace.a, 0).id, last(&sidebar.a).tabs[0].clone(), child(&sidebar.a, 0).tabs[0].clone());
    let left = last(&sidebar.a).id;
    // A drop that empties its panel there closes it.
    let (out, settled) = drop_across(&mut workspace.a, &mut sidebar.a, &jackstay, to, None, None, &|_| false);
    assert_eq!((out.panel, out.source), (Some(to), None));
    assert_eq!((settled.applied, settled.source, settled.heir.is_some()), (true, Some(left), true));
    assert!(sidebar.a.panel(left).is_none() && workspace.a.panel_with(&jackstay).map(|p| p.id) == Some(to));
    // One that leaves tabs reselects there.
    let kept = root(&sidebar.a).id;
    let (_, settled) = drop_across(&mut workspace.a, &mut sidebar.a, &sessions, to, Some(Dir::Down), None, &|_| false);
    assert!(settled.applied && settled.heir.is_none());
    assert_eq!(sidebar.a.panel(kept).and_then(|p| p.selected.clone()).map(|s| s.starts_with("log")), Some(true));
    valid(&workspace.a);
    valid(&sidebar.a);
}

#[test]
fn the_sidebar_adds_sections_at_weight_one_or_at_an_equal_share() {
    let mut a: Arrangement<View> = Arrangement::new();
    assert_eq!((a.root.is_none(), a.root_axis), (true, Axis::Column));
    let first = ed(&mut a, Edit::Add { parent: None, weight: 1.0 }).panel.expect("the root, then its child");
    let root_id = root(&a).id;
    let second = ed(&mut a, Edit::Add { parent: Some(root_id), weight: 1.0 }).panel.expect("made");
    assert_eq!((root(&a).children.len(), child(&a, 0).id, child(&a, 1).id), (2, first, second));
    assert!(
        !ed(&mut a, Edit::Add { parent: None, weight: 1.0 }).applied
            && !ed(&mut a, Edit::Add { parent: Some(PanelId(99)), weight: 1.0 }).applied
    );
    assert!(root(&a).children.iter().all(|c| c.weight == 1.0));
    let third = ed(&mut a, Edit::Add { parent: Some(root_id), weight: 0.0 }).panel.expect("made");
    assert!(root(&a).children.iter().all(|c| c.weight == 0.333333) && last(&a).id == third);
    valid(&a);
    // An equal share counts malformed or zero weights as 0.01.
    a.root.as_mut().expect("root").children[0].weight = 0.0;
    ed(&mut a, Edit::Add { parent: Some(root_id), weight: 0.0 });
    assert!(root(&a).children.iter().all(|c| c.weight > 0.0));
    valid(&a);
}

#[test]
fn adding_to_a_tab_panel_lifts_its_tabs_and_presentation_state_into_a_first_child() {
    let mut f = build("panels:{text:{selected} terminal}");
    let a = &mut f.a;
    let (root_id, text) = (root(a).id, root(a).tabs[0].clone());
    let added = ed(a, Edit::Add { parent: Some(root_id), weight: 0.0 }).panel.expect("made");
    let lifted = child(a, 0);
    assert!(root(a).id == root_id && root(a).tabs.is_empty() && root(a).selected.is_none());
    assert_eq!((lifted.tabs.len(), lifted.selected.as_ref(), lifted.weight, last(a).id), (2, Some(&text), 0.5, added));
    assert!(lifted.options_from == Some(root_id) && lifted.id != root_id);
    valid(a);
}

#[test]
fn removing_a_panel_keeps_its_siblings_weights_and_takes_a_split_it_empties() {
    let mut f = build("panels:{0.5:{text} 0.3:{0.5:{terminal} 0.5:{}} 0.2:{jackstay}}");
    let a = &mut f.a;
    let split = child(a, 1).id;
    let terminal = child(a, 1).children[0].tabs[0].clone();
    let root_id = root(a).id;
    assert!(!ed(a, Edit::Remove { panel: root_id }).applied && !ed(a, Edit::Remove { panel: PanelId(99) }).applied);
    let empty = child(a, 1).children[1].id;
    assert!(ed(a, Edit::Remove { panel: empty }).applied);
    assert_eq!((child(a, 1).children.len(), root(a).children.len(), child(a, 0).weight), (1, 3, 0.5));
    let holder = a.panel_with(&terminal).expect("there").id;
    assert!(ed(a, Edit::Remove { panel: holder }).applied);
    assert_eq!((root(a).children.len(), child(a, 0).weight, last(a).weight), (2, 0.5, 0.2));
    assert!(a.panel(split).is_none() && removed(a, &terminal));
}

#[test]
fn clearing_leaves_one_new_empty_root_keeping_its_axis_until_it_splits() {
    let mut f = build("split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay}}");
    let made = f.view("text");
    let a = &mut f.a;
    let old_last = last(a).id;
    let (text, kept) = (child(a, 0).tabs[0].clone(), last(a).tabs[0].clone());
    let root_id = ed(a, Edit::Clear).panel.expect("a root");
    assert!(root_id > old_last && root(a).tabs.is_empty() && a.root_axis == Axis::Row);
    assert!(removed(a, &text) && removed(a, &kept));
    valid(a);
    let side = ed(a, Edit::Split { panel: root_id, dir: Dir::Right }).panel.expect("made");
    ed(a, Edit::Resize { panel: root_id, delta: 0.22, floor: 0.0 });
    assert_eq!((a.panel(root_id).expect("there").weight, a.panel(side).expect("there").weight), (0.72, 0.28));
    // A tab can take a removed View back.
    ed(a, Edit::InsertTab { tab: made, to: side, after: None, select: true });
    ed(a, Edit::InsertTab { tab: kept.clone(), to: root_id, after: None, select: true });
    assert_eq!(a.panel_with(&kept).map(|p| p.id), Some(root_id));
    valid(a);
    // A new document has a panel once cleared; one that never splits keeps
    // its axis.
    let mut fresh: Arrangement<View> = Arrangement::new();
    let root_id = ed(&mut fresh, Edit::Clear).panel.expect("a root");
    ed(&mut fresh, Edit::InsertTab { tab: "terminal#1".into(), to: root_id, after: None, select: true });
    assert_eq!((root(&fresh).selected.as_deref(), fresh.root_axis), (Some("terminal#1"), Axis::Column));
}

#[test]
fn a_restores_detach_and_insert_change_no_other_panel_and_select_only_if_it_was() {
    let panels = build("panels:{0.5:{terminal:{selected} text} 0.5:{jackstay}}");
    let sidebar = build("panels:{sidebar_section:{selected} log}");
    for source_first in [false, true] {
        let (mut source, mut destination) =
            if source_first { (sidebar.a.clone(), panels.a.clone()) } else { (panels.a.clone(), sidebar.a.clone()) };
        let moving = if source_first { root(&source).tabs[1].clone() } else { child(&source, 0).tabs[1].clone() };
        let leaf = if source_first { child(&destination, 0).id } else { root(&destination).id };
        let kept_selected = destination.panel(leaf).expect("there").selected.clone();
        assert!(ed(&mut source, Edit::DetachTab { tab: moving.clone() }).applied);
        assert!(!ed(&mut source, Edit::DetachTab { tab: moving.clone() }).applied);
        assert!(source.panel_with(&moving).is_none() && !removed(&source, &moving));
        let after = destination.panel(leaf).and_then(|p| p.tabs.last().cloned());
        assert!(ed(&mut destination, Edit::InsertTab { tab: moving.clone(), to: leaf, after, select: false }).applied);
        let leaf = destination.panel(leaf).expect("there");
        assert_eq!((leaf.selected.clone(), leaf.tabs.last()), (kept_selected, Some(&moving)));
        valid(&source);
        valid(&destination);
    }
    let mut a = panels.a.clone();
    let (chosen, right) = (child(&a, 0).tabs[0].clone(), last(&a).id);
    assert!(ed(&mut a, Edit::InsertTab { tab: chosen.clone(), to: right, after: None, select: true }).applied);
    assert_eq!((last(&a).selected.as_ref(), &last(&a).tabs[0]), (Some(&chosen), &chosen));
    assert!(child(&a, 0).selected.is_none());
}

/// The weight format is the native config formatter's, ties rounding up.
#[test]
fn weights_quantize_to_six_places_ties_up() {
    assert_eq!(quantize(1.0 / 3.0), 0.333333);
    assert_eq!(quantize(0.0078125), 0.007813);
    assert_eq!(quantize(0.5), 0.5);
}
