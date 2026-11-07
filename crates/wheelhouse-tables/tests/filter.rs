//! The rows' quick filter in the canned records table as a host drives it:
//! the filter bar says when the source left out the filter it asked for.
use binnacle_core::{Axis, TextQuery, Viewport};
use wheelhouse_tables::{Metrics, Table};

const METRICS: Metrics = Metrics { line: 20.0, digit: 8.0 };

/// A filter the source takes is applied; an invalid regex is not, and the
/// one before stays in force until a valid one, or none, is asked for.
#[test]
fn an_invalid_regex_is_not_applied_and_keeps_the_filter_before() {
    let (mut table, mut grid) = Table::open("records", METRICS).expect("records");
    let viewport = Viewport::new(800.0, 600.0);
    table.fill(&mut grid, viewport);
    assert!(table.quick_filter_applied(&grid), "no filter asked for");
    let all = grid.snapshot().count(Axis::Rows);

    let mut filter = |table: &mut Table, query: Option<TextQuery>| {
        grid.set_quick_filter(Axis::Rows, query);
        table.pump(&mut grid, |_| {});
        table.fill(&mut grid, viewport);
        (table.quick_filter_applied(&grid), grid.snapshot().count(Axis::Rows))
    };
    let (applied, bergen) = filter(&mut table, Some(TextQuery::new("bergen")));
    assert!(applied && bergen > 0 && bergen < all, "{bergen} of {all}");
    assert_eq!(filter(&mut table, Some(TextQuery::new("berg(").regex(true))), (false, bergen));
    assert!(filter(&mut table, Some(TextQuery::new("^berg").regex(true))).0);
    assert_eq!(filter(&mut table, None), (true, all));
}
