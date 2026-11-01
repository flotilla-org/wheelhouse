//! The canned tables as a host drives them: whatever the viewport and
//! scroll, every Window the Grid asks for is filled; and the live table's
//! clock only lists more tickers and always says when it changes next.
use binnacle_core::{Action, Axis, Grid, Viewport};
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

/// A long gap between frames (a hidden tab, a sleep) doesn't replay the
/// missed ticks: one frame lists at most one new ticker.
#[hegel::test(test_cases = 50)]
fn a_long_gap_starts_the_live_clock_again(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let (mut table, mut grid) = Table::open("live", METRICS).expect("live");
    let start = Duration::from_micros(tc.draw(gs::integers::<u64>().max_value(1_000_000)));
    table.tick(&mut grid, start);
    table.pump(&mut grid, |_| {});
    let count = grid.snapshot().count(Axis::Rows);
    let gap = Duration::from_secs(tc.draw(gs::integers::<u64>().min_value(10).max_value(36_000)));
    table.tick(&mut grid, start + gap);
    table.pump(&mut grid, |_| {});
    let after = grid.snapshot().count(Axis::Rows);
    assert!(after <= count + 1, "{after} rows after a {gap:?} gap, from {count}");
}

/// A given table shows each cell's own text at its row and column, and
/// names its columns by its header row.
#[hegel::test(test_cases = 100)]
fn a_given_table_shows_its_cells_by_row_and_column(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let columns = tc.draw(gs::integers::<usize>().min_value(1).max_value(5));
    let header: Vec<String> = (0..columns).map(|c| format!("h{c}")).collect();
    let row_count = tc.draw(gs::integers::<usize>().max_value(40));
    let rows: Vec<Vec<String>> = (0..row_count).map(|_| (0..columns).map(|_| tc.draw(gs::text().max_size(12))).collect()).collect();
    let right: Vec<bool> = (0..columns).map(|_| tc.draw(gs::booleans())).collect();
    let (mut table, mut grid) = Table::from_cells(header.clone(), rows.clone(), right.clone(), METRICS);
    for _ in 0..3 {
        table.pump(&mut grid, |_| {});
        table.fill(&mut grid, Viewport::new(2000.0, 2000.0));
    }
    let snapshot = grid.snapshot();
    assert_eq!(snapshot.count(Axis::Rows), row_count as u64);
    for row in snapshot.strip(Axis::Rows) {
        let r = row.key.expect("a filled row");
        for col in snapshot.strip(Axis::Cols) {
            let c = col.key.expect("a filled column");
            let cell = table.cell(r, c);
            assert_eq!(cell.text, rows[r.0 as usize][c.0 as usize]);
            assert_eq!(cell.align == wheelhouse_tables::Align::Right, right[c.0 as usize]);
        }
    }
    for (c, name) in header.iter().enumerate() {
        assert_eq!(&table.header(Axis::Cols, binnacle_core::Key(c as u64), c as u64), name);
    }
}

/// A new text size resizes a table: rows and the column header band by the
/// line's change, columns and the row header band by the digit's, a given
/// table's columns sized to their text too. The first row and column shown
/// stay first, unless the end clamp pulls the view back (to the start, when
/// everything fits).
#[hegel::test(test_cases = 100)]
fn new_metrics_resize_a_table_and_keep_its_first_row_and_column(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let name = *tc.draw(gs::sampled_from(NAMES.iter().chain(&["cells"]).collect::<Vec<_>>()));
    let (mut table, mut grid) = match name {
        "cells" => {
            let rows = (0..200).map(|r| vec![r.to_string(), "x".repeat(r % 30)]).collect();
            Table::from_cells(vec!["n".into(), "text".into()], rows, vec![true, false], METRICS)
        }
        _ => Table::open(name, METRICS).expect("a canned table"),
    };
    let viewport = Viewport::new(800.0, 600.0);
    let frame = |table: &mut Table, grid: &mut Grid| {
        for _ in 0..3 {
            table.pump(grid, |_| {});
            table.fill(grid, viewport);
        }
    };
    frame(&mut table, &mut grid);
    for axis in Axis::ALL {
        let fraction = tc.draw(gs::floats::<f64>().min_value(0.0).max_value(1.0));
        grid.action(Action::ScrollTo { axis, fraction });
    }
    frame(&mut table, &mut grid);
    let before = Axis::ALL.map(|axis| {
        let snapshot = grid.snapshot();
        let sizes: Vec<_> = snapshot.strip(axis).iter().map(|i| (i.key, i.size)).collect();
        (snapshot.strip(axis).first().map(|i| i.position), sizes, snapshot.header(axis))
    });
    let metrics = Metrics {
        line: tc.draw(gs::floats::<f32>().min_value(6.0).max_value(60.0)),
        digit: tc.draw(gs::floats::<f32>().min_value(3.0).max_value(30.0)),
    };
    table.set_metrics(&mut grid, metrics);
    frame(&mut table, &mut grid);
    let snapshot = grid.snapshot();
    let near = |a: f32, b: f32| (a - b).abs() <= 1e-3 * b.max(1.0);
    for axis in Axis::ALL {
        let scale = match axis {
            Axis::Rows => metrics.line / METRICS.line,
            Axis::Cols => metrics.digit / METRICS.digit,
        };
        let (first, sizes, header) = &before[axis.index()];
        assert!(near(snapshot.header(axis), header * scale), "{name}: {axis:?} header {} from {header}", snapshot.header(axis));
        for item in snapshot.strip(axis) {
            if let Some((_, size)) = sizes.iter().find(|(key, _)| *key == item.key) {
                assert!(near(item.size, size * scale), "{name}: {axis:?} {:?} is {} from {size}", item.key, item.size);
            }
        }
        let now = snapshot.strip(axis).first().map(|i| i.position);
        // The end clamp: scrolled to the end, or everything fits from the start.
        let thumb = snapshot.thumb(axis);
        if now != *first {
            assert!(
                thumb.fraction == 1.0 || (thumb.size >= 1.0 && now == Some(0)),
                "{name}: {axis:?} first {now:?}, was {first:?}, short of the end"
            );
        }
    }
}
