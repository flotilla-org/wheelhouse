//! A change of layout drawn as movement (docs/design/overview-scene.md,
//! "Animating a change of layout"): every tile is the same workspace before
//! and after, so it moves from its old place to its new one. What it carries
//! (its label, its cluster's shading) moves with it; a heading moves with
//! its group. A workspace that is new grows from the middle of its place;
//! one that has gone is simply gone.
use std::collections::BTreeMap;

use super::scene::{Scene, SceneRect};
use crate::WorkspaceId;

/// How long a change of layout takes to draw.
pub const MORPH_MS: f64 = 350.0;

/// The map taking rectangle `from` onto `to`, scaling each axis.
#[derive(Debug, Clone, Copy)]
struct Fit {
    from: SceneRect,
    to: SceneRect,
}

impl Fit {
    fn apply(&self, r: &SceneRect) -> SceneRect {
        let axis = |v: f64, f0: f64, f1: f64, t0: f64, t1: f64| if f1 > f0 { t0 + (v - f0) * (t1 - t0) / (f1 - f0) } else { t0 + (v - f0) };
        SceneRect {
            x0: axis(r.x0, self.from.x0, self.from.x1, self.to.x0, self.to.x1),
            y0: axis(r.y0, self.from.y0, self.from.y1, self.to.y0, self.to.y1),
            x1: axis(r.x1, self.from.x0, self.from.x1, self.to.x0, self.to.x1),
            y1: axis(r.y1, self.from.y0, self.from.y1, self.to.y0, self.to.y1),
        }
    }
}

fn lerp(a: &SceneRect, b: &SceneRect, t: f64) -> SceneRect {
    let mix = |a: f64, b: f64| a + (b - a) * t;
    SceneRect { x0: mix(a.x0, b.x0), y0: mix(a.y0, b.y0), x1: mix(a.x1, b.x1), y1: mix(a.y1, b.y1) }
}

/// The smallest rectangle holding all of `rects` (at least one).
fn hull(rects: impl Iterator<Item = SceneRect>) -> SceneRect {
    rects.reduce(|a, b| SceneRect { x0: a.x0.min(b.x0), y0: a.y0.min(b.y0), x1: a.x1.max(b.x1), y1: a.y1.max(b.y1) }).unwrap_or_default()
}

/// `r` scaled by `t` about its middle: how something new grows in.
fn grown(r: &SceneRect, t: f64) -> SceneRect {
    let (cx, cy) = ((r.x0 + r.x1) / 2.0, (r.y0 + r.y1) / 2.0);
    SceneRect { x0: cx + (r.x0 - cx) * t, y0: cy + (r.y0 - cy) * t, x1: cx + (r.x1 - cx) * t, y1: cy + (r.y1 - cy) * t }
}

/// The scene `t` of the way (0 to 1, already eased) from `from` to `to`: `to`
/// with every tile, label, region, heading and group moved part way from
/// where it was in `from`. Everything else is `to`'s.
pub fn morph(from: &Scene, to: &Scene, t: f64) -> Scene {
    let t = t.clamp(0.0, 1.0);
    let mut out = to.clone();
    let before: BTreeMap<WorkspaceId, SceneRect> = from.tiles.iter().map(|tile| (tile.workspace_id, tile.rect)).collect();
    // Each tile's map from where it goes to where it is drawn, by group and
    // cluster, in order.
    let mut fits: BTreeMap<(usize, usize), Vec<Fit>> = BTreeMap::new();
    for tile in &mut out.tiles {
        let drawn = match before.get(&tile.workspace_id) {
            Some(was) => lerp(was, &tile.rect, t),
            None => grown(&tile.rect, t),
        };
        let fit = Fit { from: tile.rect, to: drawn };
        tile.label = fit.apply(&tile.label);
        tile.rect = drawn;
        fits.entry((tile.group, tile.cluster)).or_default().push(fit);
    }
    for (gi, group) in out.groups.iter_mut().enumerate() {
        let drawn = match from.groups.iter().find(|g| g.key == group.key) {
            Some(was) => lerp(&was.bounds, &group.bounds, t),
            None => grown(&group.bounds, t),
        };
        let fit = Fit { from: group.bounds, to: drawn };
        group.heading = fit.apply(&group.heading);
        group.bounds = drawn;
        // A grid's cluster has a region per tile, which moves with its tile;
        // rows' has one for all its tiles, which moves with all of them.
        for (ci, regions) in group.regions.iter_mut().enumerate() {
            let Some(tiles) = fits.get(&(gi, ci)) else { continue };
            let whole = Fit { from: hull(tiles.iter().map(|f| f.from)), to: hull(tiles.iter().map(|f| f.to)) };
            let per_tile = regions.len() == tiles.len();
            for (k, region) in regions.iter_mut().enumerate() {
                *region = if per_tile { tiles[k].apply(region) } else { whole.apply(region) };
            }
        }
    }
    out.bounds = lerp(&from.bounds, &to.bounds, t);
    out
}
