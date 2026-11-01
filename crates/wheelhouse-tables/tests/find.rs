//! Find in the canned records table as a host drives it: the count says how
//! many cells match, stepping visits each of them once in order and then
//! wraps, every match stepped to is marked, and closing find clears it all.
use binnacle_core::{Action, Axis, FindQuery, TextQuery, Viewport};
use std::collections::HashSet;
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
