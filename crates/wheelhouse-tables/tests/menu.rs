//! The column menu in the canned records table as a host drives it (#577):
//! which columns open one, the conditions its rows write and the rows they
//! leave, the chips naming them, and the checklist narrowed by the other
//! columns.
use binnacle_core::{Axis, Condition, ConditionState, Grid, Key, TextQuery, Value, Viewport};
use wheelhouse_tables::{Metrics, Table};

const METRICS: Metrics = Metrics { line: 20.0, digit: 8.0 };
const VIEWPORT: Viewport = Viewport { width: 800.0, height: 600.0 };
const ID: Key = Key(0);
const WEIGHT: Key = Key(2);
const PORT: Key = Key(4);
const SHIPPED: Key = Key(5);

fn records() -> (Table, Grid) {
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    table.fill(&mut grid, VIEWPORT);
    (table, grid)
}

/// Answers what the menu asked for, as a frame does, and the row count.
fn answer(table: &mut Table, grid: &mut Grid) -> u64 {
    table.pump(grid, |_| {});
    table.fill(grid, VIEWPORT);
    grid.snapshot().count(Axis::Rows)
}

fn conditions(grid: &Grid) -> Vec<Condition> {
    grid.applied_view().rows.filter.conditions.conditions().to_vec()
}

/// The checklist's row for value `label` (0 is its head).
fn row(table: &mut Table, grid: &Grid, label: &str) -> usize {
    let view = table.menu_view(grid).expect("open");
    1 + view.values.iter().position(|v| v.label == label).expect("listed")
}

#[test]
fn the_ids_take_no_conditions_so_open_no_menu() {
    let (mut table, grid) = records();
    assert!(!table.open_menu(&grid, ID));
    assert_eq!(table.menu_view(&grid), None);
    assert!(table.open_menu(&grid, PORT));
    assert_eq!(table.menu_key(), Some(PORT));
}

#[test]
fn the_checklist_lists_blanks_first_with_counts_and_unticking_writes_not_one_of() {
    let (mut table, mut grid) = records();
    let all = grid.snapshot().count(Axis::Rows);
    table.open_menu(&grid, PORT);
    let view = table.menu_view(&grid).expect("open");
    assert!(view.one_of && view.text.is_some() && view.compared.is_none());
    assert!(view.listed && !view.partial && view.head_ticked);
    assert_eq!(view.values[0].label, "(Blanks)");
    assert!(view.values[0].blank);
    let counted: u64 = view.values.iter().map(|v| v.count.expect("counted")).sum();
    assert_eq!(counted, all);

    let bergen = row(&mut table, &grid, "Bergen");
    let bergens = table.menu_view(&grid).unwrap().values[bergen - 1].count.unwrap();
    table.menu_tick(&mut grid, bergen);
    assert_eq!(answer(&mut table, &mut grid), all - bergens);
    assert_eq!(conditions(&grid), vec![Condition::one_of(PORT, vec![Value::of(Some("Bergen"))]).not(true)]);
    let view = table.menu_view(&grid).unwrap();
    assert!(!view.head_ticked && !view.values[bergen - 1].ticked && view.values[0].ticked);
    assert_eq!(table.chips(&grid)[0].text, "port: not Bergen");

    // Ticking it back filters nothing.
    table.menu_tick(&mut grid, bergen);
    assert_eq!(answer(&mut table, &mut grid), all);
    assert_eq!(conditions(&grid), vec![]);
}

#[test]
fn the_text_row_writes_contains_or_not_and_leaves_the_checklist_its_one_of() {
    let (mut table, mut grid) = records();
    table.open_menu(&grid, PORT);
    // Untick the blanks, listed first.
    table.menu_tick(&mut grid, 1);
    answer(&mut table, &mut grid);
    table.menu_set_text(&mut grid, TextQuery::new("en"), true);
    answer(&mut table, &mut grid);
    let view = table.menu_view(&grid).unwrap();
    assert_eq!((view.query.text.as_str(), view.not), ("en", true));
    let bergen = view.values.iter().find(|v| v.label == "Bergen").unwrap();
    assert!(bergen.excluded && !bergen.ticked);
    assert_eq!(table.chips(&grid).iter().map(|c| c.text.as_str()).collect::<Vec<_>>(), ["port: not (Blanks)", "port: not en"]);
    // No text asks for no Text condition.
    table.menu_set_text(&mut grid, TextQuery::new(""), false);
    answer(&mut table, &mut grid);
    assert_eq!(conditions(&grid), vec![Condition::one_of(PORT, vec![Value::Blank]).not(true)]);
}

#[test]
fn comparisons_write_once_their_fields_hold_values_of_the_column_kind() {
    let (mut table, mut grid) = records();
    let all = grid.snapshot().count(Axis::Rows);
    table.open_menu(&grid, WEIGHT);
    // Greater than, then a value that isn't a number: nothing is asked.
    table.menu_set_compare(&mut grid, 5, "x", "");
    assert_eq!(answer(&mut table, &mut grid), all);
    table.menu_set_compare(&mut grid, 5, "500", "");
    let over = answer(&mut table, &mut grid);
    assert!(over > 0 && over < all);
    let view = table.menu_view(&grid).unwrap();
    assert_eq!((view.step, view.low.as_str()), (5, "500"));
    assert_eq!(table.chips(&grid)[0].text, "weight: > 500");

    table.open_menu(&grid, SHIPPED);
    table.menu_set_compare(&mut grid, 7, "2024-04-01", "2024-06-30");
    let between = answer(&mut table, &mut grid);
    assert!(between > 0 && between < over);
    assert_eq!(table.chips(&grid)[1].text, "shipped: between 2024-04-01 and 2024-06-30");
}

#[test]
fn the_checklist_counts_only_the_rows_the_other_columns_keep() {
    let (mut table, mut grid) = records();
    table.open_menu(&grid, WEIGHT);
    table.menu_set_compare(&mut grid, 5, "500", "");
    let kept = answer(&mut table, &mut grid);
    table.open_menu(&grid, PORT);
    let view = table.menu_view(&grid).unwrap();
    assert!(view.narrowed);
    assert_eq!(view.values.iter().map(|v| v.count.unwrap()).sum::<u64>(), kept);
}

#[test]
fn a_chips_cross_and_clear_remove_conditions_and_the_menu_follows() {
    let (mut table, mut grid) = records();
    let all = grid.snapshot().count(Axis::Rows);
    table.open_menu(&grid, PORT);
    table.menu_set_text(&mut grid, TextQuery::new("a"), false);
    answer(&mut table, &mut grid);
    let aberdeen = row(&mut table, &grid, "Aberdeen");
    table.menu_tick(&mut grid, aberdeen);
    answer(&mut table, &mut grid);
    assert_eq!(table.chips(&grid).len(), 2);
    table.remove_chip(&mut grid, 0);
    // Removed, until the answer.
    assert_eq!(table.chips(&grid).iter().map(|c| c.state).collect::<Vec<_>>(), [ConditionState::Applied, ConditionState::Removing]);
    // The menu shows it gone at once.
    assert_eq!(table.menu_view(&grid).unwrap().query.text, "");
    answer(&mut table, &mut grid);
    let view = table.menu_view(&grid).unwrap();
    assert_eq!(view.query.text, "");
    assert!(view.filtered);
    table.menu_clear(&mut grid);
    assert_eq!(answer(&mut table, &mut grid), all);
    assert!(table.chips(&grid).is_empty() && !table.menu_view(&grid).unwrap().filtered);
}
