//! Equivalence, edits (#516, test 1): every edit the core applies gives
//! what the native shell's C rule it replaced gives. Arrangements are grown
//! from empty by clear, add, split, tab inserts and provider names, then
//! edited by every kind of edit with random tabs hidden; after every step
//! the documents are equal (IDs, `next_id`, the root's axis, weights bit for
//! bit, tabs in order, Selected Views, names, Presentation State moves,
//! restore marks and removed tabs), and so are the outcomes and the
//! validity verdicts.
#![cfg(oracle)]
use hegel::generators as gs;
use hegel::TestCase;
use wheelhouse_arrangement_oracle::{quantize as c_quantize, Oracle};
use wheelhouse_core::arrangement::generate::{build_step, draw_dir, draw_hidden, draw_tab, edit_step, Step, Tab};
use wheelhouse_core::arrangement::{drop_across, problem, quantize, Arrangement};

/// Applies `step` to both, then asserts they agree.
fn step_both(rust: &mut Arrangement<Tab>, c: &mut Oracle, step: &Step) {
    let outcome = step.apply(rust);
    let expected = match step {
        Step::Edit { edit, hidden } => Some(c.apply(edit, hidden)),
        Step::Name { panel, name } => {
            if rust.panel(*panel).is_some() {
                c.set_named(*panel, *name);
            }
            None
        }
    };
    assert_eq!(outcome, expected, "outcomes differ after {step:?}");
    assert_eq!(*rust, c.dump(), "documents differ after {step:?}");
    assert_eq!(problem(rust), c.problem(), "validity differs after {step:?}");
}

#[hegel::test(test_cases = 400)]
fn every_edit_matches_the_c(tc: TestCase) {
    let mut rust = Arrangement::new();
    let mut c = Oracle::new();
    let mut next_tab = 1;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(30)) {
        let step = build_step(&tc, &rust, &mut next_tab);
        step_both(&mut rust, &mut c, &step);
    }
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(20)) {
        let step = edit_step(&tc, &rust, next_tab + 1);
        step_both(&mut rust, &mut c, &step);
    }
}

/// A drop whose tab comes from another arrangement (the sidebar's into a
/// workspace's, say) edits both as the shell's drop command does.
#[hegel::test(test_cases = 200)]
fn drops_from_another_arrangement_match_the_c(tc: TestCase) {
    let (mut a, mut b) = (Arrangement::new(), Arrangement::new());
    let (mut ca, mut cb) = (Oracle::new(), Oracle::new());
    let mut next_tab = 1;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(10)) {
        let step = build_step(&tc, &a, &mut next_tab);
        step_both(&mut a, &mut ca, &step);
        let step = build_step(&tc, &b, &mut next_tab);
        step_both(&mut b, &mut cb, &step);
    }
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(4)) {
        let tab = draw_tab(&tc, &b, next_tab);
        let to = wheelhouse_core::arrangement::generate::draw_panel(&tc, &a);
        let dir = tc.draw(gs::booleans()).then(|| draw_dir(&tc));
        let after = tc.draw(gs::booleans()).then(|| draw_tab(&tc, &a, next_tab));
        let mut hidden = draw_hidden(&tc, &a);
        hidden.extend(draw_hidden(&tc, &b));
        let (out, settled) = drop_across(&mut a, &mut b, &tab, to, dir, after, &|t| hidden.contains(t));
        let (c_out, c_changed, c_heir) = ca.drop_across(&mut cb, tab, to, dir, after, &hidden);
        let what = format!("drop {tab} into {to:?} {dir:?} after {after:?}, hidden {hidden:?}");
        assert_eq!((out.applied, out.panel, out.source, out.heir), (c_out.applied, c_out.panel, c_out.source, c_out.heir), "{what}");
        assert_eq!((settled.applied, settled.heir), (c_changed, c_heir), "{what}");
        assert_eq!(a, ca.dump(), "{what}");
        assert_eq!(b, cb.dump(), "{what}");
        // Either side may go on being edited.
        let step = edit_step(&tc, &b, next_tab);
        step_both(&mut b, &mut cb, &step);
    }
}

/// The weight format: every quantized weight is the C's, bit for bit,
/// across the range weights take and the format's ties (1/128 rounds up).
#[hegel::test(test_cases = 2000)]
fn quantize_matches_the_c(tc: TestCase) {
    let weight = if tc.draw(gs::booleans()) {
        tc.draw(gs::floats::<f32>().min_value(-4.0).max_value(4.0).allow_nan(false).allow_infinity(false))
    } else {
        // Dyadic fractions: exact ties of the six-place format among them.
        let numerator = tc.draw(gs::integers::<i32>().min_value(-4096).max_value(4096));
        let shift = tc.draw(gs::integers::<i32>().min_value(0).max_value(24));
        numerator as f32 / (1u32 << shift) as f32
    };
    assert_eq!(quantize(weight).to_bits(), c_quantize(weight).to_bits(), "quantize({weight:e})");
}

/// And for values far outside weights: tiny, huge, infinite and NaN.
#[test]
fn quantize_matches_the_c_at_the_edges() {
    let mut values =
        vec![0.0f32, -0.0, f32::INFINITY, f32::NEG_INFINITY, f32::NAN, -f32::NAN, f32::MAX, f32::MIN, f32::MIN_POSITIVE, 1e-45];
    values.extend((-45..39).map(|e| 10f32.powi(e)));
    values.extend((-45..39).map(|e| 3.7f32 * 10f32.powi(e)));
    for weight in values {
        assert_eq!(quantize(weight).to_bits(), c_quantize(weight).to_bits(), "quantize({weight:e})");
    }
}
