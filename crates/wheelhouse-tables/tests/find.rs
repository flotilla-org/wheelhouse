//! Find in the canned records table as a host drives it: the count says how
//! many cells match, stepping visits each of them once in order and then
//! wraps, every match stepped to is marked, and closing find clears it all.
use binnacle_core::{Action, Axis, FindQuery, TextQuery, Viewport};
use std::collections::HashSet;
use std::time::Duration;
use wheelhouse_tables::{Metrics, Stepped, Table};

const METRICS: Metrics = Metrics { line: 20.0, digit: 8.0 };
const WORDS: [&str; 6] = ["bosun", "galley", "Bergen", "Halifax", "Funchal", "ensign"];

#[hegel::test(test_cases = 30)]
fn stepping_visits_every_counted_match_once_then_wraps(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let word = WORDS[tc.draw(gs::integers::<usize>().max_value(WORDS.len() - 1))];
    let upper = tc.draw(gs::booleans());
    let text = if upper { word.to_uppercase() } else { word.to_owned() };
    let by = if tc.draw(gs::booleans()) { Axis::Cols } else { Axis::Rows };
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    let viewport = Viewport::new(800.0, 600.0);
    table.fill(&mut grid, viewport);
    grid.set_find(Some(FindQuery::new(TextQuery::new(text)).by(by)));
    table.pump(&mut grid, |_| {});
    let count = table.find_count().expect("find is open");
    assert!(!count.partial && count.total > 0, "{word}: {count:?}");
    assert_eq!(table.find_stepped(), None, "no step yet");

    let mut seen = HashSet::new();
    for i in 0..count.total {
        grid.action(Action::FindNext);
        table.pump(&mut grid, |_| {});
        table.fill(&mut grid, viewport);
        let at = grid.snapshot().find_match().expect("a step lands");
        assert_eq!(table.find_stepped(), Some(Stepped { index: Some(i), wrapped: false }), "{word} step {i}");
        assert!(seen.insert((at.row, at.col)), "{word}: step {i} came back to {at:?}");
        assert_eq!(table.find_marks(&[at.row], &[at.col]), vec![true], "{word}: {at:?} unmarked");
    }
    grid.action(Action::FindNext);
    table.pump(&mut grid, |_| {});
    assert_eq!(table.find_stepped(), Some(Stepped { index: Some(0), wrapped: true }));

    grid.set_find(None);
    table.pump(&mut grid, |_| {});
    assert_eq!(table.find_count(), None);
    assert_eq!(table.find_stepped(), None);
    let (row, col) = seen.into_iter().next().expect("a match");
    assert_eq!(table.find_marks(&[row], &[col]), vec![false]);
}

/// Matching case finds a word only as the data spells it.
#[test]
fn matching_case_finds_only_the_data_spelling() {
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    let mut total = |text: &str, case_sensitive: bool| {
        grid.set_find(Some(FindQuery::new(TextQuery::new(text).case_sensitive(case_sensitive))));
        table.pump(&mut grid, |_| {});
        table.find_count().expect("find is open").total
    };
    let any = total("bergen", false);
    assert!(any > 0);
    assert_eq!(total("Bergen", true), any);
    assert_eq!(total("bergen", true), 0);
}

/// A step asked for under one query and a new query in the same pump: the
/// step is the old query's, so the new one has none yet.
#[test]
fn a_new_query_drops_a_step_asked_for_the_old_one() {
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    let viewport = Viewport::new(800.0, 600.0);
    table.fill(&mut grid, viewport);
    grid.set_find(Some(FindQuery::new(TextQuery::new("bergen"))));
    table.pump(&mut grid, |_| {});
    grid.action(Action::FindNext);
    grid.set_find(Some(FindQuery::new(TextQuery::new("bosun"))));
    table.pump(&mut grid, |_| {});
    assert_eq!(table.find_stepped(), None);
}

/// The count is taken again when a View Spec changes which rows are shown,
/// and when rows come.
#[test]
fn the_count_follows_a_view_spec_and_new_rows() {
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    let viewport = Viewport::new(800.0, 600.0);
    table.fill(&mut grid, viewport);
    grid.set_find(Some(FindQuery::new(TextQuery::new("bergen"))));
    table.pump(&mut grid, |_| {});
    let all = table.find_count().expect("find is open").total;
    grid.set_quick_filter(Axis::Rows, Some(TextQuery::new("bosun")));
    table.pump(&mut grid, |_| {});
    table.fill(&mut grid, viewport);
    let filtered = table.find_count().expect("find is open").total;
    assert!(filtered < all, "{filtered} of {all} once filtered");

    // The live table lists a new ANCHOR now and then.
    let (mut table, mut grid) = Table::open("live", METRICS).expect("live");
    table.fill(&mut grid, viewport);
    grid.set_find(Some(FindQuery::new(TextQuery::new("anchor"))));
    table.pump(&mut grid, |_| {});
    let before = table.find_count().expect("find is open").total;
    let mut now = Duration::from_secs(1);
    let mut after = before;
    for _ in 0..200 {
        now += Duration::from_millis(250);
        table.tick(&mut grid, now);
        table.pump(&mut grid, |_| {});
        after = table.find_count().expect("find is open").total;
        if after != before {
            break;
        }
    }
    assert_eq!(after, before + 1);
}
