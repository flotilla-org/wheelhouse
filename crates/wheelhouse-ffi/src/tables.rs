//! Tables for the native table control (#506): the core owns each table's
//! source and its binnacle Grid, and the host draws and drives that Grid
//! through binnacle.h. The tables are canned (wheelhouse-tables) until the
//! described source over Flotilla's tables arrives.
use super::*;
use binnacle_core::{Axis, Key, Viewport};
use binnacle_ffi::BinnacleGrid;
use std::time::Duration;
use wheelhouse_tables::{Align, Metrics, Table, NAMES};

/// A cell's text sits at its leading edge.
pub const WHEELHOUSE_TABLE_ALIGN_LEFT: u32 = 0;
/// A cell's text sits at its trailing edge.
pub const WHEELHOUSE_TABLE_ALIGN_RIGHT: u32 = 1;
/// wheelhouse_table_frame's answer when the table never changes by itself.
pub const WHEELHOUSE_TABLE_NEVER: u64 = u64::MAX;

/// A table and the Grid over it.
pub struct WheelhouseTable {
    table: Table,
    grid: *mut BinnacleGrid,
    /// The text the last wheelhouse_table_cell or wheelhouse_table_header
    /// returned.
    text: String,
}

/// How many canned tables there are.
#[no_mangle]
pub extern "C" fn wheelhouse_table_count() -> usize {
    NAMES.len()
}

/// The name of canned table index, a static string, or an empty one past
/// the count.
#[no_mangle]
pub extern "C" fn wheelhouse_table_name(index: usize) -> Text {
    Text::borrowed(NAMES.get(index).copied().unwrap_or(""))
}

/// Opens the canned table named name, sized for text line pixels high
/// whose digits are digit pixels wide, or returns NULL for an unknown name.
/// Free it with wheelhouse_table_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_open(name: Text, line: f32, digit: f32) -> *mut WheelhouseTable {
    // SAFETY: the caller's contract.
    let Ok(name) = (unsafe { name.read() }) else {
        return ptr::null_mut();
    };
    let Some((table, grid)) = Table::open(&name, Metrics { line, digit }) else {
        return ptr::null_mut();
    };
    let grid = Box::into_raw(BinnacleGrid::from_grid(grid));
    Box::into_raw(Box::new(WheelhouseTable { table, grid, text: String::new() }))
}

/// The table's Grid, for binnacle.h's calls, live until wheelhouse_table_free.
/// Don't free it with binnacle_grid_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_grid(table: *mut WheelhouseTable) -> *mut BinnacleGrid {
    // SAFETY: the caller's contract.
    unsafe { table.as_ref() }.map_or(ptr::null_mut(), |t| t.grid)
}

/// Starts a frame of width by height pixels at now_us on the host's
/// monotonic clock: answers the Effects the table's source answers, passing
/// the others on to binnacle_grid_poll_effect, advances a live table, then
/// begins the Grid's frame and fills its Windows. Take the Snapshot with
/// binnacle_grid_snapshot after it. Returns the microseconds until the
/// table changes by itself, or WHEELHOUSE_TABLE_NEVER.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_frame(table: *mut WheelhouseTable, width: f32, height: f32, now_us: u64) -> u64 {
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return WHEELHOUSE_TABLE_NEVER;
    };
    // SAFETY: the handle owns its Grid until wheelhouse_table_free.
    let handle = unsafe { &mut *t.grid };
    let source = &mut t.table;
    let mut wake = None;
    handle.pump_with(|grid, pass_on| {
        source.pump(grid, &mut *pass_on);
        wake = source.tick(grid, Duration::from_micros(now_us));
        // A tick's notices can ask for more answers.
        source.pump(grid, &mut *pass_on);
        source.fill(grid, Viewport::new(width, height));
    });
    wake.map_or(WHEELHOUSE_TABLE_NEVER, |d| d.as_micros().min(u64::MAX as u128 - 1) as u64)
}

/// The text of the cell at the row and column Keys, valid until the next
/// call on table, and its WHEELHOUSE_TABLE_ALIGN_* in align_out (if not NULL).
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_cell(table: *mut WheelhouseTable, row: u64, col: u64, align_out: *mut u32) -> Text {
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return Text::borrowed("");
    };
    let cell = t.table.cell(Key(row), Key(col));
    // SAFETY: the caller's contract.
    if let Some(align) = unsafe { align_out.as_mut() } {
        *align = match cell.align {
            Align::Left => WHEELHOUSE_TABLE_ALIGN_LEFT,
            Align::Right => WHEELHOUSE_TABLE_ALIGN_RIGHT,
        };
    }
    t.text = cell.text;
    Text::borrowed(&t.text)
}

/// The header text of the item with key at position on axis (binnacle's
/// BINNACLE_ROWS or BINNACLE_COLS), valid until the next call on table.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_header(table: *mut WheelhouseTable, axis: u32, key: u64, position: u64) -> Text {
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return Text::borrowed("");
    };
    let axis = if axis == 0 { Axis::Rows } else { Axis::Cols };
    t.text = t.table.header(axis, Key(key), position);
    Text::borrowed(&t.text)
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_free(table: *mut WheelhouseTable) {
    if table.is_null() {
        return;
    }
    // SAFETY: a live handle from wheelhouse_table_open, freed once.
    let t = unsafe { Box::from_raw(table) };
    // SAFETY: the handle owned its Grid.
    drop(unsafe { Box::from_raw(t.grid) });
}
