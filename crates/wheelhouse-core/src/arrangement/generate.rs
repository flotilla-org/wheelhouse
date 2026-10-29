//! Generators for arrangement tests: here and in the C oracle's equivalence
//! tests (crates/wheelhouse-arrangement-oracle). Tabs are numbers from 1;
//! a named panel's name is "n" and a number, as the oracle keeps it.
//!
//! An arrangement is grown from empty by [`build_step`]s (clear, add,
//! split, tab inserts and provider names), then edited by [`edit_step`]s,
//! which draw every [`Edit`] with targets that mostly exist and sometimes
//! don't (no panel, an unallocated ID, an unknown tab), weights across the
//! boundaries the rules test (zero, negative, one, ties of the weight
//! format), and random tabs hidden.
use hegel::generators as gs;
use hegel::TestCase;

use super::{apply, Arrangement, Dir, Edit, Outcome, PanelId};

pub type Tab = u64;

/// One step of a generated session.
#[derive(Clone, Debug, PartialEq)]
pub enum Step {
    Edit {
        edit: Edit<Tab>,
        hidden: Vec<Tab>,
    },
    /// A provider names a panel (0: unnames it), as the workspace store
    /// does with a Workspace Layout's panel IDs.
    Name {
        panel: PanelId,
        name: u64,
    },
}

/// A panel's name in tests.
pub fn name(n: u64) -> Option<String> {
    (n != 0).then(|| format!("n{n}"))
}

impl Step {
    /// Applies it to `a`; an edit's outcome, none for a name.
    pub fn apply(&self, a: &mut Arrangement<Tab>) -> Option<Outcome<Tab>> {
        match self {
            Step::Edit { edit, hidden } => Some(apply(a, edit, &|tab| hidden.contains(tab))),
            Step::Name { panel, name: n } => {
                if let Some(p) = a.panel_mut(*panel) {
                    p.named = name(*n);
                }
                None
            }
        }
    }
}

fn pick<T: Clone + Send + Sync + std::fmt::Debug>(tc: &TestCase, items: &[T]) -> Option<T> {
    (!items.is_empty()).then(|| tc.draw(gs::sampled_from(items)))
}

/// Every panel ID in `a`.
pub fn panel_ids(a: &Arrangement<Tab>) -> Vec<PanelId> {
    a.panels().map(|p| p.id).collect()
}

/// Every tab in `a`, in tree order.
pub fn tabs(a: &Arrangement<Tab>) -> Vec<Tab> {
    a.panels().flat_map(|p| p.tabs.iter().copied()).collect()
}

/// A panel: mostly one there is, else none, the next unallocated, or far
/// out of range.
pub fn draw_panel(tc: &TestCase, a: &Arrangement<Tab>) -> PanelId {
    let ids = panel_ids(a);
    match tc.draw(gs::integers::<u8>().max_value(9)) {
        0 => PanelId(0),
        1 => PanelId(a.next_id),
        2 => PanelId(a.next_id + 7),
        _ => pick(tc, &ids).unwrap_or(PanelId(0)),
    }
}

/// A panel there is, nearly always: what a growing arrangement edits.
fn draw_existing_panel(tc: &TestCase, a: &Arrangement<Tab>) -> PanelId {
    if tc.draw(gs::integers::<u8>().max_value(19)) == 0 {
        draw_panel(tc, a)
    } else {
        pick(tc, &panel_ids(a)).unwrap_or(PanelId(0))
    }
}

/// A tab: mostly one there is, else one that is nowhere (up to `fresh`).
pub fn draw_tab(tc: &TestCase, a: &Arrangement<Tab>, fresh: Tab) -> Tab {
    let there = tabs(a);
    if there.is_empty() || tc.draw(gs::integers::<u8>().max_value(4)) == 0 {
        tc.draw(gs::integers::<u64>().min_value(1).max_value(fresh.max(1)))
    } else {
        pick(tc, &there).expect("nonempty")
    }
}

/// A weight: the boundaries the rules test, or any in [-0.5, 2.5].
pub fn draw_weight(tc: &TestCase) -> f32 {
    const EDGES: [f32; 9] = [0.0, -0.0, 1.0, 0.5, 0.25, 0.0078125, 0.01, -0.25, 1.0 / 3.0];
    if tc.draw(gs::booleans()) {
        tc.draw(gs::sampled_from(&EDGES[..]))
    } else {
        tc.draw(gs::floats::<f32>().min_value(-0.5).max_value(2.5).allow_nan(false).allow_infinity(false))
    }
}

pub fn draw_dir(tc: &TestCase) -> Dir {
    tc.draw(gs::sampled_from(&Dir::ALL[..]))
}

pub fn draw_option_dir(tc: &TestCase) -> Option<Dir> {
    tc.draw(gs::booleans()).then(|| draw_dir(tc))
}

/// Tabs a settle passes over: any of those there are.
pub fn draw_hidden(tc: &TestCase, a: &Arrangement<Tab>) -> Vec<Tab> {
    tabs(a).into_iter().filter(|_| tc.draw(gs::integers::<u8>().max_value(3)) == 0).collect()
}

/// A tab after which another goes: one of `panel`'s, or none, or one that
/// is elsewhere.
fn draw_after(tc: &TestCase, a: &Arrangement<Tab>, panel: PanelId, fresh: Tab) -> Option<Tab> {
    let mine = a.panel(panel).map(|p| p.tabs.clone()).unwrap_or_default();
    match tc.draw(gs::integers::<u8>().max_value(4)) {
        0 => None,
        1 => Some(draw_tab(tc, a, fresh)),
        _ => pick(tc, &mine),
    }
}

/// A step growing an arrangement: clear, add, split, a new tab, a name.
/// `next_tab` is the next tab never used.
pub fn build_step(tc: &TestCase, a: &Arrangement<Tab>, next_tab: &mut Tab) -> Step {
    let edit = match tc.draw(gs::integers::<u8>().max_value(19)) {
        0 => Edit::Clear,
        1..=4 => {
            let parent = if tc.draw(gs::integers::<u8>().max_value(5)) == 0 { None } else { Some(draw_existing_panel(tc, a)) };
            let weight = if tc.draw(gs::booleans()) { 0.0 } else { draw_weight(tc) };
            Edit::Add { parent, weight }
        }
        5..=9 => Edit::Split { panel: draw_existing_panel(tc, a), dir: draw_dir(tc) },
        10 => {
            let panel = draw_existing_panel(tc, a);
            let n = if tc.draw(gs::booleans()) { tc.draw(gs::integers::<u64>().min_value(1).max_value(3)) } else { 0 };
            return Step::Name { panel, name: n };
        }
        _ => {
            let leaves: Vec<PanelId> = a.panels().filter(|p| p.children.is_empty()).map(|p| p.id).collect();
            let to =
                if tc.draw(gs::integers::<u8>().max_value(5)) == 0 { draw_panel(tc, a) } else { pick(tc, &leaves).unwrap_or(PanelId(0)) };
            let tab = *next_tab;
            *next_tab += 1;
            let after = draw_after(tc, a, to, tab);
            Edit::InsertTab { tab, to, after, select: tc.draw(gs::booleans()) }
        }
    };
    Step::Edit { edit, hidden: vec![] }
}

/// Any edit, with random tabs hidden. `fresh` bounds the tabs that may be
/// nowhere.
pub fn edit_step(tc: &TestCase, a: &Arrangement<Tab>, fresh: Tab) -> Step {
    let edit = match tc.draw(gs::integers::<u8>().max_value(16)) {
        0 => Edit::Clear,
        1 => Edit::Discard,
        2 => Edit::Split { panel: draw_panel(tc, a), dir: draw_dir(tc) },
        3 => Edit::Add { parent: tc.draw(gs::booleans()).then(|| draw_panel(tc, a)), weight: draw_weight(tc) },
        4 => Edit::Remove { panel: draw_panel(tc, a) },
        5 => Edit::Close { panel: draw_panel(tc, a) },
        6 => {
            let to = draw_panel(tc, a);
            Edit::InsertTab { tab: draw_tab(tc, a, fresh), to, after: draw_after(tc, a, to, fresh), select: tc.draw(gs::booleans()) }
        }
        7 => Edit::DetachTab { tab: draw_tab(tc, a, fresh) },
        8 => Edit::RemoveTab { tab: draw_tab(tc, a, fresh) },
        9 | 10 => {
            let to = draw_panel(tc, a);
            let tab = (tc.draw(gs::integers::<u8>().max_value(5)) != 0).then(|| draw_tab(tc, a, fresh));
            Edit::Drop { tab, to, dir: draw_option_dir(tc), after: draw_after(tc, a, to, fresh) }
        }
        11 => Edit::Settle { panel: draw_panel(tc, a), dir: draw_option_dir(tc), may_close: tc.draw(gs::booleans()) },
        12 => {
            let panel = draw_panel(tc, a);
            Edit::Select { panel, tab: draw_after(tc, a, panel, fresh) }
        }
        13 => Edit::Reorder { panel: draw_panel(tc, a), after: tc.draw(gs::booleans()).then(|| draw_panel(tc, a)) },
        14 => {
            let delta = tc.draw(gs::floats::<f32>().min_value(-1.5).max_value(1.5).allow_nan(false).allow_infinity(false));
            let floor = tc.draw(gs::sampled_from(&[0.0f32, 0.05, 0.1, 0.25, 0.5, 0.6][..]));
            Edit::Resize { panel: draw_panel(tc, a), delta, floor }
        }
        15 => Edit::Equalize { panel: draw_panel(tc, a) },
        _ => Edit::Readdress { from: draw_tab(tc, a, fresh), to: draw_tab(tc, a, fresh + 3) },
    };
    Step::Edit { edit, hidden: draw_hidden(tc, a) }
}

/// A grown arrangement, by up to `steps` build steps, with the steps.
pub fn build(tc: &TestCase, steps: usize) -> (Arrangement<Tab>, Vec<Step>, Tab) {
    let mut a = Arrangement::new();
    let mut next_tab = 1;
    let mut log = vec![];
    let count = tc.draw(gs::integers::<usize>().max_value(steps));
    for _ in 0..count {
        let step = build_step(tc, &a, &mut next_tab);
        step.apply(&mut a);
        log.push(step);
    }
    (a, log, next_tab)
}
