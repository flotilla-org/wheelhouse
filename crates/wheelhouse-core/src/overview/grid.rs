//! The overview grid's packings (docs/design/overview-scene.md, "The grid's
//! layout rule"): where each group's block goes in a region the viewport's
//! shape and 1 high, and how high its tiles are. Groups keep sidebar order.
use super::logical::Group;
use super::scene::{BLOCK_GAP_H, BLOCK_GAP_W, BLOCK_PAD_W, GAP_W, HEADING_H, LABEL_H};

/// A treemap rectangle's margin around its block, as a share of the
/// region's shorter side.
const TREEMAP_MARGIN: f64 = 0.012;

/// A group's block: its top left, its tiles' height, its columns and rows,
/// and each tile's cell (column, row), in sidebar order.
#[derive(Debug, Clone, PartialEq)]
pub(super) struct Block {
    pub x: f64,
    pub y: f64,
    pub t: f64,
    pub columns: usize,
    pub rows: usize,
    pub cells: Vec<(usize, usize)>,
}

/// A block's size with tiles 1 high and `aspect` wide.
#[derive(Debug, Clone, Copy)]
pub(super) struct Geometry {
    pub aspect: f64,
}

impl Geometry {
    pub fn width(&self, columns: usize) -> f64 {
        columns as f64 * self.aspect * (1.0 + GAP_W) - GAP_W * self.aspect + 2.0 * BLOCK_PAD_W * self.aspect
    }

    pub fn height(&self, rows: usize) -> f64 {
        HEADING_H + 2.0 * BLOCK_PAD_W * self.aspect + rows as f64 * (1.0 + LABEL_H + GAP_W * self.aspect) - GAP_W * self.aspect
    }
}

/// One way to lay a group's tiles out: so many columns, and where that puts
/// each tile.
#[derive(Debug, Clone)]
struct Shape {
    columns: usize,
    rows: usize,
    cells: Vec<(usize, usize)>,
}

fn tile_count(group: &Group) -> usize {
    group.clusters.iter().map(|c| c.tiles.len()).sum()
}

/// Where each of `group`'s tiles goes with `columns` columns, in sidebar
/// order. Kept whole, a cluster that doesn't fit in what's left of a row
/// starts the next one; one wider than a row starts a row and wraps.
fn shape(group: &Group, columns: usize, keep_whole: bool) -> Shape {
    let columns = columns.max(1);
    let (mut column, mut row) = (0, 0);
    let mut cells = vec![];
    for cluster in &group.clusters {
        let n = cluster.tiles.len();
        if keep_whole && column > 0 && column + n > columns {
            column = 0;
            row += 1;
        }
        for _ in 0..n {
            if column == columns {
                column = 0;
                row += 1;
            }
            cells.push((column, row));
            column += 1;
        }
    }
    Shape { columns, rows: row + 1, cells }
}

/// Every column count a group could take, one to all its tiles in a row.
fn shapes(group: &Group, keep_whole: bool) -> Vec<Shape> {
    (1..=tile_count(group).max(1)).map(|columns| shape(group, columns, keep_whole)).collect()
}

/// One tile size everywhere. Each block takes the shape closest to the
/// region's, blocks go onto shelves in order, and the tiles are as large
/// as lets every shelf fit. The whole is centred in the region.
pub(super) fn shelves(groups: &[Group], aspect: f64, keep_whole: bool) -> Vec<Block> {
    let size = Geometry { aspect };
    let mismatch = |s: &Shape| (size.width(s.columns) / size.height(s.rows) / aspect).ln().abs();
    let chosen: Vec<Shape> = groups
        .iter()
        .map(|g| shapes(g, keep_whole).into_iter().min_by(|a, b| mismatch(a).total_cmp(&mismatch(b))).expect("a group has a shape"))
        .collect();
    // Each block's top left, how wide the widest shelf is and how high the
    // shelves are, at tiles `t` high.
    let pack = |t: f64| {
        let (mut x, mut y, mut shelf, mut widest) = (0.0, 0.0, 0.0f64, 0.0f64);
        let mut at = vec![];
        for s in &chosen {
            let (w, h) = (size.width(s.columns) * t, size.height(s.rows) * t);
            if x > 0.0 && x + w > aspect {
                y += shelf + BLOCK_GAP_H * t;
                x = 0.0;
                shelf = 0.0;
            }
            at.push((x, y));
            widest = widest.max(x + w);
            shelf = shelf.max(h);
            x += w + BLOCK_GAP_W * aspect * t;
        }
        (at, widest, y + shelf)
    };
    let fits = |t: f64| {
        let (_, w, h) = pack(t);
        w <= aspect * (1.0 + 1e-12) && h <= 1.0 + 1e-12
    };
    // No larger than lets the biggest block fit the region alone.
    let mut hi = chosen.iter().map(|s| (aspect / size.width(s.columns)).min(1.0 / size.height(s.rows))).fold(f64::INFINITY, f64::min);
    if !hi.is_finite() {
        return vec![];
    }
    // Tiles 0 high always fit (every block is then a point), so 0 is a
    // safe lower bound.
    let mut lo = 0.0;
    if fits(hi) {
        lo = hi;
    } else {
        for _ in 0..60 {
            let mid = (lo + hi) / 2.0;
            if fits(mid) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
    }
    let (at, widest, high) = pack(lo);
    let (dx, dy) = ((aspect - widest) / 2.0, (1.0 - high) / 2.0);
    chosen
        .into_iter()
        .zip(at)
        .map(|(s, (x, y))| Block { x: x + dx, y: y + dy, t: lo, columns: s.columns, rows: s.rows, cells: s.cells })
        .collect()
}

/// A strip of the treemap: its height, and each group's tile height and
/// shape (an index into its shapes) in its rectangle; its smallest and
/// largest tile heights.
#[derive(Debug, Clone)]
struct Strip {
    h: f64,
    fits: Vec<(f64, usize)>,
    area: f64,
    least: f64,
    most: f64,
}

/// An ordered treemap. Groups go into strips across the region in order;
/// each strip is as high as its share of the tiles, and each group's
/// rectangle in it as wide as its share of the strip's. A group's tiles are
/// the largest its rectangle fits, so their sizes differ between groups,
/// by at most `limit` when any breaks allow it. The breaks are those that
/// give the most tile area.
///
/// For g groups this is O(g²) candidate smallest heights times an O(g²)
/// search each: about 0.4 ms at 40 groups and 1 ms at 60 in a release
/// build. The scene is rebuilt only when its inputs change, but a live
/// resize changes the aspect each frame.
pub(super) fn treemap(groups: &[Group], aspect: f64, keep_whole: bool, limit: f64) -> Vec<Block> {
    let map = Treemap::measure(groups, aspect, keep_whole);
    let g = groups.len();
    if g == 0 {
        return vec![];
    }
    let strip = |i: usize, j: usize| map.strip(i, j);
    // The breaks giving the most tile area with every tile from `lo` to
    // `hi` high, by dynamic programming over where the last strip starts;
    // with its area.
    let best = |lo: f64, hi: f64| {
        let mut score = vec![f64::NEG_INFINITY; g + 1];
        let mut from = vec![0; g + 1];
        score[0] = 0.0;
        for j in 1..=g {
            for i in 0..j {
                let s = strip(i, j);
                if score[i] == f64::NEG_INFINITY || s.least < lo || s.most > hi {
                    continue;
                }
                if score[i] + s.area > score[j] {
                    score[j] = score[i] + s.area;
                    from[j] = i;
                }
            }
        }
        (score[g] > f64::NEG_INFINITY).then(|| {
            let mut starts = vec![];
            let mut j = g;
            while j > 0 {
                starts.push(from[j]);
                j = from[j];
            }
            starts.reverse();
            (score[g], starts)
        })
    };
    // Try each strip's smallest tile height as the smallest allowed: the
    // best breaks within the limit have their smallest tiles in some strip.
    let mut lows: Vec<f64> = map.strips.iter().flatten().flatten().map(|s| s.least).collect();
    lows.sort_by(f64::total_cmp);
    lows.dedup();
    let within = lows.iter().filter_map(|&lo| best(lo, lo * limit * (1.0 + 1e-9))).max_by(|a, b| a.0.total_cmp(&b.0));
    let (_, starts) = within.or_else(|| best(0.0, f64::INFINITY)).expect("unlimited, some breaks fit");
    map.place(&starts)
}

/// Every strip a treemap of some groups could have, measured.
struct Treemap {
    aspect: f64,
    count: Vec<f64>,
    shapes: Vec<Vec<Shape>>,
    /// strips[i][j]: groups i to j - 1 as one strip.
    strips: Vec<Vec<Option<Strip>>>,
}

impl Treemap {
    fn measure(groups: &[Group], aspect: f64, keep_whole: bool) -> Treemap {
        let size = Geometry { aspect };
        let g = groups.len();
        let count: Vec<f64> = groups.iter().map(|group| tile_count(group).max(1) as f64).collect();
        let total: f64 = count.iter().sum();
        let shapes: Vec<Vec<Shape>> = groups.iter().map(|group| shapes(group, keep_whole)).collect();
        // Group `k`'s largest tiles in a rectangle `w` by `h`, and its shape.
        let fit = |k: usize, w: f64, h: f64| {
            let m = Self::margin(aspect, w, h);
            shapes[k]
                .iter()
                .enumerate()
                .map(|(i, s)| (((w - m) / size.width(s.columns)).min((h - m) / size.height(s.rows)), i))
                .fold((0.0, 0), |best, this| if this.0 > best.0 { this } else { best })
        };
        let mut strips: Vec<Vec<Option<Strip>>> = vec![vec![None; g + 1]; g + 1];
        for i in 0..g {
            for j in i + 1..=g {
                let share: f64 = count[i..j].iter().sum();
                let h = share / total;
                let fits: Vec<(f64, usize)> = (i..j).map(|k| fit(k, aspect * count[k] / share, h)).collect();
                let area = (i..j).zip(&fits).map(|(k, f)| count[k] * aspect * f.0 * f.0).sum();
                let least = fits.iter().map(|f| f.0).fold(f64::INFINITY, f64::min);
                let most = fits.iter().map(|f| f.0).fold(f64::NEG_INFINITY, f64::max);
                strips[i][j] = Some(Strip { h, fits, area, least, most });
            }
        }
        Treemap { aspect, count, shapes, strips }
    }

    /// The margin in a rectangle `w` by `h`: never so much it leaves no room.
    fn margin(aspect: f64, w: f64, h: f64) -> f64 {
        (TREEMAP_MARGIN * aspect.min(1.0)).min(w.min(h) / 4.0)
    }

    fn strip(&self, i: usize, j: usize) -> &Strip {
        self.strips[i][j].as_ref().expect("every strip is measured")
    }

    /// The blocks with strips starting at each of `starts` (the first 0).
    fn place(&self, starts: &[usize]) -> Vec<Block> {
        let size = Geometry { aspect: self.aspect };
        let mut blocks = vec![];
        let mut y = 0.0;
        for (n, &i) in starts.iter().enumerate() {
            let j = starts.get(n + 1).copied().unwrap_or(self.count.len());
            let s = self.strip(i, j);
            let share: f64 = self.count[i..j].iter().sum();
            let mut x = 0.0;
            for (k, &(t, which)) in (i..j).zip(&s.fits) {
                let w = self.aspect * self.count[k] / share;
                let shape = &self.shapes[k][which];
                blocks.push(Block {
                    x: x + (w - size.width(shape.columns) * t) / 2.0,
                    y: y + Self::margin(self.aspect, w, s.h) / 2.0,
                    t,
                    columns: shape.columns,
                    rows: shape.rows,
                    cells: shape.cells.clone(),
                });
                x += w;
            }
            y += s.h;
        }
        blocks
    }
}

/// The treemap with strips starting at each of `starts`, for checking the
/// chosen breaks against every other.
#[cfg(test)]
pub(super) fn treemap_with_breaks(groups: &[Group], aspect: f64, keep_whole: bool, starts: &[usize]) -> Vec<Block> {
    Treemap::measure(groups, aspect, keep_whole).place(starts)
}
