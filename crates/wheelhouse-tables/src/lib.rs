//! Canned tables for the native table control (#506): binnacle In-Memory
//! Table Sources over data generated here, as binnacle's ratatui demo
//! (`binnacle-demo`) has them. Each is a Table Source and the text its cells
//! draw; the Grid over it is the host's, which pumps it each frame.

use std::borrow::Cow;
use std::ops::Range;
use std::time::Duration;

use binnacle_core::{Axis, AxisConfig, Grid, Key, Located, Scalar, Stamped, Support, Viewport};
use binnacle_memory::{pump, IdentitySource, MemorySource, PumpSource};

/// The tables, by the name a host opens them with.
pub const NAMES: [&str; 3] = ["records", "huge", "live"];

/// The host's measures, in its unit (pixels): a line of text and the width
/// of a digit, which the tables' default sizes are counted in.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Metrics {
    pub line: f32,
    pub digit: f32,
}

const RECORDS: usize = 1000;
/// The most records the source hides, fewer than the Grid's own cap.
const RECORD_HIDE_CAP: usize = 100;
const HEADERS: [&str; 6] = ["id", "name", "weight", "crew", "port", "shipped"];

const HUGE_ROWS: u64 = 1_000_000_000_000_000;
const HUGE_COLS: u64 = 12;

const TICKERS: usize = 200;
/// The live table lists a new ticker every this many ticks, up to [`LISTED`].
const LISTING: u32 = 8;
const LISTED: usize = 240;
const TICKER_HEADERS: [&str; 3] = ["ticker", "price", "load"];
const TICK: Duration = Duration::from_millis(250);

const NAMES_: [&str; 8] = ["anchor", "bosun", "capstan", "davit", "ensign", "fathom", "galley", "hawser"];
const PORTS: [&str; 9] = ["Aberdeen", "Bergen", "Cádiz", "Dunedin", "Esbjerg", "Funchal", "Gdańsk", "Halifax", "Île d'Yeu"];

struct Record {
    id: i64,
    name: String,
    /// Missing for every seventh record, to show nulls.
    weight: Option<f64>,
    crew: i64,
    /// Missing for every eleventh record.
    port: Option<&'static str>,
    /// When it shipped, a day in 2024 in milliseconds since the Unix epoch;
    /// missing for every thirteenth record.
    shipped: Option<i64>,
}

impl Record {
    fn generate(i: usize) -> Record {
        // A cheap scramble so the columns are not already in order.
        let h = (i as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15) >> 40;
        Record {
            id: i as i64,
            name: format!("{} {}", NAMES_[(h % 8) as usize], h % 97),
            weight: (!i.is_multiple_of(7)).then(|| (h % 10_000) as f64 / 10.0),
            crew: (h % 13) as i64,
            port: (!i.is_multiple_of(11)).then(|| PORTS[((h >> 4) % 9) as usize]),
            shipped: (!i.is_multiple_of(13)).then(|| {
                let Scalar::Date(start) = Scalar::date(2024, 1, 1) else { unreachable!("a date") };
                start + ((h >> 8) % 366) as i64 * 86_400_000
            }),
        }
    }
}

/// A row of the live table.
struct Ticker {
    name: String,
    price: f64,
    /// Between 0 and 1.
    load: f64,
}

/// Tickers whose values change every [`TICK`].
struct Live {
    rows: Vec<Ticker>,
    source: Box<MemorySource<Ticker>>,
    /// A xorshift state, so runs repeat.
    seed: u64,
    /// When the next tick is due, on the host's clock.
    next: Duration,
    since_listing: u32,
}

impl Live {
    fn new() -> Live {
        let mut live = Live {
            rows: Vec::new(),
            source: Box::new(
                MemorySource::<Ticker>::new(TICKERS, TICKER_HEADERS.len())
                    .bytes_column(Key(0), |t| Some(Cow::Borrowed(t.name.as_bytes())))
                    .f64_column(Key(1), |t| Some(t.price))
                    .f64_column(Key(2), |t| Some(t.load)),
            ),
            seed: 0x2545_F491_4F6C_DD1D,
            next: Duration::ZERO,
            since_listing: 0,
        };
        live.rows = (0..TICKERS).map(|i| live.ticker(i)).collect();
        live
    }

    fn ticker(&mut self, i: usize) -> Ticker {
        Ticker { name: format!("{} {}", NAMES_[i % 8].to_uppercase(), i / 8), price: 10.0 + self.random() * 990.0, load: self.random() }
    }

    /// A number in `0..1`.
    fn random(&mut self) -> f64 {
        self.seed ^= self.seed << 13;
        self.seed ^= self.seed >> 7;
        self.seed ^= self.seed << 17;
        (self.seed >> 11) as f64 / (1u64 << 53) as f64
    }

    /// Moves about a quarter of the tickers, now and then lists a new one,
    /// and tells the Grid what changed.
    fn tick(&mut self, grid: &mut Grid) {
        for i in 0..self.rows.len() {
            if self.random() >= 0.25 {
                continue;
            }
            let (step, load) = (self.random() - 0.5, self.random() - 0.5);
            let t = &mut self.rows[i];
            t.price = (t.price * (1.0 + step * 0.04)).max(0.01);
            t.load = (t.load + load * 0.2).clamp(0.0, 1.0);
            for col in [Key(1), Key(2)] {
                for (axis, notice) in self.source.changed(&self.rows, i, col) {
                    grid.notify(axis, notice);
                }
            }
        }
        self.since_listing += 1;
        if self.since_listing >= LISTING && self.rows.len() < LISTED {
            self.since_listing = 0;
            let ticker = self.ticker(self.rows.len());
            self.rows.push(ticker);
            for (axis, notice) in self.source.appended(&self.rows, &[]) {
                grid.notify(axis, notice);
            }
        }
    }
}

/// The huge table: Keys are Positions on both axes, and nothing sorts.
struct Huge {
    rows: IdentitySource,
    cols: IdentitySource,
}

impl Huge {
    fn axis(&self, axis: Axis) -> &IdentitySource {
        match axis {
            Axis::Rows => &self.rows,
            Axis::Cols => &self.cols,
        }
    }
}

impl PumpSource for Huge {
    fn locate(&mut self, axis: Axis, key: Key) -> Located {
        self.axis(axis).locate(key)
    }
}

enum Kind {
    Records { rows: Vec<Record>, source: Box<MemorySource<Record>> },
    Huge(Huge),
    Live(Live),
}

/// A canned table: its Table Source and the text its cells draw.
pub struct Table {
    kind: Kind,
}

/// Where a cell's text sits across it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Align {
    Left,
    Right,
}

/// A cell's text and how it aligns.
pub struct Cell {
    pub text: String,
    pub align: Align,
}

fn records_source(rows: &[Record]) -> MemorySource<Record> {
    MemorySource::<Record>::new(rows.len(), HEADERS.len())
        .i64_column(Key(0), |r| Some(r.id))
        .bytes_column(Key(1), |r| Some(Cow::Borrowed(r.name.as_bytes())))
        .f64_column(Key(2), |r| r.weight)
        .i64_column(Key(3), |r| Some(r.crew))
        .bytes_column(Key(4), |r| r.port.map(|p| Cow::Borrowed(p.as_bytes())))
        .date_column(Key(5), |r| r.shipped)
        // The quick filter searches the weight as drawn.
        .text_column(Key(2), |r| Some(r.weight.map_or("-".into(), |w| format!("{w:.1}").into())))
}

impl Table {
    /// The table named `name` (one of [`NAMES`]) and a Grid set up for it,
    /// sized in `metrics`.
    pub fn open(name: &str, metrics: Metrics) -> Option<(Table, Grid)> {
        let line = metrics.line;
        let digit = metrics.digit;
        match name {
            "records" => {
                let rows: Vec<Record> = (0..RECORDS).map(Record::generate).collect();
                let source = records_source(&rows);
                let mut grid = Grid::new(
                    AxisConfig::new(rows.len() as u64, line).header(line),
                    AxisConfig::new(HEADERS.len() as u64, 14.0 * digit).header(5.0 * digit).gap(1.0),
                );
                for axis in Axis::ALL {
                    grid.set_quick_filter_support(axis, source.quick_filter(axis));
                    grid.set_hide_support(axis, source.hide_support(axis));
                }
                grid.set_hide_cap(Axis::Rows, RECORD_HIDE_CAP);
                grid.set_find_support(source.find_support());
                for c in 0..HEADERS.len() as u64 {
                    grid.set_support(Axis::Rows, Key(c), source.support(Axis::Rows, Key(c)));
                }
                // The ids take no conditions.
                let id = source.support(Axis::Rows, Key(0)).with_text(None).with_one_of(false).with_compare(None);
                grid.set_support(Axis::Rows, Key(0), id);
                let kind = Kind::Records { rows, source: Box::new(source) };
                Some((Table { kind }, grid))
            }
            "huge" => {
                let digits = HUGE_ROWS.to_string().len() as f32;
                let mut grid = Grid::new(
                    AxisConfig::new(HUGE_ROWS, line).header(line),
                    AxisConfig::new(HUGE_COLS, (digits + 4.0) * digit).header((digits + 1.0) * digit).gap(1.0),
                );
                for c in 0..HUGE_COLS {
                    let none = Support::default().with_sort(false).with_text(None);
                    grid.set_support(Axis::Rows, Key(c), none);
                }
                let kind = Kind::Huge(Huge { rows: IdentitySource::new(HUGE_ROWS), cols: IdentitySource::new(HUGE_COLS) });
                Some((Table { kind }, grid))
            }
            "live" => {
                let grid = Grid::new(
                    AxisConfig::new(TICKERS as u64, line).header(line),
                    AxisConfig::new(TICKER_HEADERS.len() as u64, 12.0 * digit).header(5.0 * digit).gap(1.0),
                );
                Some((Table { kind: Kind::Live(Live::new()) }, grid))
            }
            _ => None,
        }
    }

    /// Advances the table's own clock to `now` (the host's, from any fixed
    /// start), telling `grid` what changed. Returns how long until it
    /// changes again by itself, or `None` if it never does.
    pub fn tick(&mut self, grid: &mut Grid, now: Duration) -> Option<Duration> {
        let Kind::Live(live) = &mut self.kind else {
            return None;
        };
        if live.next.is_zero() {
            live.next = now + TICK;
        }
        while live.next <= now {
            live.tick(grid);
            live.next += TICK;
        }
        Some(live.next - now)
    }

    /// Answers the Effects the source answers (View Specs, Locates, moves,
    /// find steps) and hands every other one to `pass_on`, in order.
    pub fn pump(&mut self, grid: &mut Grid, mut pass_on: impl FnMut(Stamped)) {
        let mut seen = |_: &mut Grid, stamped: Stamped| {
            use binnacle_core::Effect::*;
            match stamped.effect {
                ViewRequested { .. } | Locate { .. } | MoveRequested { .. } | FindStepRequested { .. } => {}
                _ => pass_on(stamped),
            }
        };
        match &mut self.kind {
            Kind::Records { rows, source } => source.pump(rows, grid, &mut seen),
            Kind::Huge(huge) => pump(grid, huge, &mut seen),
            Kind::Live(Live { rows, source, .. }) => source.pump(rows, grid, &mut seen),
        }
    }

    /// Starts `grid`'s frame for `viewport` and fills the Windows it asks for.
    pub fn fill(&self, grid: &mut Grid, viewport: Viewport) {
        for (axis, range) in grid.begin_frame(viewport) {
            let keys = self.window(axis, range.clone());
            grid.set_window(axis, range.start, &keys);
        }
    }

    fn window(&self, axis: Axis, range: Range<u64>) -> Vec<Key> {
        match &self.kind {
            Kind::Records { source, .. } => source.window(axis, range),
            Kind::Huge(huge) => huge.axis(axis).window(range),
            Kind::Live(live) => live.source.window(axis, range),
        }
    }

    /// The cell at `row` and `col`, by their Keys.
    pub fn cell(&self, row: Key, col: Key) -> Cell {
        let right = |text: String| Cell { text, align: Align::Right };
        let left = |text: String| Cell { text, align: Align::Left };
        match &self.kind {
            Kind::Records { rows, .. } => {
                let Some(r) = rows.get(row.0 as usize) else {
                    return left(String::new());
                };
                match col.0 {
                    0 => right(r.id.to_string()),
                    1 => left(r.name.clone()),
                    2 => right(r.weight.map_or_else(|| "-".into(), |w| format!("{w:.1}"))),
                    3 => right(r.crew.to_string()),
                    4 => left(r.port.unwrap_or_default().to_string()),
                    _ => left(r.shipped.map(|d| Scalar::Date(d).to_string()).unwrap_or_default()),
                }
            }
            Kind::Huge(_) => right(format!("r{}c{}", row.0, col.0)),
            Kind::Live(live) => {
                let Some(t) = live.rows.get(row.0 as usize) else {
                    return left(String::new());
                };
                match col.0 {
                    0 => left(t.name.clone()),
                    1 => right(format!("{:.2}", t.price)),
                    _ => right(format!("{:.0}%", t.load * 100.0)),
                }
            }
        }
    }

    /// The header of the item with `key` at `position` on `axis`: a column's
    /// name, or a row's number.
    pub fn header(&self, axis: Axis, key: Key, position: u64) -> String {
        match (axis, &self.kind) {
            (Axis::Cols, Kind::Records { .. }) => HEADERS.get(key.0 as usize).copied().unwrap_or_default().into(),
            (Axis::Cols, Kind::Huge(_)) => format!("col {}", key.0),
            (Axis::Cols, Kind::Live(_)) => TICKER_HEADERS.get(key.0 as usize).copied().unwrap_or_default().into(),
            (Axis::Rows, Kind::Huge(_)) => position.saturating_add(1).to_string(),
            (Axis::Rows, _) => key.0.to_string(),
        }
    }
}
