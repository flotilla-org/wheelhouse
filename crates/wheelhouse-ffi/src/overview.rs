//! A window's overview (docs/design/overview-scene.md): one scene that every
//! workspace the sidebar shows live sits in, and a camera on it that is the
//! window's workspace region. A workspace is open when the camera fits its
//! tile; opening the overview, picking a workspace and switching are camera
//! flights. The host feeds the overview its current workspace and region
//! shape each frame (wheelhouse_overview_update), passes on the user's
//! moves, and draws what wheelhouse_overview_frame gives through its camera.
//!
//! Scene units: a tile is 1 high and as wide as the region's width over its
//! height, so a camera that fits a tile shows its workspace whole. A point
//! (x, y) of the scene is at region.x0 + (x - view.x0) / (view.x1 - view.x0)
//! * region width across, and likewise down.
use super::*;
use wheelhouse_core::overview::{entries, Camera, OverviewMode, SceneRect, ViewMode};

/// The overview's mode: the current workspace is open.
pub const WHEELHOUSE_OVERVIEW_WORKSPACE: u32 = 0;
/// The overview's mode: the camera shows the overview.
pub const WHEELHOUSE_OVERVIEW_OVERVIEW: u32 = 1;
/// The overview's mode: the camera fits one group (the frame's `group`).
pub const WHEELHOUSE_OVERVIEW_GROUP: u32 = 2;

/// A view mode: a sliding row per group.
pub const WHEELHOUSE_OVERVIEW_ROWS: u32 = 0;
/// A view mode: groups as blocks of a grid.
pub const WHEELHOUSE_OVERVIEW_GRID: u32 = 1;

/// A rectangle of the scene.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WheelhouseOverviewRect {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
}
impl From<SceneRect> for WheelhouseOverviewRect {
    fn from(r: SceneRect) -> Self {
        Self { x0: r.x0, y0: r.y0, x1: r.x1, y1: r.y1 }
    }
}

/// A workspace's tile.
#[repr(C)]
pub struct WheelhouseOverviewTile {
    pub workspace_id: WorkspaceIdView,
    /// Indexes into the frame's groups, and the group's clusters.
    pub group: usize,
    pub cluster: usize,
    pub rect: WheelhouseOverviewRect,
    /// Where its title goes, under it.
    pub label: WheelhouseOverviewRect,
    pub title: Text,
    /// The kind of what it is the workspace of; empty for a plain one.
    pub kind: Text,
    /// What its sidebar row presents: status, attention and icon, as a
    /// stream node's are; absent ones are empty.
    pub status: Text,
    pub attention: u32,
    pub icon: Text,
    /// The window's current workspace.
    pub current: u32,
}

/// What a cluster (a convoy and its vessels, say) covers: one rectangle
/// in rows, one per cell in the grid.
#[repr(C)]
pub struct WheelhouseOverviewRegion {
    pub group: usize,
    pub cluster: usize,
    pub rect: WheelhouseOverviewRect,
}

#[repr(C)]
pub struct WheelhouseOverviewGroup {
    pub heading: Text,
    /// Where the heading goes; clicking it fits the group.
    pub heading_rect: WheelhouseOverviewRect,
    pub bounds: WheelhouseOverviewRect,
    /// Rows: how far the row is slid (0 to slide_min, which is negative or
    /// 0 when the row fits).
    pub slide: f64,
    pub slide_min: f64,
}

/// What the region shows at one moment.
#[repr(C)]
pub struct WheelhouseOverviewFrame {
    /// WHEELHOUSE_OVERVIEW_WORKSPACE, _OVERVIEW or _GROUP.
    pub mode: u32,
    /// The fitted group's index, for WHEELHOUSE_OVERVIEW_GROUP; else
    /// WHEELHOUSE_NONE.
    pub group: usize,
    /// WHEELHOUSE_OVERVIEW_ROWS or _GRID.
    pub view_mode: u32,
    /// The camera is moving: draw again next frame.
    pub flying: u32,
    /// The current workspace is open and still: draw it as itself, not
    /// through the scene.
    pub settled: u32,
    /// Whether there is a camera yet (there is none until there is a current
    /// workspace or something to show); without one, draw the workspace as
    /// itself.
    pub has_view: u32,
    /// The part of the scene the region shows.
    pub view: WheelhouseOverviewRect,
    pub bounds: WheelhouseOverviewRect,
    pub tiles: *const WheelhouseOverviewTile,
    pub tile_count: usize,
    pub groups: *const WheelhouseOverviewGroup,
    pub group_count: usize,
    pub regions: *const WheelhouseOverviewRegion,
    pub region_count: usize,
}
type FrameKeep = (Vec<WheelhouseOverviewTile>, Vec<WheelhouseOverviewGroup>, Vec<WheelhouseOverviewRegion>);

fn current(id: WorkspaceIdView) -> Option<WorkspaceId> {
    (id.bytes != [0; 16]).then(|| id.into())
}

/// Each frame, before reading it: the window's current workspace (zeroes for
/// none), its workspace region's width over height, the time, and whether
/// the camera moves (0 for a setting or the system's reduce motion: every
/// move is then a cut). A new current workspace opens with a flight from
/// wherever the camera is. It reads the sidebar's live workspaces when they
/// change.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_update(
    h: *mut WheelhouseCore,
    current_workspace: WorkspaceIdView,
    aspect: f64,
    now_ms: f64,
    motion: u32,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        h.overview.set_motion(motion != 0);
        let snapshot = h.sidebar.snapshot_shared();
        if h.overview_revision == Some(snapshot.revision) {
            h.overview.update_unchanged(current(current_workspace), aspect, now_ms);
        } else {
            h.overview_revision = Some(snapshot.revision);
            h.overview.update(entries(&snapshot.surface), current(current_workspace), aspect, now_ms);
        }
        Ok(())
    })
    .is_some() as u32
}

/// Show the overview, with the current workspace's row slid into view.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_open(h: *mut WheelhouseCore, now_ms: f64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| {
        h.overview.open(now_ms);
        Ok(())
    })
    .is_some() as u32
}

/// Open the current workspace from wherever the camera is.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_close(h: *mut WheelhouseCore, now_ms: f64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| {
        h.overview.close(now_ms);
        Ok(())
    })
    .is_some() as u32
}

/// Go back one level (Escape): from a group to the overview, from the
/// overview to the current workspace. 0 when the workspace was already open,
/// and on an error.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_back(h: *mut WheelhouseCore, now_ms: f64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| Ok(h.overview.back(now_ms))).unwrap_or(false) as u32
}

/// Fit group `group` (an index into the frame's groups).
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_zoom_group(
    h: *mut WheelhouseCore,
    group: usize,
    now_ms: f64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        h.overview.zoom_group(group, now_ms);
        Ok(())
    })
    .is_some() as u32
}

/// Open a tile's workspace: the camera flies into it. The host also makes it
/// the window's current workspace, which the next update confirms.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_pick(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    now_ms: f64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        h.overview.pick(workspace.into(), now_ms);
        Ok(())
    })
    .is_some() as u32
}

/// Rows: slide group `group`'s row by `by` scene units (negative shows what
/// is further right), within its range.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_slide(
    h: *mut WheelhouseCore,
    group: usize,
    by: f64,
    now_ms: f64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        h.overview.slide(group, if by.is_finite() { by } else { 0.0 }, now_ms);
        Ok(())
    })
    .is_some() as u32
}

/// Scroll the overview down (or up) by `by` scene units.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_pan(h: *mut WheelhouseCore, by: f64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| {
        h.overview.pan(if by.is_finite() { by } else { 0.0 });
        Ok(())
    })
    .is_some() as u32
}

/// Collapse or expand a cluster (indexes into the frame's groups and the
/// group's clusters).
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_toggle_cluster(
    h: *mut WheelhouseCore,
    group: usize,
    cluster: usize,
    now_ms: f64,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        h.overview.toggle_cluster(group, cluster, now_ms);
        Ok(())
    })
    .is_some() as u32
}

/// WHEELHOUSE_OVERVIEW_ROWS or _GRID; the camera cuts to the same place.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_set_view_mode(h: *mut WheelhouseCore, view_mode: u32, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| {
        let mode = match view_mode {
            WHEELHOUSE_OVERVIEW_ROWS => ViewMode::Rows,
            WHEELHOUSE_OVERVIEW_GRID => ViewMode::Grid,
            other => return Err(format!("unknown overview view mode {other}")),
        };
        h.overview.set_view_mode(mode);
        Ok(())
    })
    .is_some() as u32
}

/// The region at `now_ms`. Its text is borrowed from the overview: free
/// it with wheelhouse_overview_frame_free before the next call that changes
/// the overview.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_frame(
    h: *mut WheelhouseCore,
    now_ms: f64,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseOverviewFrame {
    run(h, error_out, |h| {
        let window = &h.overview;
        let scene = window.scene();
        let layout = window.layout();
        let text = |value: &Option<String>| Text::borrowed(value.as_deref().unwrap_or(""));
        let tiles: Vec<WheelhouseOverviewTile> = scene
            .tiles
            .iter()
            .filter_map(|t| {
                let tile = layout
                    .groups
                    .get(t.group)
                    .and_then(|g| g.clusters.get(t.cluster))
                    .and_then(|c| c.tiles.iter().find(|x| x.workspace_id == t.workspace_id));
                // The scene is placed from this layout, so every tile is in
                // it; a release build drops a tile rather than crash if not.
                debug_assert!(tile.is_some(), "scene tile {t:?} isn't in the layout it was placed from");
                let tile = tile?;
                Some(WheelhouseOverviewTile {
                    workspace_id: t.workspace_id.into(),
                    group: t.group,
                    cluster: t.cluster,
                    rect: t.rect.into(),
                    label: t.label.into(),
                    title: Text::borrowed(&tile.title),
                    kind: Text::borrowed(&tile.kind),
                    status: text(&tile.presented.status),
                    attention: tile.presented.attention as u32,
                    icon: text(&tile.presented.icon),
                    current: tile.current as u32,
                })
            })
            .collect();
        let groups: Vec<WheelhouseOverviewGroup> = scene
            .groups
            .iter()
            .enumerate()
            .map(|(i, g)| WheelhouseOverviewGroup {
                heading: Text::borrowed(layout.groups.get(i).map_or("", |l| l.heading.as_str())),
                heading_rect: g.heading.into(),
                bounds: g.bounds.into(),
                slide: g.slide,
                slide_min: g.slide_min,
            })
            .collect();
        let regions: Vec<WheelhouseOverviewRegion> = scene
            .groups
            .iter()
            .enumerate()
            .flat_map(|(group, g)| {
                g.regions.iter().enumerate().flat_map(move |(cluster, rects)| {
                    rects.iter().map(move |r| WheelhouseOverviewRegion { group, cluster, rect: (*r).into() })
                })
            })
            .collect();
        let (mode, group) = match window.mode() {
            OverviewMode::Workspace => (WHEELHOUSE_OVERVIEW_WORKSPACE, NONE),
            OverviewMode::Overview => (WHEELHOUSE_OVERVIEW_OVERVIEW, NONE),
            OverviewMode::Group(key) => (WHEELHOUSE_OVERVIEW_GROUP, scene.groups.iter().position(|g| &g.key == key).unwrap_or(NONE)),
        };
        let camera = window.camera(now_ms);
        let view = camera.unwrap_or(Camera { cx: 0.0, cy: 0.0, w: 1.0 }).view(scene.options.aspect);
        let value = WheelhouseOverviewFrame {
            mode,
            group,
            view_mode: match scene.options.mode {
                ViewMode::Rows => WHEELHOUSE_OVERVIEW_ROWS,
                ViewMode::Grid => WHEELHOUSE_OVERVIEW_GRID,
            },
            flying: window.flying(now_ms) as u32,
            settled: window.settled(now_ms) as u32,
            has_view: camera.is_some() as u32,
            view: view.into(),
            bounds: scene.bounds.into(),
            tiles: tiles.as_ptr(),
            tile_count: tiles.len(),
            groups: groups.as_ptr(),
            group_count: groups.len(),
            regions: regions.as_ptr(),
            region_count: regions.len(),
        };
        Ok(give(value, (tiles, groups, regions)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_overview_frame_free(frame: *mut WheelhouseOverviewFrame) {
    take::<_, FrameKeep>(frame)
}
