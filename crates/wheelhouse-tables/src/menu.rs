//! A column's menu (#577): its Text condition ("Contains" or "Does not
//! contain"), its comparison where it compares numbers or dates, and the
//! checklist of its values, each written by binnacle's column filter rules
//! as the web and ratatui menus write them. The menu keeps only its column,
//! its search and the values last listed; the conditions are read from the
//! Grid each time, so an edit elsewhere (a chip's ×) shows at once.

use binnacle_core::{
    Axis, Blanks, Compared, Condition, ConditionKind, ConditionState, FilterSpec, Grid, Key, TextQuery, TextSupport, Value,
};
use binnacle_memory::column_filter::{chip_text, compare, Column, Shown};
use binnacle_memory::PumpSource;

use crate::{Kind, Table};

/// At most this many values are listed, as ratatui's popup lists them.
const LIMIT: usize = 1000;

/// The open menu: its column, the checklist's search, and the values the
/// source last listed for the question asked.
pub(crate) struct Menu {
    key: Key,
    search: String,
    asked: Option<(String, FilterSpec)>,
    listed: Option<Listed>,
}

/// Listed values, the blank first if any, with their counts.
struct Listed {
    values: Vec<Value>,
    counts: Vec<Option<u64>>,
    partial: bool,
    narrowed: bool,
}

/// What a column's menu shows.
#[derive(Clone, Debug, PartialEq)]
pub struct MenuView {
    pub key: Key,
    /// What the column takes: Text conditions (and whether regexes), OneOf,
    /// and comparisons on numbers or dates.
    pub text: Option<TextSupport>,
    pub one_of: bool,
    pub compared: Option<Compared>,
    /// Its Text condition: the text (empty for none), and whether it is
    /// "Does not contain", matches case and is a regex.
    pub query: TextQuery,
    pub not: bool,
    /// Its comparison as the comparison row shows it: the step in
    /// [`compare::STEPS`] (0, "Any value", for none) and the fields.
    pub step: usize,
    pub low: String,
    pub high: String,
    /// The checklist: its search, whether its head ("(Select all)", or
    /// "(Select all matching …)" while searching) is ticked, and the values.
    pub search: String,
    pub head_ticked: bool,
    pub values: Vec<MenuValue>,
    /// Whether the list was asked for and answered, cut at the limit, and
    /// narrowed by the other columns' filters.
    pub listed: bool,
    pub partial: bool,
    pub narrowed: bool,
    /// Whether the column has any condition, which Clear removes.
    pub filtered: bool,
}

/// One value in the checklist.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MenuValue {
    /// Its text, or "(Blanks)" for the blank.
    pub label: String,
    pub blank: bool,
    pub count: Option<u64>,
    pub ticked: bool,
    /// A Text condition rejects it, so it can't be ticked.
    pub excluded: bool,
}

/// One condition on the rows, as the filter bar's chip shows it.
#[derive(Clone, Debug, PartialEq)]
pub struct Chip {
    /// `label: text`, as binnacle names it.
    pub text: String,
    pub state: ConditionState,
    /// The column it is read in.
    pub key: Key,
}

impl Table {
    /// Opens the menu on column `key`, if the column takes a condition of
    /// a kind the menu writes; closes any other.
    pub fn open_menu(&mut self, grid: &Grid, key: Key) -> bool {
        let support = grid.support(Axis::Rows, key);
        let takes = support.text.is_some() || support.one_of || support.compare.is_some();
        self.menu = takes.then(|| Menu { key, search: String::new(), asked: None, listed: None });
        takes
    }

    pub fn close_menu(&mut self) {
        self.menu = None;
    }

    /// The column the menu is open on.
    pub fn menu_key(&self) -> Option<Key> {
        self.menu.as_ref().map(|m| m.key)
    }

    /// The conditions on column `key` the rows' latest request holds.
    fn column(grid: &Grid, key: Key) -> Column {
        let latest = grid.conditions(Axis::Rows);
        let requested = latest.iter().filter(|(_, state)| *state != ConditionState::Removing).map(|(c, _)| c);
        Column::of(requested, Axis::Rows, key)
    }

    /// What the menu shows, the column's values listed again first if the
    /// search or the other columns' filters changed.
    pub fn menu_view(&mut self, grid: &Grid) -> Option<MenuView> {
        let key = self.menu.as_ref()?.key;
        let support = grid.support(Axis::Rows, key);
        if support.one_of {
            self.list(grid);
        }
        let menu = self.menu.as_ref()?;
        let column = Table::column(grid, key);
        let (query, not) = match &column.text {
            Some(Condition { kind: ConditionKind::Text(q), not, .. }) => (q.clone(), *not),
            _ => (TextQuery::new(""), false),
        };
        let (step, low, high) = match column.compare.as_ref().map(|c| &c.kind) {
            Some(ConditionKind::Compare(c)) => compare::fields(c),
            _ => (0, String::new(), String::new()),
        };
        let mut view = MenuView {
            key,
            text: support.text,
            one_of: support.one_of,
            compared: support.compare,
            query,
            not,
            step,
            low,
            high,
            search: menu.search.clone(),
            head_ticked: false,
            values: Vec::new(),
            listed: false,
            partial: false,
            narrowed: false,
            filtered: !column.all.is_empty(),
        };
        if let Some(listed) = &menu.listed {
            let shown = Shown { values: &listed.values, partial: listed.partial, search: &menu.search };
            view.head_ticked = column.head_ticked(&shown);
            let ticks = column.ticks(&listed.values);
            view.values = (listed.values.iter().zip(&listed.counts).zip(ticks))
                .map(|((value, count), tick)| MenuValue {
                    label: match value {
                        Value::Blank => "(Blanks)".into(),
                        Value::Text(t) => t.clone(),
                    },
                    blank: *value == Value::Blank,
                    count: *count,
                    ticked: tick.ticked,
                    excluded: tick.excluded,
                })
                .collect();
            view.listed = true;
            view.partial = listed.partial;
            view.narrowed = listed.narrowed;
        }
        Some(view)
    }

    /// Asks the source for the column's values, searched and narrowed by
    /// the applied filter less the column's own conditions, unless that
    /// question was the last one asked.
    fn list(&mut self, grid: &Grid) {
        let Some(menu) = &self.menu else {
            return;
        };
        let applied = &grid.applied_view().rows.filter;
        let others: Vec<Condition> = (applied.conditions.conditions().iter()).filter(|c| c.key != menu.key).cloned().collect();
        let narrow = FilterSpec::default().with_quick(applied.quick.clone()).with_conditions(others.into());
        let question = (menu.search.clone(), narrow);
        if menu.asked.as_ref() == Some(&question) {
            return;
        }
        let query = binnacle_core::DistinctQuery::new(LIMIT).search(question.0.clone()).narrow(question.1.clone());
        let key = menu.key;
        let answer = match &mut self.kind {
            Kind::Records { rows, source } => source.over(rows).distinct(Axis::Rows, key, &query),
            Kind::Cells(cells) => cells.source.over(&cells.rows).distinct(Axis::Rows, key, &query),
            Kind::Described(d) => d.source.distinct(Axis::Rows, key, &query),
            Kind::Huge(_) | Kind::Live(_) => None,
        };
        let listed = answer.map(|d| {
            let blanks = d.blanks.map(|b| match b {
                Blanks::Counted(n) => Some(n),
                _ => None,
            });
            let (mut values, mut counts) = (Vec::new(), Vec::new());
            if let Some(count) = blanks {
                values.push(Value::Blank);
                counts.push(count);
            }
            for v in d.values {
                values.push(Value::Text(v.text));
                counts.push(v.count);
            }
            Listed { values, counts, partial: d.partial, narrowed: d.narrowed }
        });
        if let Some(menu) = &mut self.menu {
            menu.listed = listed;
            menu.asked = Some(question);
        }
    }

    /// Searches the checklist's values for `search`, ignoring case.
    pub fn menu_search(&mut self, search: &str) {
        if let Some(menu) = &mut self.menu {
            menu.search = search.to_owned();
        }
    }

    /// Ticks the checklist's row `row` (0 its head, then each value as
    /// [`MenuView::values`] lists them) if it is not, else unticks it.
    pub fn menu_tick(&mut self, grid: &mut Grid, row: usize) {
        self.list(grid);
        let Some(menu) = &self.menu else {
            return;
        };
        let Some(listed) = &menu.listed else {
            return;
        };
        let column = Table::column(grid, menu.key);
        let shown = Shown { values: &listed.values, partial: listed.partial, search: &menu.search };
        if row == 0 {
            column.tick_head(grid, !column.head_ticked(&shown), &shown);
        } else if let Some(value) = listed.values.get(row - 1) {
            column.tick(grid, value, !column.ticked(value), &shown);
        }
    }

    /// Asks for the Text condition the text row shows: `query` contained,
    /// or not, or none for no text. The checklist's OneOf stays.
    pub fn menu_set_text(&mut self, grid: &mut Grid, query: TextQuery, not: bool) {
        let Some(key) = self.menu_key() else {
            return;
        };
        let column = Table::column(grid, key);
        let to = (!query.text.is_empty()).then(|| Condition::text(key, query).not(not));
        if to != column.text {
            column.write(grid, to, column.one_of.clone());
        }
    }

    /// Asks for the comparison the comparison row shows, keeping its not:
    /// `step` in [`compare::STEPS`] with `low` (and `high`, for Between)
    /// typed in, or none while a field holds no value of the column's kind.
    pub fn menu_set_compare(&mut self, grid: &mut Grid, step: usize, low: &str, high: &str) {
        let Some(key) = self.menu_key() else {
            return;
        };
        let Some(compared) = grid.support(Axis::Rows, key).compare else {
            return;
        };
        let column = Table::column(grid, key);
        let not = column.compare.as_ref().is_some_and(|c| c.not);
        let to = compare::comparison(step, compared, low, high).map(|c| Condition::compare(key, c).not(not));
        if to != column.compare {
            column.write_compare(grid, to);
        }
    }

    /// Removes every condition on the menu's column.
    pub fn menu_clear(&mut self, grid: &mut Grid) {
        let Some(key) = self.menu_key() else {
            return;
        };
        for condition in Table::column(grid, key).all {
            grid.remove_condition(Axis::Rows, &condition);
        }
    }

    /// The rows' conditions as the filter bar's chips show them, each named
    /// by its column's header.
    pub fn chips(&self, grid: &Grid) -> Vec<Chip> {
        (grid.conditions(Axis::Rows).into_iter())
            .map(|(condition, state)| Chip {
                text: chip_text(&self.header(Axis::Cols, condition.key, 0), &condition),
                state,
                key: condition.key,
            })
            .collect()
    }

    /// Removes the condition chip `index` shows, unless it is already
    /// being removed.
    pub fn remove_chip(&self, grid: &mut Grid, index: usize) {
        let listed = grid.conditions(Axis::Rows);
        if let Some((condition, state)) = listed.get(index) {
            if *state != ConditionState::Removing {
                grid.remove_condition(Axis::Rows, condition);
            }
        }
    }
}
