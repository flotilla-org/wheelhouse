//! Tables for the native table control (#506): the core owns each table's
//! source and its binnacle Grid, and the host draws and drives that Grid
//! through binnacle.h. A table is a canned one (#506) or a described one
//! read live from a server by its link (#529), or one of given cells, as a
//! Rich Text table Embed shows (#561).
use super::*;
use binnacle_core::{Axis, Compared, ConditionState, Key, TextQuery, Viewport};
use binnacle_ffi::BinnacleGrid;
use binnacle_memory::column_filter::compare;
use std::time::Duration;
use wheelhouse_tables::{Align, Chip, Link, MenuView, Metrics, Table, NAMES};

/// A cell's text sits at its leading edge.
pub const WHEELHOUSE_TABLE_ALIGN_LEFT: u32 = 0;
/// A cell's text sits at its trailing edge.
pub const WHEELHOUSE_TABLE_ALIGN_RIGHT: u32 = 1;
/// wheelhouse_table_frame's answer when the table never changes by itself.
pub const WHEELHOUSE_TABLE_NEVER: u64 = u64::MAX;

/// What the host's find bar says, as of the last wheelhouse_table_frame.
#[repr(C)]
pub struct WheelhouseTableFind {
    /// 1 while a Find Query is in force; the rest is zero while not.
    pub open: u32,
    /// How many cells match: total, or at least total while partial, of
    /// which hidden lie in collapsed groups.
    pub total: u64,
    pub partial: u32,
    pub hidden: u64,
    /// 1 once a find step has landed since the Find Query changed: its
    /// 0-based index among the matches if has_index, and whether it wrapped.
    pub stepped: u32,
    pub has_index: u32,
    pub index: u64,
    pub wrapped: u32,
}

/// A table and the Grid over it.
pub struct WheelhouseTable {
    table: Table,
    grid: *mut BinnacleGrid,
    /// The text the last wheelhouse_table_cell or wheelhouse_table_header
    /// returned.
    text: String,
    /// What the last wheelhouse_table_menu and wheelhouse_table_chips read.
    menu: Option<MenuView>,
    chips: Vec<Chip>,
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

/// Writes what a tab over the table name names says (a described table's
/// table name, not its link; a canned table's own name) to out, at most cap
/// bytes and cut at a character boundary, and returns how many it wrote:
/// zero for text that isn't UTF-8, or a NULL out.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_title(name: Text, out: *mut u8, cap: usize) -> usize {
    // SAFETY: the caller's contract.
    let Ok(name) = (unsafe { name.read() }) else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    let title = wheelhouse_tables::title(&name);
    let len = (0..=title.len().min(cap)).rev().find(|&i| title.is_char_boundary(i)).unwrap_or(0);
    // SAFETY: the caller's contract: out holds cap bytes.
    unsafe { ptr::copy_nonoverlapping(title.as_ptr(), out, len) };
    len
}

/// Opens the table name names, sized for text line pixels high whose
/// digits are digit pixels wide: a canned table's name, or a described
/// table's link (http://host:port/tables/{table}?namespace=…, or
/// http+unix://{socket path, percent-encoded}/tables/{table}?…), its dates
/// drawn utc_offset seconds east of UTC. A described table calls wake (if
/// not NULL) from another thread whenever data comes, for the host to draw
/// a frame. Returns NULL for an unknown name or a link that does not parse.
/// Free it with wheelhouse_table_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_open(
    name: Text,
    line: f32,
    digit: f32,
    utc_offset: i32,
    wake: Option<extern "C" fn()>,
) -> *mut WheelhouseTable {
    // SAFETY: the caller's contract.
    let Ok(name) = (unsafe { name.read() }) else {
        return ptr::null_mut();
    };
    let metrics = Metrics { line, digit };
    let opened = match Link::parse(&name) {
        Some(link) => Table::open_described(&link, metrics, utc_offset, Box::new(move || wake.map_or((), |wake| wake()))),
        None => Table::open(&name, metrics),
    };
    let Some((table, grid)) = opened else {
        return ptr::null_mut();
    };
    let grid = Box::into_raw(BinnacleGrid::from_grid(grid));
    Box::into_raw(Box::new(WheelhouseTable { table, grid, text: String::new(), menu: None, chips: Vec::new() }))
}

/// Opens a table of row_count rows of column_count plain-text cells, row by
/// row, the first row its header; right (if not NULL) has column_count
/// entries, 1 for a column aligned right. Sized as wheelhouse_table_open's
/// are. Returns NULL for a NULL cells with cells to read, or text that
/// isn't UTF-8. Free it with wheelhouse_table_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_open_cells(
    cells: *const Text,
    column_count: usize,
    row_count: usize,
    right: *const u32,
    line: f32,
    digit: f32,
) -> *mut WheelhouseTable {
    // SAFETY: the caller's contract.
    let opened = unsafe {
        pure(ptr::null_mut(), || {
            // A table with no cells is an empty one, whatever its sizes say:
            // nothing is sized from them before cells are read.
            let count = column_count.checked_mul(row_count).ok_or("too many cells")?;
            if count == 0 {
                return Ok(Table::from_cells(vec![], vec![], vec![], Metrics { line, digit }));
            }
            let cells = slice(cells, count)?;
            let right = if right.is_null() { &[][..] } else { slice(right, column_count)? };
            let mut rows = vec![];
            for row in cells.chunks(column_count) {
                rows.push(row.iter().map(|t| t.read()).collect::<Result<Vec<_>, _>>()?);
            }
            let header = rows.remove(0);
            let right = right.iter().map(|&r| r != 0).collect();
            Ok(Table::from_cells(header, rows, right, Metrics { line, digit }))
        })
    };
    let Some((table, grid)) = opened else {
        return ptr::null_mut();
    };
    let grid = Box::into_raw(BinnacleGrid::from_grid(grid));
    Box::into_raw(Box::new(WheelhouseTable { table, grid, text: String::new(), menu: None, chips: Vec::new() }))
}

/// The table's Grid, for binnacle.h's calls, live until wheelhouse_table_free.
/// Don't free it with binnacle_grid_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_grid(table: *mut WheelhouseTable) -> *mut BinnacleGrid {
    // SAFETY: the caller's contract.
    unsafe { table.as_ref() }.map_or(ptr::null_mut(), |t| t.grid)
}

/// Resizes the table for text line pixels high whose digits are digit
/// pixels wide, as when the host's font size changes: its rows, columns and
/// header bands, a user's resizes too, scale with the text. The first row
/// and column shown stay first.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_set_metrics(table: *mut WheelhouseTable, line: f32, digit: f32) {
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return;
    };
    // SAFETY: the handle owns its Grid until wheelhouse_table_free.
    let handle = unsafe { &mut *t.grid };
    t.table.set_metrics(handle.grid_mut(), Metrics { line, digit });
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

/// What to show about the table above it (a described table connecting,
/// retrying, failed or partly shown), valid until the next call on table;
/// empty when there is nothing to say.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_status(table: *mut WheelhouseTable) -> Text {
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return Text::borrowed("");
    };
    t.text = t.table.status().unwrap_or_default();
    Text::borrowed(&t.text)
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

/// What the find bar says about the Find Query in force: the count, counted
/// once until the query or the data changes, and where the last step landed.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_find(table: *mut WheelhouseTable) -> WheelhouseTableFind {
    let mut find = WheelhouseTableFind { open: 0, total: 0, partial: 0, hidden: 0, stepped: 0, has_index: 0, index: 0, wrapped: 0 };
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return find;
    };
    let Some(count) = t.table.find_count() else {
        return find;
    };
    find.open = 1;
    find.total = count.total;
    find.partial = count.partial.into();
    find.hidden = count.hidden;
    if let Some(stepped) = t.table.find_stepped() {
        find.stepped = 1;
        find.has_index = stepped.index.is_some().into();
        find.index = stepped.index.unwrap_or(0);
        find.wrapped = stepped.wrapped.into();
    }
    find
}

/// 0 when the Table Source left out the rows' quick filter last asked for
/// (an invalid regex, say), keeping the one before; 1 otherwise.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_quick_filter_applied(table: *mut WheelhouseTable) -> u32 {
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return 1;
    };
    // SAFETY: the handle owns its Grid until wheelhouse_table_free.
    let handle = unsafe { &mut *t.grid };
    t.table.quick_filter_applied(handle.grid_mut()).into()
}

/// 1 when the host fits the columns, once, to the first values it shows:
/// the table brings no widths of its own.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_fits_columns(table: *mut WheelhouseTable) -> u32 {
    // SAFETY: the caller's contract.
    unsafe { table.as_ref() }.is_some_and(|t| t.table.fits_columns()).into()
}

/// Marks which cells of the block rows by cols (row and column Keys,
/// rows_len and cols_len of them) match the Find Query in force: out, of
/// rows_len*cols_len bytes, gets 1 for a match and 0 otherwise, row by row;
/// all 0 while find is closed.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_find_marks(
    table: *mut WheelhouseTable,
    rows: *const u64,
    rows_len: usize,
    cols: *const u64,
    cols_len: usize,
    out: *mut u8,
) {
    let Some(len) = rows_len.checked_mul(cols_len) else {
        return;
    };
    if out.is_null() || len == 0 {
        return;
    }
    // SAFETY: the caller's contract: out holds rows_len*cols_len bytes.
    let out = unsafe { std::slice::from_raw_parts_mut(out, len) };
    out.fill(0);
    // SAFETY: the caller's contract.
    let Some(t) = (unsafe { table.as_mut() }) else {
        return;
    };
    if rows.is_null() || cols.is_null() {
        return;
    }
    // SAFETY: the caller's contract: rows_len and cols_len Keys.
    let keys = |p: *const u64, n: usize| unsafe { std::slice::from_raw_parts(p, n) }.iter().map(|&k| Key(k)).collect::<Vec<_>>();
    let marks = t.table.find_marks(&keys(rows, rows_len), &keys(cols, cols_len));
    for (o, m) in out.iter_mut().zip(marks) {
        *o = m.into();
    }
}

/// WheelhouseTableMenu.compared: the column compares no values, numbers, or
/// dates.
pub const WHEELHOUSE_TABLE_COMPARE_NONE: u32 = 0;
pub const WHEELHOUSE_TABLE_COMPARE_NUMBERS: u32 = 1;
pub const WHEELHOUSE_TABLE_COMPARE_DATES: u32 = 2;
/// The comparison row's steps, "Any value" (0, none) through "Between"; see
/// wheelhouse_table_compare_step.
pub const WHEELHOUSE_TABLE_COMPARE_STEPS: u32 = 8;
pub const WHEELHOUSE_TABLE_COMPARE_BETWEEN: u32 = 7;
// Literals, for the header: they must stay binnacle's.
const _: () = assert!(
    compare::STEPS.len() == WHEELHOUSE_TABLE_COMPARE_STEPS as usize && compare::BETWEEN == WHEELHOUSE_TABLE_COMPARE_BETWEEN as usize
);

/// What a column's menu shows, its texts valid until the next
/// wheelhouse_table_menu on the table. open is 0, and the rest zero, while
/// no menu is open.
#[repr(C)]
pub struct WheelhouseTableMenu {
    pub open: u32,
    /// The column's Key.
    pub key: u64,
    /// What it takes: Text conditions (text_regex: with regexes), OneOf (the
    /// checklist) and comparisons (a WHEELHOUSE_TABLE_COMPARE_*).
    pub takes_text: u32,
    pub text_regex: u32,
    pub one_of: u32,
    pub compared: u32,
    /// Its Text condition: the text (empty for none), "Does not contain"
    /// (not), matching case, a regex.
    pub text: Text,
    pub not: u32,
    pub case_sensitive: u32,
    pub regex: u32,
    /// Its comparison: the step and the fields, low and (for Between) high.
    pub step: u32,
    pub low: Text,
    pub high: Text,
    /// The checklist: its search, whether its head is ticked, and how many
    /// values wheelhouse_table_menu_value reads; listed once the source
    /// answered, partial when cut at the limit, narrowed by the other
    /// columns' filters.
    pub search: Text,
    pub head_ticked: u32,
    pub value_count: usize,
    pub listed: u32,
    pub partial: u32,
    pub narrowed: u32,
    /// The column has a condition, which wheelhouse_table_menu_clear removes.
    pub filtered: u32,
}

/// One of the checklist's values, as of the last wheelhouse_table_menu.
#[repr(C)]
pub struct WheelhouseTableMenuValue {
    /// Its text, or "(Blanks)" for the blank (blank 1).
    pub label: Text,
    pub blank: u32,
    pub has_count: u32,
    pub count: u64,
    pub ticked: u32,
    /// A Text condition rejects it: it can't be ticked.
    pub excluded: u32,
}

/// One of the rows' conditions as the filter bar's chip shows it, as of the
/// last wheelhouse_table_chips: its text (`label: text`), its state (a
/// binnacle BINNACLE_CONDITION_APPLIED, ADDING, REMOVING or NOT_APPLIED) and
/// the column it is read in.
#[repr(C)]
pub struct WheelhouseTableChip {
    pub text: Text,
    pub state: u32,
    pub key: u64,
}

/// The table and its Grid, for the menu's calls.
unsafe fn table_and_grid<'a>(table: *mut WheelhouseTable) -> Option<(&'a mut WheelhouseTable, &'a mut binnacle_core::Grid)> {
    // SAFETY: the caller's contract.
    let t = unsafe { table.as_mut() }?;
    // SAFETY: the handle owns its Grid until wheelhouse_table_free.
    let grid = unsafe { &mut *t.grid }.grid_mut();
    Some((t, grid))
}

/// Opens the column menu on the column with key, if the column takes a
/// condition the menu writes (1), closing any other; 0 otherwise.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_open(table: *mut WheelhouseTable, key: u64) -> u32 {
    // SAFETY: the caller's contract.
    let Some((t, grid)) = (unsafe { table_and_grid(table) }) else {
        return 0;
    };
    t.table.open_menu(grid, Key(key)).into()
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_close(table: *mut WheelhouseTable) {
    // SAFETY: the caller's contract.
    if let Some(t) = unsafe { table.as_mut() } {
        t.table.close_menu();
    }
}

/// What the open column menu shows, its values listed again first if its
/// search or the other columns' filters changed.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu(table: *mut WheelhouseTable) -> WheelhouseTableMenu {
    let mut out = WheelhouseTableMenu {
        open: 0,
        key: 0,
        takes_text: 0,
        text_regex: 0,
        one_of: 0,
        compared: WHEELHOUSE_TABLE_COMPARE_NONE,
        text: Text::borrowed(""),
        not: 0,
        case_sensitive: 0,
        regex: 0,
        step: 0,
        low: Text::borrowed(""),
        high: Text::borrowed(""),
        search: Text::borrowed(""),
        head_ticked: 0,
        value_count: 0,
        listed: 0,
        partial: 0,
        narrowed: 0,
        filtered: 0,
    };
    // SAFETY: the caller's contract.
    let Some((t, grid)) = (unsafe { table_and_grid(table) }) else {
        return out;
    };
    t.menu = t.table.menu_view(grid);
    let Some(view) = &t.menu else {
        return out;
    };
    out.open = 1;
    out.key = view.key.0;
    out.takes_text = view.text.is_some().into();
    out.text_regex = view.text.is_some_and(|t| t.regex).into();
    out.one_of = view.one_of.into();
    out.compared = match view.compared {
        Some(Compared::Numbers) => WHEELHOUSE_TABLE_COMPARE_NUMBERS,
        Some(Compared::Dates) => WHEELHOUSE_TABLE_COMPARE_DATES,
        _ => WHEELHOUSE_TABLE_COMPARE_NONE,
    };
    out.text = Text::borrowed(&view.query.text);
    out.not = view.not.into();
    out.case_sensitive = view.query.case_sensitive.into();
    out.regex = view.query.regex.into();
    out.step = view.step as u32;
    out.low = Text::borrowed(&view.low);
    out.high = Text::borrowed(&view.high);
    out.search = Text::borrowed(&view.search);
    out.head_ticked = view.head_ticked.into();
    out.value_count = view.values.len();
    out.listed = view.listed.into();
    out.partial = view.partial.into();
    out.narrowed = view.narrowed.into();
    out.filtered = view.filtered.into();
    out
}

/// The checklist's value index, as of the last wheelhouse_table_menu; all
/// zero past the count.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_value(table: *mut WheelhouseTable, index: usize) -> WheelhouseTableMenuValue {
    let none = WheelhouseTableMenuValue { label: Text::borrowed(""), blank: 0, has_count: 0, count: 0, ticked: 0, excluded: 0 };
    // SAFETY: the caller's contract.
    let Some(v) = unsafe { table.as_ref() }.and_then(|t| t.menu.as_ref()?.values.get(index)) else {
        return none;
    };
    WheelhouseTableMenuValue {
        label: Text::borrowed(&v.label),
        blank: v.blank.into(),
        has_count: v.count.is_some().into(),
        count: v.count.unwrap_or(0),
        ticked: v.ticked.into(),
        excluded: v.excluded.into(),
    }
}

/// Searches the checklist's values for search, ignoring case.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_search(table: *mut WheelhouseTable, search: Text) {
    // SAFETY: the caller's contract.
    let (Some(t), Ok(search)) = (unsafe { table.as_mut() }, unsafe { search.read() }) else {
        return;
    };
    t.table.menu_search(&search);
}

/// Ticks the checklist's row (0 its head, then each value by its index plus
/// one) if it is not, else unticks it, writing the conditions binnacle's
/// list-of-values rules give.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_tick(table: *mut WheelhouseTable, row: usize) {
    // SAFETY: the caller's contract.
    if let Some((t, grid)) = unsafe { table_and_grid(table) } {
        t.table.menu_tick(grid, row);
    }
}

/// Asks for the column's Text condition: text contained (or, with not, not
/// contained), matching case and as a regex as asked; none for no text.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_set_text(
    table: *mut WheelhouseTable,
    text: Text,
    not: u32,
    case_sensitive: u32,
    regex: u32,
) {
    // SAFETY: the caller's contract.
    let (Some((t, grid)), Ok(text)) = (unsafe { table_and_grid(table) }, unsafe { text.read() }) else {
        return;
    };
    let query = TextQuery::new(text).case_sensitive(case_sensitive != 0).regex(regex != 0);
    t.table.menu_set_text(grid, query, not != 0);
}

/// Asks for the column's comparison: step (a WHEELHOUSE_TABLE_COMPARE_STEPS
/// index, 0 for none) with low and, for Between, high, each a number or a
/// YYYY-MM-DD date as the column compares; none while a field holds no
/// such value.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_set_compare(table: *mut WheelhouseTable, step: u32, low: Text, high: Text) {
    // SAFETY: the caller's contract.
    let (Some((t, grid)), Ok(low), Ok(high)) = (unsafe { table_and_grid(table) }, unsafe { low.read() }, unsafe { high.read() }) else {
        return;
    };
    t.table.menu_set_compare(grid, step as usize, &low, &high);
}

/// Removes every condition on the open menu's column.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_menu_clear(table: *mut WheelhouseTable) {
    // SAFETY: the caller's contract.
    if let Some((t, grid)) = unsafe { table_and_grid(table) } {
        t.table.menu_clear(grid);
    }
}

/// The comparison row's step index: "Any value", "Equal to" through "At
/// least", or "Between"; empty past WHEELHOUSE_TABLE_COMPARE_STEPS.
#[no_mangle]
pub extern "C" fn wheelhouse_table_compare_step(index: u32) -> Text {
    Text::borrowed(compare::STEPS.get(index as usize).copied().unwrap_or(""))
}

/// Reads the rows' conditions as the filter bar's chips show them, for
/// wheelhouse_table_chip, and returns how many there are.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_chips(table: *mut WheelhouseTable) -> usize {
    // SAFETY: the caller's contract.
    let Some((t, grid)) = (unsafe { table_and_grid(table) }) else {
        return 0;
    };
    t.chips = t.table.chips(grid);
    t.chips.len()
}

/// Chip index as of the last wheelhouse_table_chips; all zero past the
/// count.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_chip(table: *mut WheelhouseTable, index: usize) -> WheelhouseTableChip {
    // SAFETY: the caller's contract.
    let Some(c) = unsafe { table.as_ref() }.and_then(|t| t.chips.get(index)) else {
        return WheelhouseTableChip { text: Text::borrowed(""), state: 0, key: 0 };
    };
    let state = match c.state {
        ConditionState::Applied => 0,
        ConditionState::Adding => 1,
        ConditionState::Removing => 2,
        _ => 3,
    };
    WheelhouseTableChip { text: Text::borrowed(&c.text), state, key: c.key.0 }
}

/// Removes the condition chip index shows (as the Grid lists them now),
/// unless it is already being removed.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_table_remove_chip(table: *mut WheelhouseTable, index: usize) {
    // SAFETY: the caller's contract.
    if let Some((t, grid)) = unsafe { table_and_grid(table) } {
        t.table.remove_chip(grid, index);
    }
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
