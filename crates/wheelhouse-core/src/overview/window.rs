//! A window's overview: what its workspace region's camera is doing, and the
//! scene it looks at. The host feeds it the sidebar's live workspaces, the
//! window's current workspace and the region's shape, passes on the user's
//! input, and draws the scene through `camera` (docs/design/overview-scene.md).
use std::borrow::Cow;
use std::collections::{BTreeMap, BTreeSet};

use super::camera::{ease_out, Camera, CameraRig, FlightTiming};
use super::logical::{logical_layout, GroupKey, LogicalLayout, OverviewEntry};
use super::morph::{morph, MORPH_MS};
use super::scene::{scene, GridPacking, Scene, SceneOptions, SceneRect, ViewMode, Zoom};
use super::scroller::{Extent, Range, Scroller};
use crate::{PlacementKey, WorkspaceId};

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum OverviewMode {
    /// The camera is on the current workspace: it is open.
    #[default]
    Workspace,
    /// The camera shows the overview.
    Overview,
    /// The camera fits one group.
    Group(GroupKey),
}

#[derive(Debug, Clone)]
pub struct OverviewWindow {
    options: SceneOptions,
    timing: FlightTiming,
    motion: bool,
    mode: OverviewMode,
    current: Option<WorkspaceId>,
    entries: Vec<OverviewEntry>,
    slides: BTreeMap<GroupKey, f64>,
    collapsed: BTreeSet<PlacementKey>,
    /// The centre the overview has been scrolled to, if it has, and how far
    /// a grid has been zoomed by hand.
    pan: Option<(f64, f64)>,
    zoom: Option<Zoom>,
    layout: LogicalLayout,
    scene: Scene,
    rig: Option<CameraRig>,
    morph: Option<Morph>,
    /// The smallest readable tile width the host last gave, and the
    /// region's width in the same units.
    readable_width: f64,
    region_width: f64,
    /// A press on the overview not yet a drag: the row under it, if any,
    /// and when it was pressed.
    press: Option<(Option<usize>, f64)>,
    /// The drag under way, and one let go of that is still gliding or
    /// springing back (or was caught by the press).
    held: Option<Gesture>,
    loose: Option<Gesture>,
}

/// What a drag moves: a row's slide, or the overview's camera across or
/// down.
#[derive(Debug, Clone, PartialEq)]
enum Target {
    Slide(GroupKey),
    Pan(Axis),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Axis {
    Across,
    Down,
}

#[derive(Debug, Clone)]
struct Gesture {
    target: Target,
    scroller: Scroller,
}

/// A stretch past an end, drawn on top of the scene: a row's (`group`)
/// across, or the whole overview's across or down. In scene units, as the
/// row's slide or the camera's centre moves.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Stretch {
    pub group: Option<usize>,
    pub x: f64,
    pub y: f64,
}

/// A change of layout being drawn.
#[derive(Debug, Clone)]
struct Morph {
    /// The scene as drawn when it began.
    from: Scene,
    start_ms: f64,
    /// What the camera was resting on when it began: it stays on that as
    /// it moves, until a flight or a scroll takes it elsewhere.
    track: Option<Track>,
}

/// A camera staying on what it was aimed at through a change of layout. It
/// starts where it was drawn, which may be off its aim (an earlier change
/// still under way, or a flight landed in one), and that offset eases out
/// with the change.
#[derive(Debug, Clone)]
struct Track {
    aim: Aim,
    shown: Camera,
    /// Where the aim was in the change's first frame; none when it wasn't
    /// there (growing in from nothing), and the camera eases from where it
    /// was shown to where it comes to rest.
    aimed: Option<Camera>,
}

impl Track {
    /// `on`, offset by as much of the start's offset from `aimed` as is
    /// `left` of the change.
    fn offset(&self, on: Camera, aimed: Camera, left: f64) -> Camera {
        Camera {
            cx: on.cx + (self.shown.cx - aimed.cx) * left,
            cy: on.cy + (self.shown.cy - aimed.cy) * left,
            w: on.w * (self.shown.w / aimed.w).powf(left),
        }
    }
}

fn usable(camera: &Camera) -> bool {
    camera.cx.is_finite() && camera.cy.is_finite() && camera.w.is_finite() && camera.w > 0.0
}

/// What a camera is aimed at, as the mode says.
#[derive(Debug, Clone)]
struct Aim {
    mode: OverviewMode,
    current: Option<WorkspaceId>,
    pan: Option<(f64, f64)>,
    zoom: Option<Zoom>,
}

impl Aim {
    fn camera(&self, scene: &Scene) -> Option<Camera> {
        match &self.mode {
            OverviewMode::Workspace => self.current.and_then(|id| scene.rest_camera(id)),
            OverviewMode::Overview => (!scene.tiles.is_empty()).then(|| scene.overview_camera(self.current, self.pan, self.zoom)),
            OverviewMode::Group(key) => scene.groups.iter().position(|g| &g.key == key).and_then(|i| scene.group_camera(i)),
        }
    }
}

impl Default for OverviewWindow {
    fn default() -> Self {
        Self::new(SceneOptions::default())
    }
}

impl OverviewWindow {
    pub fn new(options: SceneOptions) -> Self {
        let layout = LogicalLayout::default();
        let scene = scene(&layout, &options, &BTreeMap::new());
        Self {
            options,
            timing: FlightTiming::default(),
            motion: true,
            mode: OverviewMode::Workspace,
            current: None,
            entries: vec![],
            slides: BTreeMap::new(),
            collapsed: BTreeSet::new(),
            pan: None,
            zoom: None,
            layout,
            scene,
            rig: None,
            morph: None,
            readable_width: 0.0,
            region_width: 0.0,
            press: None,
            held: None,
            loose: None,
        }
    }

    /// Whether the camera flies; without motion (a setting, or the system's
    /// reduce motion) every move is a cut.
    pub fn set_motion(&mut self, motion: bool) {
        self.motion = motion;
    }

    pub fn set_timing(&mut self, timing: FlightTiming) {
        self.timing = timing;
    }

    /// Change the view mode. Each tile moves to its place in the new scene
    /// while the camera flies to the same place in it; without motion both
    /// cut.
    pub fn set_view_mode(&mut self, mode: ViewMode, now_ms: f64) {
        if self.options.mode != mode {
            self.options.mode = mode;
            self.relayout(now_ms);
        }
    }

    /// The grid's packing, the smallest width a tile is readable at, and the
    /// region's width, in the same units; their ratio is the share of the
    /// region a tile needs (`SceneOptions::readable_share`), and a readable
    /// width of 0 always fits the whole grid. A new packing moves to the new
    /// scene as a new view mode does. A new readable width goes back to the
    /// current tile from wherever the overview was scrolled; a region that
    /// changed width keeps its scrolling, within the scene.
    pub fn set_grid(&mut self, packing: GridPacking, readable_width: f64, region_width: f64, now_ms: f64) {
        let readable = if readable_width.is_finite() { readable_width.max(0.0) } else { 0.0 };
        let share = if region_width.is_finite() && region_width > 0.0 { (readable / region_width).clamp(0.0, 1.0) } else { 0.0 };
        let new_readable = readable != self.readable_width;
        self.readable_width = readable;
        if region_width.is_finite() && region_width > 0.0 {
            self.region_width = region_width;
        }
        if self.options.packing != packing {
            self.options.packing = packing;
            self.options.readable_share = share;
            self.relayout(now_ms);
        } else if new_readable || self.options.readable_share != share {
            if new_readable {
                self.pan = None;
                self.zoom = None;
            }
            self.options.readable_share = share;
            self.rebuild();
            self.follow_scene(now_ms, true);
        }
    }

    /// A new arrangement of the same scene (a view mode, a packing): the
    /// overview's own scrolling and group are left behind, each tile moves
    /// to its new place and the camera flies to the same place in the new
    /// scene, since the old and new layouts frame the overview differently.
    fn relayout(&mut self, now_ms: f64) {
        self.hold(now_ms);
        self.pan = None;
        self.zoom = None;
        if matches!(self.mode, OverviewMode::Group(_)) {
            self.mode = OverviewMode::Overview;
        }
        self.begin_morph(now_ms);
        self.rebuild();
        self.go(now_ms);
    }

    /// Draw the coming change of layout as movement from the scene as it is
    /// drawn now; without motion, or before anything was drawn through the
    /// scene (no camera yet), it is a cut. Returns what a camera at rest was
    /// aimed at and where it was drawn, for `track` once the new scene is
    /// built.
    fn begin_morph(&mut self, now_ms: f64) -> Option<(Aim, Camera)> {
        let held = (!self.flying(now_ms)).then(|| self.camera(now_ms)).flatten().map(|shown| (self.aim(), shown));
        self.morph =
            (self.motion && self.rig.is_some()).then(|| Morph { from: self.scene_at(now_ms).into_owned(), start_ms: now_ms, track: None });
        held
    }

    /// Keep a camera that was at rest on what it was aimed at, from where it
    /// was drawn, as the new scene's layout is drawn. It is anchored in the
    /// change's first frame, which already lacks what has gone. A camera
    /// aimed at something new to the scene stays where the change leaves
    /// it.
    fn track(&mut self, held: Option<(Aim, Camera)>) {
        let Some(m) = &mut self.morph else { return };
        m.track = held.map(|(aim, shown)| {
            let aimed = aim.camera(&morph(&m.from, &self.scene, 0.0)).filter(usable);
            Track { aim, shown, aimed }
        });
    }

    /// Feed the sidebar's live workspaces (`super::entries`), the window's
    /// current workspace and its workspace region's width over height. A new
    /// current workspace opens: the camera flies to it from wherever it is,
    /// whether it was showing another workspace or the overview. Workspaces
    /// coming and going move each tile to its new place, and the camera
    /// with the scene; a changed shape moves the camera with the scene
    /// without a flight. A flight under way re-aims at where its destination
    /// went.
    pub fn update(&mut self, entries: Vec<OverviewEntry>, current: Option<WorkspaceId>, aspect: f64, now_ms: f64) {
        let changed = entries != self.entries;
        if changed {
            self.entries = entries;
        }
        self.follow(changed, current, aspect, now_ms);
    }

    /// `update` with the workspaces it was last given, for a host that
    /// knows the sidebar has not changed since.
    pub fn update_unchanged(&mut self, current: Option<WorkspaceId>, aspect: f64, now_ms: f64) {
        self.follow(false, current, aspect, now_ms);
    }

    fn follow(&mut self, changed: bool, current: Option<WorkspaceId>, aspect: f64, now_ms: f64) {
        self.advance_gestures(now_ms);
        let aspect = if aspect.is_finite() && aspect > 0.0 { aspect } else { self.options.aspect };
        let reshaped = aspect != self.options.aspect;
        let switched = current != self.current;
        if switched {
            self.hold(now_ms);
        }
        let held = (changed && !reshaped).then(|| self.begin_morph(now_ms));
        if changed || reshaped || switched {
            self.options.aspect = aspect;
            self.current = current;
            self.rebuild();
        }
        if let Some(held) = held {
            self.track(held);
        }
        if switched {
            self.mode = OverviewMode::Workspace;
            self.pan = None;
            self.zoom = None;
            self.go(now_ms);
        } else {
            self.follow_scene(now_ms, reshaped);
        }
    }

    /// After the scene moved: a flight re-aims at where its destination
    /// went; a camera at rest, or one in a region that changed shape, stays
    /// on what it shows, through a change of layout as it is drawn.
    fn follow_scene(&mut self, now_ms: f64, reshaped: bool) {
        if self.target().is_some_and(|target| self.rig.is_none_or(|rig| rig.destination() != target)) {
            if self.flying(now_ms) && !reshaped {
                self.go(now_ms);
            } else {
                self.cut();
            }
        }
    }

    /// Show the overview, with the current workspace's row slid to it.
    pub fn open(&mut self, now_ms: f64) {
        if self.mode == OverviewMode::Overview {
            return;
        }
        self.hold(now_ms);
        self.mode = OverviewMode::Overview;
        self.pan = None;
        self.zoom = None;
        if let Some(current) = self.current {
            if let Some((key, slide)) = self.scene.slide_into(current) {
                self.slides.insert(key, slide);
                self.rebuild();
            }
        }
        self.go(now_ms);
    }

    /// Go back one level: from a group to the overview, from the overview to
    /// the current workspace. False when the workspace is already open.
    pub fn back(&mut self, now_ms: f64) -> bool {
        if self.mode != OverviewMode::Workspace {
            self.hold(now_ms);
        }
        self.mode = match self.mode {
            OverviewMode::Workspace => return false,
            OverviewMode::Group(_) => OverviewMode::Overview,
            OverviewMode::Overview => OverviewMode::Workspace,
        };
        self.go(now_ms);
        true
    }

    /// Open the current workspace from wherever the camera is.
    pub fn close(&mut self, now_ms: f64) {
        if self.mode != OverviewMode::Workspace {
            self.hold(now_ms);
            self.mode = OverviewMode::Workspace;
            self.go(now_ms);
        }
    }

    /// Fit group `group` (an index into the scene's groups).
    pub fn zoom_group(&mut self, group: usize, now_ms: f64) {
        if let Some(key) = self.scene.groups.get(group).map(|g| g.key.clone()) {
            self.hold(now_ms);
            self.mode = OverviewMode::Group(key);
            self.go(now_ms);
        }
    }

    /// Open `workspace` from the overview: the camera flies into its tile.
    /// The host also makes it the window's current workspace. If the host
    /// doesn't, its next `update` passes the old current workspace, which
    /// reads as a switch, and the camera flies back to it.
    pub fn pick(&mut self, workspace: WorkspaceId, now_ms: f64) {
        if self.scene.tile(workspace).is_none() {
            return;
        }
        self.hold(now_ms);
        self.current = Some(workspace);
        self.mode = OverviewMode::Workspace;
        self.rebuild();
        self.go(now_ms);
    }

    /// Rows: slide group `group`'s row by `by` scene units (negative shows
    /// what is further right), within its range.
    pub fn slide(&mut self, group: usize, by: f64, now_ms: f64) {
        self.stop_glide();
        if let Some(group) = self.scene.groups.get(group) {
            let slide = (group.slide + by).clamp(group.slide_min, 0.0);
            self.slides.insert(group.key.clone(), slide);
            self.rebuild();
            self.follow_scene(now_ms, false);
        }
    }

    /// Scroll the overview right and down (or left and up) by `dx` and `dy`
    /// scene units, as far as it goes: rows only scroll down, and a grid
    /// only when it is zoomed in.
    pub fn pan(&mut self, dx: f64, dy: f64) {
        self.stop_glide();
        if self.mode != OverviewMode::Overview {
            return;
        }
        let from = self.scene.overview_camera(self.current, self.pan, self.zoom);
        // Kept within the scene, as the camera keeps itself. A pan that
        // moves nothing (a grid that fits) isn't kept, or it would become
        // where the grid later zooms in.
        let to = self.scene.overview_camera(self.current, Some((from.cx + dx, from.cy + dy)), self.zoom);
        if to != from {
            self.pan = Some((to.cx, to.cy));
            if let Some(m) = &mut self.morph {
                m.track = None;
            }
        }
        self.cut();
    }

    /// A grid can be zoomed by hand: the overview is open on one.
    pub fn zoomable(&self) -> bool {
        self.mode == OverviewMode::Overview && self.options.mode == ViewMode::Grid && !self.scene.tiles.is_empty()
    }

    /// Zoom a grid in by `factor` (below 1 zooms out) about the point at
    /// `at_x`, `at_y`, as shares of the region's width and height: what is
    /// there stays there. From everything fitting to the narrowest tile
    /// filling the region. A cut, like a pan. Zoomed all the way out it
    /// holds as Fit, so the next zoom in starts from everything fitting.
    pub fn zoom_by(&mut self, factor: f64, at_x: f64, at_y: f64) {
        self.stop_glide();
        if !self.zoomable() || !factor.is_finite() || factor <= 0.0 {
            return;
        }
        let aspect = self.options.aspect;
        let from = self.scene.overview_camera(self.current, self.pan, self.zoom);
        let (closest, fit) = self.scene.zoom_range();
        let w = (from.w / factor).clamp(closest, fit);
        let (at_x, at_y) =
            (if at_x.is_finite() { at_x.clamp(0.0, 1.0) } else { 0.5 }, if at_y.is_finite() { at_y.clamp(0.0, 1.0) } else { 0.5 });
        let view = from.view(aspect);
        let (x, y) = (view.x0 + at_x * view.width(), view.y0 + at_y * view.height());
        let centre = (x + (0.5 - at_x) * w, y + (0.5 - at_y) * w / aspect);
        let zoom = if w >= fit { Zoom::Fit } else { Zoom::Width(w) };
        let to = self.scene.overview_camera(self.current, Some(centre), Some(zoom));
        self.zoom = Some(zoom);
        self.pan = (zoom != Zoom::Fit).then_some((to.cx, to.cy));
        if let Some(m) = &mut self.morph {
            m.track = None;
        }
        self.cut();
    }

    /// The whole grid is in view.
    pub fn fits_all(&self) -> bool {
        self.zoomable() && self.target().is_some_and(|t| t.w >= self.scene.zoom_range().1 * (1.0 - 1e-9))
    }

    /// The Fit all toggle: fly out to the whole grid, or, when it is all in
    /// view, back to where the grid starts (zoomed in on the current tile
    /// if it would be too small to read). When the grid starts all in view
    /// the two are the same camera, and the toggle stays marked.
    pub fn fit_all(&mut self, now_ms: f64) {
        if !self.zoomable() {
            return;
        }
        self.hold(now_ms);
        self.zoom = if self.fits_all() { None } else { Some(Zoom::Fit) };
        self.pan = None;
        self.go(now_ms);
    }

    /// Collapse or expand cluster `cluster` of group `group`.
    pub fn toggle_cluster(&mut self, group: usize, cluster: usize, now_ms: f64) {
        let Some(key) = self.layout.groups.get(group).and_then(|g| g.clusters.get(cluster)).map(|c| c.key.clone()) else {
            return;
        };
        if !self.collapsed.remove(&key) {
            self.collapsed.insert(key);
        }
        let held = self.begin_morph(now_ms);
        self.rebuild();
        self.track(held);
        self.follow_scene(now_ms, false);
    }

    pub fn mode(&self) -> &OverviewMode {
        &self.mode
    }

    pub fn current(&self) -> Option<WorkspaceId> {
        self.current
    }

    pub fn layout(&self) -> &LogicalLayout {
        &self.layout
    }

    /// Where everything goes: the scene the cameras are aimed in.
    pub fn scene(&self) -> &Scene {
        &self.scene
    }

    /// The scene as drawn at `now_ms`: part way from the last one, while a
    /// change of layout is drawn, else `scene`.
    pub fn scene_at(&self, now_ms: f64) -> Cow<'_, Scene> {
        match self.morph.as_ref().filter(|m| now_ms < m.start_ms + MORPH_MS) {
            Some(m) => Cow::Owned(morph(&m.from, &self.scene, ease_out((now_ms - m.start_ms) / MORPH_MS))),
            None => Cow::Borrowed(&self.scene),
        }
    }

    /// A change of layout is being drawn: the host draws again next frame.
    pub fn morphing(&self, now_ms: f64) -> bool {
        self.morph.as_ref().is_some_and(|m| now_ms < m.start_ms + MORPH_MS)
    }

    /// Where the camera is at `now_ms`, once there is a current workspace or
    /// the overview has something to show. A camera at rest through a change
    /// of layout stays on what it shows as that moves.
    pub fn camera(&self, now_ms: f64) -> Option<Camera> {
        let rig = self.rig.map(|rig| rig.camera(now_ms));
        if let Some((m, track)) = self.morph.as_ref().and_then(|m| Some((m, m.track.as_ref()?))) {
            if self.morphing(now_ms) && !self.flying(now_ms) {
                let left = 1.0 - ease_out((now_ms - m.start_ms) / MORPH_MS);
                let on = track.aimed.and_then(|aimed| Some((track.aim.camera(&self.scene_at(now_ms)).filter(usable)?, aimed)));
                return match on {
                    Some((on, aimed)) => Some(track.offset(on, aimed, left)),
                    None => rig.map(|rest| track.offset(rest, rest, left)),
                };
            }
        }
        rig
    }

    /// A flight is under way: the host asks for frames until it ends.
    pub fn flying(&self, now_ms: f64) -> bool {
        self.rig.is_some_and(|rig| rig.flying(now_ms))
    }

    /// The current workspace is open and still: the host draws it as itself
    /// rather than through the scene.
    pub fn settled(&self, now_ms: f64) -> bool {
        self.mode == OverviewMode::Workspace && !self.flying(now_ms) && self.current.is_some_and(|id| self.scene.tile(id).is_some())
    }

    fn rebuild(&mut self) {
        self.layout = logical_layout(&self.entries, self.current, &self.collapsed);
        self.scene = scene(&self.layout, &self.options, &self.slides);
        // Keep each row's slide as the scene clamped it.
        for group in &self.scene.groups {
            if self.slides.contains_key(&group.key) {
                self.slides.insert(group.key.clone(), group.slide);
            }
        }
        if let OverviewMode::Group(key) = &self.mode {
            if !self.scene.groups.iter().any(|g| &g.key == key) {
                self.mode = OverviewMode::Overview;
            }
        }
    }

    /// Where the camera comes to rest for the mode: the current workspace's
    /// tile, the overview, or a group.
    pub fn target(&self) -> Option<Camera> {
        self.aim().camera(&self.scene)
    }

    fn aim(&self) -> Aim {
        Aim { mode: self.mode.clone(), current: self.current, pan: self.pan, zoom: self.zoom }
    }

    /// Before a move aims the camera elsewhere: a camera staying on a moving
    /// layout stops where it is drawn, so a flight leaves from there.
    fn hold(&mut self, now_ms: f64) {
        let shown = self.camera(now_ms);
        if let (Some(rig), Some(_), Some(shown)) = (&mut self.rig, self.morph.as_mut().and_then(|m| m.track.take()), shown) {
            rig.jump_to(shown);
        }
    }

    fn go(&mut self, now_ms: f64) {
        self.stop_glide();
        let Some(target) = self.target() else { return };
        if let Some(m) = &mut self.morph {
            m.track = None;
        }
        match &mut self.rig {
            Some(rig) if self.motion => rig.fly_to(target, now_ms, &self.timing),
            Some(rig) => rig.jump_to(target),
            None => self.rig = Some(CameraRig::new(target)),
        }
    }

    fn cut(&mut self) {
        let Some(target) = self.target() else { return };
        match &mut self.rig {
            Some(rig) => rig.jump_to(target),
            None => self.rig = Some(CameraRig::new(target)),
        }
    }

    // ---------- grabbing, dragging and gliding (wheelhouse#527) ----------

    /// A press on the overview, on row `group` or between rows: it catches
    /// whatever is gliding or springing back where it is drawn, and may
    /// become a drag. True when it caught something, so the press isn't
    /// also a click.
    pub fn grab(&mut self, group: Option<usize>, now_ms: f64) -> bool {
        if self.mode == OverviewMode::Workspace {
            return false;
        }
        self.press = Some((group, now_ms));
        self.held = None;
        let Some(mut gesture) = self.loose.take() else { return false };
        let caught = gesture.scroller.grab(self.extent(&gesture.target), now_ms);
        self.loose = Some(gesture);
        caught
    }

    /// The press has moved `dx` and `dy` since it began, as shares of the
    /// region's width and height. Its first move picks what it drags:
    /// mostly sideways over a row too long for the region slides the row,
    /// else pans an overview wider than the region; mostly up or down pans
    /// one taller than it. What it drags follows, stretching past its ends.
    pub fn drag(&mut self, dx: f64, dy: f64, now_ms: f64) {
        let (dx, dy) = (if dx.is_finite() { dx } else { 0.0 }, if dy.is_finite() { dy } else { 0.0 });
        if self.held.is_none() {
            let Some((group, pressed_ms)) = self.press.take() else { return };
            let Some(target) = self.drag_target(group, dx, dy) else {
                // Nothing to drag that way: the rest of the press does nothing.
                self.release_loose(now_ms);
                return;
            };
            self.held = Some(match self.loose.take() {
                Some(gesture) if gesture.target == target && gesture.scroller.held() => gesture,
                other => {
                    self.loose = other;
                    self.release_loose(now_ms);
                    // Held since the press, so the first move has a speed.
                    let mut scroller = Scroller::at(self.place(&target).unwrap_or(0.0));
                    scroller.grab(self.extent(&target), pressed_ms);
                    Gesture { target, scroller }
                }
            });
        }
        let Some(mut gesture) = self.held.take() else { return };
        let Some(view) = self.camera(now_ms).map(|c| c.view(self.options.aspect)) else { return };
        // The content follows the hand: a row moves with it, and the camera
        // the other way.
        let by = match gesture.target {
            Target::Slide(_) => dx * view.width(),
            Target::Pan(Axis::Across) => -dx * view.width(),
            Target::Pan(Axis::Down) => -dy * view.height(),
        };
        match self.range(&gesture.target) {
            Some(range) => {
                let extent = self.extent(&gesture.target);
                gesture.scroller.drag(by, range, extent, now_ms);
                self.put(&gesture.target, gesture.scroller.place(), now_ms);
                self.held = Some(gesture);
            }
            // What it dragged has gone.
            None => self.held = None,
        }
    }

    /// The press ends: what was dragged glides on at the drag's speed, or
    /// springs back from past its end.
    pub fn release(&mut self, now_ms: f64) {
        self.press = None;
        if let Some(gesture) = self.held.take() {
            self.release_loose(now_ms);
            self.loose = Some(gesture);
        }
        if let Some(gesture) = &mut self.loose {
            gesture.scroller.release(self.motion, now_ms);
        }
        self.settle_loose();
    }

    /// Something is gliding or springing back: the host draws again next
    /// frame.
    pub fn moving(&self) -> bool {
        self.loose.as_ref().is_some_and(|g| g.scroller.moving())
    }

    /// The stretch past an end being drawn, if any.
    pub fn stretch(&self) -> Option<Stretch> {
        let gesture = self.held.as_ref().or(self.loose.as_ref())?;
        let over = gesture.scroller.over();
        if over == 0.0 || self.mode == OverviewMode::Workspace {
            return None;
        }
        Some(match &gesture.target {
            Target::Slide(key) => Stretch { group: Some(self.scene.groups.iter().position(|g| &g.key == key)?), x: over, y: 0.0 },
            Target::Pan(Axis::Across) => Stretch { group: None, x: over, y: 0.0 },
            Target::Pan(Axis::Down) => Stretch { group: None, x: 0.0, y: over },
        })
    }

    /// The scene as drawn at `now_ms`: part way through a change of layout,
    /// with a row stretched past its end.
    pub fn drawn_scene(&self, now_ms: f64) -> Cow<'_, Scene> {
        let scene = self.scene_at(now_ms);
        let Some(Stretch { group: Some(group), x, .. }) = self.stretch() else { return scene };
        let mut scene = scene.into_owned();
        let shift = |r: &mut SceneRect| {
            r.x0 += x;
            r.x1 += x;
        };
        for tile in scene.tiles.iter_mut().filter(|t| t.group == group) {
            shift(&mut tile.rect);
            shift(&mut tile.label);
        }
        if let Some(g) = scene.groups.get_mut(group) {
            shift(&mut g.bounds);
            g.regions.iter_mut().flatten().for_each(shift);
        }
        Cow::Owned(scene)
    }

    /// The camera as drawn at `now_ms`: with the overview stretched past
    /// its end.
    pub fn drawn_camera(&self, now_ms: f64) -> Option<Camera> {
        let camera = self.camera(now_ms)?;
        Some(match self.stretch() {
            Some(Stretch { group: None, x, y }) => Camera { cx: camera.cx + x, cy: camera.cy + y, ..camera },
            _ => camera,
        })
    }

    /// What a drag that has moved `dx`, `dy` from a press on `group` moves.
    fn drag_target(&self, group: Option<usize>, dx: f64, dy: f64) -> Option<Target> {
        let slides = group.and_then(|g| self.scene.groups.get(g)).filter(|g| self.options.mode == ViewMode::Rows && g.slide_min < 0.0);
        // Compared as drawn: a share of the width is `aspect` times as long
        // as the same share of the height.
        if (dx * self.options.aspect).abs() >= dy.abs() {
            match slides {
                Some(g) => Some(Target::Slide(g.key.clone())),
                None => self.pans(Axis::Across).then_some(Target::Pan(Axis::Across)),
            }
        } else {
            self.pans(Axis::Down).then_some(Target::Pan(Axis::Down))
        }
    }

    /// The overview is larger than the region that way, so it pans: down
    /// when it is taller; across only for a grid wider than it (rows slide
    /// instead).
    fn pans(&self, axis: Axis) -> bool {
        if self.mode != OverviewMode::Overview || (axis == Axis::Across && self.options.mode != ViewMode::Grid) {
            return false;
        }
        self.range(&Target::Pan(axis)).is_some_and(|r| r.hi - r.lo > 1e-9)
    }

    /// Where a target is now: the row's slide, or the overview camera's
    /// centre.
    fn place(&self, target: &Target) -> Option<f64> {
        match target {
            Target::Slide(key) => self.scene.groups.iter().find(|g| &g.key == key).map(|g| g.slide),
            Target::Pan(axis) => {
                let camera = self.scene.overview_camera(self.current, self.pan, self.zoom);
                Some(if *axis == Axis::Across { camera.cx } else { camera.cy })
            }
        }
    }

    /// The ends a target keeps to: a row from its slide_min to 0, the
    /// overview's camera within the scene. None once it can't move.
    fn range(&self, target: &Target) -> Option<Range> {
        match target {
            Target::Slide(key) => self.scene.groups.iter().find(|g| &g.key == key).map(|g| Range::new(g.slide_min, 0.0)),
            Target::Pan(axis) => {
                if self.mode != OverviewMode::Overview || self.scene.tiles.is_empty() {
                    return None;
                }
                let view = self.scene.overview_camera(self.current, self.pan, self.zoom).view(self.options.aspect);
                let bounds = self.scene.bounds;
                let (lo, hi, half) = match axis {
                    Axis::Across => (bounds.x0, bounds.x1, view.width() / 2.0),
                    Axis::Down => (bounds.y0, bounds.y1, view.height() / 2.0),
                };
                Some(Range::new(lo + half, (hi - half).max(lo + half)))
            }
        }
    }

    /// How long the region is along a target's axis, in scene units, and
    /// how long a point is: the host's region width, in points, when it has
    /// given one.
    /// From the camera's destination, not the camera as drawn: a stretch and
    /// a spring are solved in units of the extent, so it holds still through
    /// a flight. A drag's distance comes from the camera as drawn, so the
    /// content follows the hand; the two differ only during a flight.
    fn extent(&self, target: &Target) -> Extent {
        let view = self.camera_view();
        let points_across = if self.region_width > 0.0 { self.region_width } else { 1000.0 };
        let (length, points) = match target {
            Target::Pan(Axis::Down) => (view.height(), points_across / self.options.aspect),
            _ => (view.width(), points_across),
        };
        let length = if length.is_finite() && length > 0.0 { length } else { 1.0 };
        Extent { length, point: length / points.max(1.0) }
    }

    fn camera_view(&self) -> SceneRect {
        let camera = self.rig.map(|rig| rig.destination()).unwrap_or(Camera { cx: 0.0, cy: 0.0, w: 1.0 });
        camera.view(self.options.aspect)
    }

    /// Move a target to `place`, as a drag or glide does: a row's slide, or
    /// the overview scrolled there, without stopping the glide doing it.
    fn put(&mut self, target: &Target, place: f64, now_ms: f64) {
        match target {
            Target::Slide(key) => {
                if self.slides.get(key) != Some(&place) {
                    self.slides.insert(key.clone(), place);
                    self.rebuild();
                    self.follow_scene(now_ms, false);
                }
            }
            Target::Pan(axis) => {
                let camera = self.scene.overview_camera(self.current, self.pan, self.zoom);
                let centre = match axis {
                    Axis::Across => (place, camera.cy),
                    Axis::Down => (camera.cx, place),
                };
                let to = self.scene.overview_camera(self.current, Some(centre), self.zoom);
                if to != camera {
                    self.pan = Some((to.cx, to.cy));
                    self.cut();
                }
            }
        }
    }

    /// Each frame: what was let go of glides or springs on, following its
    /// target as the scene moves under it; it stops once its target is
    /// gone or the overview closes.
    fn advance_gestures(&mut self, now_ms: f64) {
        if self.mode == OverviewMode::Workspace {
            self.press = None;
            self.held = None;
            self.loose = None;
            return;
        }
        let Some(mut gesture) = self.loose.take() else { return };
        if gesture.scroller.held() {
            self.loose = Some(gesture);
            return;
        }
        // What it moved has gone: the gesture stops, and leaves nothing to put.
        let Some(range) = self.range(&gesture.target) else { return };
        let extent = self.extent(&gesture.target);
        gesture.scroller.advance(range, extent, now_ms);
        self.put(&gesture.target, gesture.scroller.place(), now_ms);
        self.loose = Some(gesture);
        self.settle_loose();
    }

    /// Let go of a gesture caught by a press that turned into something
    /// else: a stretch springs back from where it was caught.
    fn release_loose(&mut self, now_ms: f64) {
        if let Some(gesture) = &mut self.loose {
            gesture.scroller.release(self.motion, now_ms);
        }
        self.settle_loose();
    }

    fn settle_loose(&mut self) {
        if self.loose.as_ref().is_some_and(|g| !g.scroller.moving() && !g.scroller.held()) {
            self.loose = None;
        }
    }

    /// A wheel's scroll, a pick or a flight stops a glide where it is; a
    /// stretch still springs back.
    fn stop_glide(&mut self) {
        if let Some(gesture) = &mut self.loose {
            gesture.scroller.stop_glide();
        }
        self.settle_loose();
    }
}
