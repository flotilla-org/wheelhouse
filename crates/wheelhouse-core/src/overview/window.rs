//! A window's overview: what its workspace region's camera is doing, and the
//! scene it looks at. The host feeds it the sidebar's live workspaces, the
//! window's current workspace and the region's shape, passes on the user's
//! input, and draws the scene through `camera` (docs/design/overview-scene.md).
use std::collections::{BTreeMap, BTreeSet};

use super::camera::{Camera, CameraRig, FlightTiming};
use super::logical::{logical_layout, GroupKey, LogicalLayout, OverviewEntry};
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

    /// Change the view mode; the camera cuts to the same place in the new
    /// scene.
    pub fn set_view_mode(&mut self, mode: ViewMode) {
        if self.options.mode != mode {
            self.options.mode = mode;
            self.pan = None;
            if matches!(self.mode, OverviewMode::Group(_)) {
                self.mode = OverviewMode::Overview;
            }
            self.rebuild();
            self.cut();
        }
    }

    /// The grid's packing, and the share of the region's width a tile needs
    /// to be readable (`SceneOptions::readable_share`). A new packing cuts to
    /// the same place in the new scene, as a new view mode does; a new share
    /// moves the overview's camera with it.
    pub fn set_grid(&mut self, packing: GridPacking, readable_share: f64, now_ms: f64) {
        let share = if readable_share.is_finite() { readable_share.clamp(0.0, 1.0) } else { 0.0 };
        if self.options.packing != packing {
            self.options.packing = packing;
            self.options.readable_share = share;
            self.pan = None;
            if matches!(self.mode, OverviewMode::Group(_)) {
                self.mode = OverviewMode::Overview;
            }
            self.rebuild();
            self.cut();
        } else if self.options.readable_share != share {
            self.options.readable_share = share;
            self.rebuild();
            self.follow_scene(now_ms, true);
        }
    }

    /// Feed the sidebar's live workspaces (`super::entries`), the window's
    /// current workspace and its workspace region's width over height. A new
    /// current workspace opens: the camera flies to it from wherever it is,
    /// whether it was showing another workspace or the overview. A changed
    /// shape, or workspaces coming and going, moves the camera with the scene
    /// without a flight; a flight under way re-aims at where its destination
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
        if changed || reshaped || switched {
            self.options.aspect = aspect;
            self.current = current;
            self.rebuild();
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
    /// on what it shows.
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
            self.mode = OverviewMode::Workspace;
            self.go(now_ms);
        }
    }

    /// Fit group `group` (an index into the scene's groups).
    pub fn zoom_group(&mut self, group: usize, now_ms: f64) {
        if let Some(group) = self.scene.groups.get(group) {
            self.mode = OverviewMode::Group(group.key.clone());
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
        self.rebuild();
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

    pub fn scene(&self) -> &Scene {
        &self.scene
    }

    /// Where the camera is at `now_ms`, once there is a current workspace or
    /// the overview has something to show.
    pub fn camera(&self, now_ms: f64) -> Option<Camera> {
        self.rig.map(|rig| rig.camera(now_ms))
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
        match &self.mode {
            OverviewMode::Workspace => self.current.and_then(|id| self.scene.rest_camera(id)),
            OverviewMode::Overview => (!self.scene.tiles.is_empty()).then(|| self.scene.overview_camera(self.current, self.pan)),
            OverviewMode::Group(key) => self.scene.groups.iter().position(|g| &g.key == key).and_then(|i| self.scene.group_camera(i)),
        }
    }

    fn go(&mut self, now_ms: f64) {
        let Some(target) = self.target() else { return };
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
