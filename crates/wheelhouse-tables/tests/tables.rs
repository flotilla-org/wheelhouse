//! The canned tables as a host drives them: whatever the viewport and
//! scroll, every Window the Grid asks for is filled; and the live table's
//! clock only lists more tickers and always says when it changes next.
use binnacle_core::{Action, Axis, Viewport};
use std::time::Duration;
use wheelhouse_tables::{Metrics, Table, NAMES};

const METRICS: Metrics = Metrics { line: 20.0, digit: 8.0 };

#[hegel::test(test_cases = 100)]
fn every_window_the_grid_asks_for_is_filled(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let name = NAMES[tc.draw(gs::integers::<usize>().max_value(NAMES.len() - 1))];
    let (mut table, mut grid) = Table::open(name, METRICS).expect("a canned table");
    let width = tc.draw(gs::floats::<f32>().min_value(1.0).max_value(4000.0));
    let height = tc.draw(gs::floats::<f32>().min_value(1.0).max_value(4000.0));
    let fraction = tc.draw(gs::floats::<f64>().min_value(0.0).max_value(1.0));
    table.fill(&mut grid, Viewport::new(width, height));
    grid.action(Action::ScrollTo { axis: Axis::Rows, fraction });
    table.pump(&mut grid, |_| {});
    table.fill(&mut grid, Viewport::new(width, height));
    let snapshot = grid.snapshot();
    for axis in Axis::ALL {
        for item in snapshot.strip(axis) {
            assert!(item.key.is_some(), "{name}: {axis:?} position {} is unfilled", item.position);
        }
    }
    for row in snapshot.strip(Axis::Rows) {
        for col in snapshot.strip(Axis::Cols) {
            let (Some(r), Some(c)) = (row.key, col.key) else { continue };
            // Every shown cell has text to draw, empty for a blank.
            let _ = table.cell(r, c);
        }
    }
}

#[hegel::test(test_cases = 50)]
fn the_live_clock_only_lists_more_and_says_when_it_next_changes(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let (mut table, mut grid) = Table::open("live", METRICS).expect("live");
    let steps = tc.draw(gs::vecs(gs::integers::<u64>().max_value(3_000_000)).max_size(20));
    let mut now = Duration::ZERO;
    let mut count = grid.snapshot().count(Axis::Rows);
    for step in steps {
        now += Duration::from_micros(step);
        let wake = table.tick(&mut grid, now).expect("live always changes again");
        assert!(!wake.is_zero() && wake <= Duration::from_millis(250), "wake {wake:?}");
        table.pump(&mut grid, |_| {});
        table.fill(&mut grid, Viewport::new(800.0, 600.0));
        let now_count = grid.snapshot().count(Axis::Rows);
        assert!(now_count >= count, "{now_count} < {count}");
        count = now_count;
    }
}
