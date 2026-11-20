//! Copying a table's selection as a host drives it: the cells as drawn, as
//! tab-separated lines a spreadsheet pastes, taken once; the focused cell
//! alone without a selection; and nothing past binnacle's limit, which the
//! status line says for a while.
use binnacle_core::{Action, Axis, FocusStep, Grid, Key, Viewport};
use std::time::Duration;
use wheelhouse_tables::{Metrics, Table};

const METRICS: Metrics = Metrics { line: 20.0, digit: 8.0 };

/// One frame: the effects answered, then the Windows filled.
fn frame(table: &mut Table, grid: &mut Grid) {
    table.pump(grid, |_| {});
    table.fill(grid, Viewport::new(4000.0, 4000.0));
}

/// Selects the block of `rows` by `cols` items from the cell at `row` and
/// `col`, by focus and extending moves as the keys make them.
fn select(table: &mut Table, grid: &mut Grid, row: u64, col: u64, rows: u64, cols: u64) {
    grid.set_focus(Key(row), Key(col));
    frame(table, grid);
    for (axis, n) in [(Axis::Rows, rows), (Axis::Cols, cols)] {
        for _ in 1..n {
            grid.action(Action::MoveFocus { axis, by: FocusStep::Item, forward: true, extend: true });
            frame(table, grid);
        }
    }
}

/// A spreadsheet's field: quoted when it holds a tab, a line break or a
/// quote, each quote doubled.
fn field(text: &str) -> String {
    match text.contains(['\t', '\n', '\r', '"']) {
        true => format!("\"{}\"", text.replace('"', "\"\"")),
        false => text.to_owned(),
    }
}

#[hegel::test(test_cases = 100)]
fn a_selection_copies_its_cells_as_tab_separated_lines(tc: hegel::TestCase) {
    use hegel::generators as gs;
    let columns = tc.draw(gs::integers::<usize>().min_value(1).max_value(5));
    let count = tc.draw(gs::integers::<usize>().min_value(1).max_value(6));
    let text = gs::sampled_from(vec!["", "plain", "tab\there", "two\nlines", "cr\rhere", "say \"hi\"", "Bergen"]);
    let rows: Vec<Vec<String>> = (0..count).map(|_| (0..columns).map(|_| tc.draw(&text).to_owned()).collect()).collect();
    let row = tc.draw(gs::integers::<u64>().max_value(count as u64 - 1));
    let col = tc.draw(gs::integers::<u64>().max_value(columns as u64 - 1));
    let height = tc.draw(gs::integers::<u64>().min_value(1).max_value(count as u64 - row));
    let width = tc.draw(gs::integers::<u64>().min_value(1).max_value(columns as u64 - col));
    let header = (0..columns).map(|c| format!("h{c}")).collect();
    let (mut table, mut grid) = Table::from_cells(header, rows.clone(), vec![], METRICS);
    frame(&mut table, &mut grid);
    select(&mut table, &mut grid, row, col, height, width);

    grid.action(Action::Copy);
    frame(&mut table, &mut grid);
    let want: Vec<String> = (row..row + height)
        .map(|r| (col..col + width).map(|c| field(&rows[r as usize][c as usize])).collect::<Vec<_>>().join("\t"))
        .collect();
    assert_eq!(table.take_copy(), Some(want.join("\n")));
    assert_eq!(table.status(), None);
}

#[test]
fn the_focused_cell_alone_copies_without_a_selection_and_is_taken_once() {
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    frame(&mut table, &mut grid);
    assert_eq!(table.take_copy(), None, "nothing copied yet");
    grid.set_focus(Key(4), Key(2));
    frame(&mut table, &mut grid);

    grid.action(Action::Copy);
    frame(&mut table, &mut grid);
    let weight = table.cell(Key(4), Key(2)).text;
    assert!(!weight.is_empty());
    assert_eq!(table.take_copy(), Some(weight), "the weight as drawn");
    assert_eq!(table.take_copy(), None, "taken once, then gone");
    frame(&mut table, &mut grid);
    assert_eq!(table.take_copy(), None, "a frame copies nothing again");
}

#[test]
fn a_huge_selection_past_the_limit_copies_nothing_and_says_so_for_a_while() {
    let (mut table, mut grid) = Table::open("huge", METRICS).expect("huge");
    frame(&mut table, &mut grid);
    grid.set_focus(Key(0), Key(0));
    frame(&mut table, &mut grid);
    grid.action(Action::Copy);
    frame(&mut table, &mut grid);
    let one = table.take_copy();
    assert_eq!(one, Some(table.cell(Key(0), Key(0)).text), "one cell is within the limit");

    grid.action(Action::SelectAll);
    grid.action(Action::Copy);
    frame(&mut table, &mut grid);
    assert_eq!(table.take_copy(), None, "nothing past the limit");
    let status = table.status().expect("the status line says why");
    assert!(status.starts_with("Too many cells to copy ("), "{status}");
    let cells = grid.snapshot().count(Axis::Rows) * grid.snapshot().count(Axis::Cols);
    let digits: String = status.chars().filter(char::is_ascii_digit).collect();
    assert_eq!(digits, cells.to_string(), "{status} counts every cell");

    // The note stands for a few seconds from the tick after the copy.
    let start = Duration::from_secs(100);
    let wake = table.tick(&mut grid, start).expect("the note's end wakes the host");
    assert!(wake >= Duration::from_secs(2) && wake <= Duration::from_secs(10), "{wake:?}");
    assert!(table.tick(&mut grid, start + wake / 2).is_some());
    assert!(table.status().is_some(), "still said halfway");
    assert_eq!(table.tick(&mut grid, start + wake), None);
    assert_eq!(table.status(), None, "gone at its end");

    // A copy that fits ends the note at once.
    grid.action(Action::SelectAll);
    grid.action(Action::Copy);
    frame(&mut table, &mut grid);
    assert!(table.status().is_some());
    grid.set_focus(Key(0), Key(0));
    grid.action(Action::Copy);
    frame(&mut table, &mut grid);
    assert!(table.take_copy().is_some());
    assert_eq!(table.status(), None, "a copy that fits says nothing");
}
