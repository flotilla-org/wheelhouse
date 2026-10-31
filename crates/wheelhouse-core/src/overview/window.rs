//! A window's overview: what its workspace region's camera is doing, and the
//! scene it looks at. The host feeds it the sidebar's live workspaces, the
//! window's current workspace and the region's shape, passes on the user's
//! input, and draws the scene through `camera` (docs/design/overview-scene.md).
use std::borrow::Cow;
use std::collections::{BTreeMap, BTreeSet};

use super::camera::{ease_out, Camera, CameraRig, FlightTiming};
use super::logical::{logical_layout, GroupKey, LogicalLayout, OverviewEntry};
use super::morph::{morph, MORPH_MS};
use super::scene::{scene, GridPacking, Scene, SceneOptions, ViewMode};
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
    /// The centre the overview has been scrolled to, if it has.
    pan: Option<(f64, f64)>,
    layout: LogicalLayout,
    scene: Scene,
    rig: Option<CameraRig>,
    morph: Option<Morph>,
    /// The smallest readable tile width the host last gave.
    readable_width: f64,
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
}

impl Aim {
    fn camera(&self, scene: &Scene) -> Option<Camera> {
        match &self.mode {
            OverviewMode::Workspace => self.current.and_then(|id| scene.rest_camera(id)),
            OverviewMode::Overview => (!scene.tiles.is_empty()).then(|| scene.overview_camera(self.current, self.pan)),
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
            layout,
            scene,
            rig: None,
            morph: None,
            readable_width: 0.0,
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
        if self.options.packing != packing {
            self.options.packing = packing;
            self.options.readable_share = share;
            self.relayout(now_ms);
        } else if new_readable || self.options.readable_share != share {
            if new_readable {
                self.pan = None;
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
        if self.mode != OverviewMode::Overview {
            return;
        }
        let from = self.scene.overview_camera(self.current, self.pan);
        // Kept within the scene, as the camera keeps itself. A pan that
        // moves nothing (a grid that fits) isn't kept, or it would become
        // where the grid later zooms in.
        let to = self.scene.overview_camera(self.current, Some((from.cx + dx, from.cy + dy)));
        if to != from {
            self.pan = Some((to.cx, to.cy));
            if let Some(m) = &mut self.morph {
                m.track = None;
            }
        }
        self.cut();
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
        Aim { mode: self.mode.clone(), current: self.current, pan: self.pan }
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
}
