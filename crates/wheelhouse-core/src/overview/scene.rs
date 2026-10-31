//! The overview's scene layout: where a view mode puts each group and
//! workspace, in scene units where a tile is 1 high and the viewport's
//! aspect wide, so a camera that fits a tile shows its workspace whole.
use std::collections::BTreeMap;

use super::camera::Camera;
use super::logical::{GroupKey, LogicalLayout};
use crate::WorkspaceId;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ViewMode {
    /// Groups stacked, each a row that slides sideways when it overflows.
    #[default]
    Rows,
    /// Groups as blocks, each a grid of tiles.
    Grid,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SceneOptions {
    pub mode: ViewMode,
    /// The viewport's width over its height.
    pub aspect: f64,
    /// Rows: how many tiles the overview shows across.
    pub row_window_tiles: f64,
    /// Grid: tiles across a group's block.
    pub grid_columns: usize,
    /// Grid: columns of blocks.
    pub grid_block_columns: usize,
    /// Grid: past this many tiles across the whole scene, the overview fits
    /// the scene's width and pans down it instead of fitting all of it.
    pub max_tiles_across: f64,
}

impl Default for SceneOptions {
    fn default() -> Self {
        Self { mode: ViewMode::Rows, aspect: 1.6, row_window_tiles: 5.4, grid_columns: 3, grid_block_columns: 2, max_tiles_across: 8.0 }
    }
}

impl SceneOptions {
    /// These options with every size usable: a measure that isn't finite
    /// and positive takes its default, and a count is at least 1. A zero
    /// aspect would make every tile 0 wide and every camera divide by it.
    fn usable(&self) -> SceneOptions {
        let defaults = SceneOptions::default();
        let usable = |value: f64, default: f64| if value.is_finite() && value > 0.0 { value } else { default };
        SceneOptions {
            mode: self.mode,
            aspect: usable(self.aspect, defaults.aspect),
            row_window_tiles: usable(self.row_window_tiles, defaults.row_window_tiles),
            grid_columns: self.grid_columns.max(1),
            grid_block_columns: self.grid_block_columns.max(1),
            max_tiles_across: usable(self.max_tiles_across, defaults.max_tiles_across),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct SceneRect {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
}

impl SceneRect {
    pub fn width(&self) -> f64 {
        self.x1 - self.x0
    }

    pub fn height(&self) -> f64 {
        self.y1 - self.y0
    }

    pub fn contains(&self, other: &SceneRect) -> bool {
        const EPS: f64 = 1e-9;
        other.x0 >= self.x0 - EPS && other.y0 >= self.y0 - EPS && other.x1 <= self.x1 + EPS && other.y1 <= self.y1 + EPS
    }

    pub fn overlaps(&self, other: &SceneRect) -> bool {
        const EPS: f64 = 1e-9;
        self.x0 < other.x1 - EPS && other.x0 < self.x1 - EPS && self.y0 < other.y1 - EPS && other.y0 < self.y1 - EPS
    }

    fn union(&self, other: &SceneRect) -> SceneRect {
        SceneRect { x0: self.x0.min(other.x0), y0: self.y0.min(other.y0), x1: self.x1.max(other.x1), y1: self.y1.max(other.y1) }
    }

    fn shifted(&self, dx: f64) -> SceneRect {
        SceneRect { x0: self.x0 + dx, x1: self.x1 + dx, ..*self }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct SceneTile {
    pub workspace_id: WorkspaceId,
    /// Indexes into the scene's groups and that group's clusters.
    pub group: usize,
    pub cluster: usize,
    pub rect: SceneRect,
    /// Where its title and subtitle go, under the tile.
    pub label: SceneRect,
}

#[derive(Debug, Clone, PartialEq)]
pub struct SceneGroup {
    pub key: GroupKey,
    pub heading: SceneRect,
    /// The group's heading, tiles and labels.
    pub bounds: SceneRect,
    /// Each cluster's region, drawn behind its tiles to show they belong
    /// together: one outline in rows, a shaded rectangle per cell in a grid,
    /// whose union is one rectilinear region. A group head's own cluster has
    /// none.
    pub regions: Vec<Vec<SceneRect>>,
    /// Rows: how far the row is slid left (never right), and how far it can
    /// be: 0 when it fits.
    pub slide: f64,
    pub slide_min: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Scene {
    pub options: SceneOptions,
    pub tiles: Vec<SceneTile>,
    pub groups: Vec<SceneGroup>,
    pub bounds: SceneRect,
}

// Spacing, as shares of a tile's width (W) or height (1), from the prototype.
const GAP_W: f64 = 0.08;
const CLUSTER_GAP_W: f64 = 0.22;
const HEADING_H: f64 = 0.22;
const LABEL_H: f64 = 0.26;
const LABEL_TOP_H: f64 = 0.03;
const ROW_GAP_H: f64 = 0.16;
const PAD_W: f64 = 0.12;
const OUTLINE_W: f64 = 0.03;
const BLOCK_PAD_W: f64 = 0.1;
const BLOCK_GAP_W: f64 = 0.25;
const BLOCK_GAP_H: f64 = 0.25;
const GROUP_MARGIN: f64 = 1.08;

/// Place `layout` for `options`. `slides` holds how far each row has been
/// slid; each is kept within its row's range. Unusable sizes in `options`
/// take their defaults, and the scene keeps the options it used.
pub fn scene(layout: &LogicalLayout, options: &SceneOptions, slides: &BTreeMap<GroupKey, f64>) -> Scene {
    let options = &options.usable();
    let tw = options.aspect;
    let mut out = Scene { options: *options, tiles: vec![], groups: vec![], bounds: SceneRect::default() };
    match options.mode {
        ViewMode::Rows => {
            let (gap, cluster_gap) = (GAP_W * tw, CLUSTER_GAP_W * tw);
            let window = row_window(options);
            let mut y = 0.0;
            for (gi, group) in layout.groups.iter().enumerate() {
                let mut x = 0.0;
                let mut regions = vec![];
                let first_tile = out.tiles.len();
                for (ci, cluster) in group.clusters.iter().enumerate() {
                    if ci > 0 {
                        x += cluster_gap - gap;
                    }
                    let x_start = x;
                    for tile in &cluster.tiles {
                        let rect = SceneRect { x0: x, y0: y + HEADING_H, x1: x + tw, y1: y + HEADING_H + 1.0 };
                        let label = SceneRect { x0: x, y0: rect.y1 + LABEL_TOP_H, x1: x + tw, y1: y + HEADING_H + 1.0 + LABEL_H };
                        out.tiles.push(SceneTile { workspace_id: tile.workspace_id, group: gi, cluster: ci, rect, label });
                        x += tw + gap;
                    }
                    let outline = OUTLINE_W * tw;
                    regions.push(if cluster.is_group_head || cluster.tiles.is_empty() {
                        vec![]
                    } else {
                        vec![SceneRect {
                            x0: x_start - outline,
                            y0: y + HEADING_H - OUTLINE_W,
                            x1: x - gap + outline,
                            y1: y + HEADING_H + 1.0 + LABEL_H,
                        }]
                    });
                }
                let width = (x - gap).max(0.0);
                let slide_min = (window - width).min(0.0);
                let slide = slides.get(&group.key).copied().unwrap_or(0.0).clamp(slide_min, 0.0);
                for tile in &mut out.tiles[first_tile..] {
                    tile.rect = tile.rect.shifted(slide);
                    tile.label = tile.label.shifted(slide);
                }
                for region in regions.iter_mut().flatten() {
                    *region = region.shifted(slide);
                }
                let heading = SceneRect { x0: 0.0, y0: y, x1: window.max(width), y1: y + HEADING_H };
                let bounds = SceneRect { x0: slide, y0: y, x1: slide + width, y1: y + HEADING_H + 1.0 + LABEL_H }.union(&heading);
                out.groups.push(SceneGroup { key: group.key.clone(), heading, bounds, regions, slide, slide_min });
                y += HEADING_H + 1.0 + LABEL_H + ROW_GAP_H;
            }
        }
        ViewMode::Grid => {
            let gap = GAP_W * tw;
            let columns = options.grid_columns;
            let (cell_w, cell_h, block_pad) = (tw + gap, 1.0 + LABEL_H + gap, BLOCK_PAD_W * tw);
            let block_w = columns as f64 * cell_w - gap + 2.0 * block_pad;
            let mut column_y = vec![0.0f64; options.grid_block_columns];
            for (gi, group) in layout.groups.iter().enumerate() {
                let column = (0..column_y.len()).min_by(|&a, &b| column_y[a].total_cmp(&column_y[b])).unwrap_or(0);
                let (bx, by) = (column as f64 * (block_w + BLOCK_GAP_W * tw), column_y[column]);
                let count: usize = group.clusters.iter().map(|c| c.tiles.len()).sum();
                let rows = count.div_ceil(columns).max(1);
                let bounds =
                    SceneRect { x0: bx, y0: by, x1: bx + block_w, y1: by + HEADING_H + block_pad + rows as f64 * cell_h - gap + block_pad };
                let heading = SceneRect { x0: bx + block_pad, y0: by, x1: bx + block_w, y1: by + HEADING_H };
                let mut regions = vec![];
                let mut cell = 0;
                for (ci, cluster) in group.clusters.iter().enumerate() {
                    let mut region = vec![];
                    for tile in &cluster.tiles {
                        let x = bx + block_pad + (cell % columns) as f64 * cell_w;
                        let y = by + HEADING_H + block_pad + (cell / columns) as f64 * cell_h;
                        let rect = SceneRect { x0: x, y0: y, x1: x + tw, y1: y + 1.0 };
                        let label = SceneRect { x0: x, y0: y + 1.0 + LABEL_TOP_H, x1: x + tw, y1: y + 1.0 + LABEL_H };
                        out.tiles.push(SceneTile { workspace_id: tile.workspace_id, group: gi, cluster: ci, rect, label });
                        if !cluster.is_group_head {
                            region.push(SceneRect {
                                x0: x - gap / 2.0,
                                y0: y - gap / 2.0,
                                x1: x + cell_w - gap / 2.0,
                                y1: y + cell_h - gap / 2.0,
                            });
                        }
                        cell += 1;
                    }
                    regions.push(region);
                }
                column_y[column] = bounds.y1 + BLOCK_GAP_H;
                out.groups.push(SceneGroup { key: group.key.clone(), heading, bounds, regions, slide: 0.0, slide_min: 0.0 });
            }
        }
    }
    let pad = PAD_W * tw;
    let content = out.groups.iter().map(|g| g.bounds).reduce(|a, b| a.union(&b)).unwrap_or_default();
    out.bounds = SceneRect { x0: content.x0.min(0.0) - pad, y0: content.y0 - pad, x1: content.x1 + pad, y1: content.y1 + pad };
    out
}

/// Rows: the width of the part of a row the overview shows.
fn row_window(options: &SceneOptions) -> f64 {
    options.row_window_tiles * options.aspect * (1.0 + GAP_W)
}

impl Scene {
    pub fn tile(&self, workspace: WorkspaceId) -> Option<&SceneTile> {
        self.tiles.iter().find(|t| t.workspace_id == workspace)
    }

    /// The camera that shows `workspace` whole: the workspace, open.
    pub fn rest_camera(&self, workspace: WorkspaceId) -> Option<Camera> {
        let rect = self.tile(workspace)?.rect;
        Some(Camera { cx: (rect.x0 + rect.x1) / 2.0, cy: (rect.y0 + rect.y1) / 2.0, w: rect.width() })
    }

    /// The overview's camera. Rows: the left part of every row, as many
    /// rows as fit, starting from `current`'s. Grid: the whole scene, or, when
    /// it is too wide to read, its width around `current`. `pan_cy`, where the
    /// overview has been scrolled to, overrides the starting row.
    pub fn overview_camera(&self, current: Option<WorkspaceId>, pan_cy: Option<f64>) -> Camera {
        let aspect = self.options.aspect;
        let b = self.bounds;
        let current = current.and_then(|id| self.tile(id));
        let (w, cx, start) = match self.options.mode {
            ViewMode::Rows => {
                let w = row_window(&self.options) + 2.0 * PAD_W * aspect;
                let top = current.map(|t| self.groups[t.group].bounds.y0 - PAD_W * aspect);
                // Rows start at 0 however far any has slid.
                (w, w / 2.0 - PAD_W * aspect, top.map(|top| top + w / aspect / 2.0))
            }
            ViewMode::Grid => {
                if b.width() / aspect > self.options.max_tiles_across {
                    (b.width(), (b.x0 + b.x1) / 2.0, current.map(|t| (t.rect.y0 + t.rect.y1) / 2.0))
                } else {
                    let fit = Camera::fit(&b, aspect, 1.0);
                    (fit.w, fit.cx, None)
                }
            }
        };
        let h = w / aspect;
        let cy = if b.height() <= h {
            (b.y0 + b.y1) / 2.0
        } else {
            pan_cy.or(start).unwrap_or(b.y0 + h / 2.0).clamp(b.y0 + h / 2.0, b.y1 - h / 2.0)
        };
        Camera { cx, cy, w }
    }

    /// The camera that fits group `group`, its heading included.
    pub fn group_camera(&self, group: usize) -> Option<Camera> {
        Some(Camera::fit(&self.groups.get(group)?.bounds, self.options.aspect, GROUP_MARGIN))
    }

    /// Rows: the slide that brings `workspace`'s tile into the part of its
    /// row the overview shows, moving the row no more than it must, with
    /// the row it is in. `None` when it is already in view, or in a grid.
    pub fn slide_into(&self, workspace: WorkspaceId) -> Option<(GroupKey, f64)> {
        if self.options.mode != ViewMode::Rows {
            return None;
        }
        let tile = self.tile(workspace)?;
        let group = &self.groups[tile.group];
        let window = row_window(&self.options);
        // The tile's place with the row unslid.
        let (x0, x1) = (tile.rect.x0 - group.slide, tile.rect.x1 - group.slide);
        // Rounding in placing a slid row isn't a reason to slide it again.
        let eps = 1e-9 * window;
        let mut slide = group.slide;
        if x0 + slide < -eps {
            slide = -x0;
        }
        if x1 + slide > window + eps {
            slide = window - x1;
        }
        let slide = slide.clamp(group.slide_min, 0.0);
        ((slide - group.slide).abs() > eps).then(|| (group.key.clone(), slide))
    }

    /// Rows: the part of group `group`'s row the overview shows.
    pub fn row_window(&self, group: usize) -> Option<SceneRect> {
        let bounds = self.groups.get(group)?.bounds;
        Some(SceneRect { x0: 0.0, x1: row_window(&self.options), ..bounds })
    }
}

impl Camera {
    /// The camera that shows `rect` whole, centred, with `margin` to spare
    /// (1 is none).
    pub fn fit(rect: &SceneRect, aspect: f64, margin: f64) -> Camera {
        let w = rect.width().max(rect.height() * aspect) * margin;
        Camera { cx: (rect.x0 + rect.x1) / 2.0, cy: (rect.y0 + rect.y1) / 2.0, w }
    }

    /// The part of the scene the camera shows.
    pub fn view(&self, aspect: f64) -> SceneRect {
        let h = self.w / aspect;
        SceneRect { x0: self.cx - self.w / 2.0, y0: self.cy - h / 2.0, x1: self.cx + self.w / 2.0, y1: self.cy + h / 2.0 }
    }
}
