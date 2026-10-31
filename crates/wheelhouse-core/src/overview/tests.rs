use std::collections::{BTreeMap, BTreeSet};

use hegel::generators as gs;
use hegel::TestCase;

use super::*;
use crate::presentation::{Presented, UNPLACED_WORKSPACES_SECTION};
use crate::scaling_tests::{fleet, patch, text, Shape, CONFIG, PROVIDER};
use crate::sidebar::{Sidebar, Workspace};
use crate::{tab_workspace_id, EntityRef, MetadataTarget, PlacementKey, PlacementSegment, WorkspaceId};

fn close(a: f64, b: f64) -> bool {
    (a - b).abs() <= 1e-9 * (1.0 + a.abs().max(b.abs()))
}

fn same(a: Camera, b: Camera) -> bool {
    close(a.cx, b.cx) && close(a.cy, b.cy) && close(a.w, b.w)
}

fn draw_camera(tc: &TestCase) -> Camera {
    let coordinate = || tc.draw(gs::floats::<f64>().min_value(-1e4).max_value(1e4).allow_nan(false).allow_infinity(false));
    Camera {
        cx: coordinate(),
        cy: coordinate(),
        w: tc.draw(gs::floats::<f64>().min_value(0.05).max_value(2e3).allow_nan(false).allow_infinity(false)),
    }
}

fn draw_rho(tc: &TestCase) -> f64 {
    tc.draw(gs::floats::<f64>().min_value(0.5).max_value(2.6).allow_nan(false).allow_infinity(false))
}

// ---------- camera ----------

/// The path agrees with d3.interpolateZoom's formula, computed separately
/// (rho 1.4): a neighbour, a far pan, a zoom in place and a zoom with a pan.
#[test]
fn the_path_matches_the_papers_formula() {
    let cases = [
        ((0.0, 0.0, 1.6), (1.728, 0.0, 1.6), 1.317245289828144, [(0.351778026641, 0.0, 2.102335191037), (0.864, 0.0, 2.329750852259)]),
        (
            (0.0, 0.0, 1.6),
            (20.0, -3.0, 1.6),
            4.587748239075419,
            [(0.744748863238, -0.111712329486, 7.673937602898), (10.0, -1.5, 19.883752160998)],
        ),
        ((5.0, 5.0, 1.6), (5.0, 5.0, 9.0), 1.23372924863606, [(5.0, 5.0, 2.464056230593), (5.0, 5.0, 3.794733192202)]),
        (
            (0.0, 0.0, 9.0),
            (3.0, 4.0, 1.6),
            1.7978660855592852,
            [(1.61804735544, 2.15739647392, 7.90915616942), (2.547169811321, 3.396226415094, 5.168020623629)],
        ),
    ];
    for (from, to, length, at) in cases {
        let path = ZoomPath::new(Camera { cx: from.0, cy: from.1, w: from.2 }, Camera { cx: to.0, cy: to.1, w: to.2 }, 1.4);
        assert!(close(path.length, length), "{from:?} to {to:?}: length {} not {length}", path.length);
        for (t, (cx, cy, w)) in [0.25, 0.5].into_iter().zip(at) {
            let got = path.at(t);
            assert!(
                (got.cx - cx).abs() < 1e-9 && (got.cy - cy).abs() < 1e-9 && (got.w - w).abs() < 1e-9,
                "{from:?} to {to:?} at {t}: {got:?}"
            );
        }
    }
}

/// Every path starts and ends exactly at its cameras and stays finite, with
/// the camera's width rising to one peak, if any, and falling: it never dips
/// below the narrower end.
#[hegel::test(test_cases = 500)]
fn paths_end_where_they_should_and_pull_back_once(tc: TestCase) {
    let (from, to, rho) = (draw_camera(&tc), draw_camera(&tc), draw_rho(&tc));
    let path = ZoomPath::new(from, to, rho);
    assert_eq!(path.at(0.0), from);
    assert_eq!(path.at(1.0), to);
    let widths: Vec<f64> = (0..=64)
        .map(|i| path.at(i as f64 / 64.0))
        .map(|c| {
            assert!(c.cx.is_finite() && c.cy.is_finite() && c.w.is_finite() && c.w > 0.0, "{from:?} to {to:?}: {c:?}");
            c.w
        })
        .collect();
    let floor = from.w.min(to.w) * (1.0 - 1e-9);
    assert!(widths.iter().all(|&w| w >= floor), "{from:?} to {to:?} dips below its ends: {widths:?}");
    let peak = widths.iter().enumerate().max_by(|a, b| a.1.total_cmp(b.1)).map(|(i, _)| i).unwrap();
    let tolerance = |a: f64| a * 1e-9;
    assert!(widths[..=peak].windows(2).all(|p| p[1] >= p[0] - tolerance(p[0])), "{from:?} to {to:?} isn't rising to its peak: {widths:?}");
    assert!(
        widths[peak..].windows(2).all(|p| p[1] <= p[0] + tolerance(p[0])),
        "{from:?} to {to:?} isn't falling after its peak: {widths:?}"
    );
}

/// Between two workspaces the same size, a farther one pulls the camera
/// back at least as far and takes at least as long.
#[hegel::test(test_cases = 300)]
fn farther_pans_pull_back_further(tc: TestCase) {
    let rho = draw_rho(&tc);
    let w = tc.draw(gs::floats::<f64>().min_value(0.1).max_value(10.0).allow_nan(false).allow_infinity(false));
    let distance = || tc.draw(gs::floats::<f64>().min_value(0.01).max_value(1e3).allow_nan(false).allow_infinity(false));
    let (near, far) = {
        let (a, b) = (distance(), distance());
        (a.min(b), a.max(b))
    };
    let from = Camera { cx: 0.0, cy: 0.0, w };
    let peak = |d: f64| {
        let path = ZoomPath::new(from, Camera { cx: d, cy: 0.0, w }, rho);
        (path.at(0.5).w, path.length)
    };
    let ((near_peak, near_length), (far_peak, far_length)) = (peak(near), peak(far));
    assert!(far_peak >= near_peak * (1.0 - 1e-9), "{far} pulls back to {far_peak}, {near} to {near_peak}");
    assert!(far_length >= near_length * (1.0 - 1e-9));
}

/// A flight lasts its length's time, kept between the shortest and longest;
/// one started mid-flight takes its share of that, never less than its
/// floor nor more than the full time. The same camera is no flight.
#[hegel::test(test_cases = 300)]
fn flights_are_timed_by_their_length(tc: TestCase) {
    let timing = FlightTiming::default();
    let (from, to) = (draw_camera(&tc), draw_camera(&tc));
    let interrupted = tc.draw(gs::booleans());
    match Flight::new(from, to, 0.0, &timing, interrupted) {
        None => assert!(ZoomPath::new(from, to, timing.rho).length < 1e-9),
        Some(flight) => {
            let full = (flight.path.length * timing.ms_per_unit).clamp(timing.min_ms, timing.max_ms);
            assert!(full >= timing.min_ms && full <= timing.max_ms);
            if interrupted {
                assert!(flight.duration_ms <= full);
                assert!(flight.duration_ms >= timing.interrupted_min_ms.min(full));
                assert!(flight.duration_ms <= (full * timing.interrupted_share).max(timing.interrupted_min_ms));
            } else {
                assert_eq!(flight.duration_ms, full);
            }
            assert!(same(flight.camera(0.0), from) && flight.camera(flight.duration_ms) == to);
        }
    }
    assert!(Flight::new(from, from, 0.0, &timing, interrupted).is_none());
}

/// However often the destination changes, and whenever, the camera never
/// jumps: each new flight starts where the camera was. Once the last one's
/// time is up it is at the last destination, and only a flight started
/// mid-flight is shortened.
#[hegel::test(test_cases = 300)]
fn the_camera_flies_from_where_it_is_and_nothing_queues(tc: TestCase) {
    let timing = FlightTiming::default();
    let mut rig = CameraRig::new(draw_camera(&tc));
    let mut now = 0.0;
    for _ in 0..tc.draw(gs::integers::<usize>().min_value(1).max_value(12)) {
        now += tc.draw(gs::floats::<f64>().min_value(0.0).max_value(800.0).allow_nan(false).allow_infinity(false));
        let before = rig.camera(now);
        let was_flying = rig.flying(now);
        let to = draw_camera(&tc);
        rig.fly_to(to, now, &timing);
        // A move too small to see (a millionth of the view) may snap.
        let after = rig.camera(now);
        let unseen = |a: f64, b: f64| (a - b).abs() <= 1e-6 * before.w;
        assert!(
            unseen(after.cx, before.cx) && unseen(after.cy, before.cy) && unseen(after.w, before.w),
            "the camera jumped from {before:?} to {after:?}"
        );
        assert_eq!(rig.destination(), to);
        if let Some(full) = Flight::new(before, to, now, &timing, false) {
            let last = Flight::new(before, to, now, &timing, was_flying).unwrap();
            assert!(rig.flying(now) && rig.flying(now + last.duration_ms - 1e-6) && !rig.flying(now + last.duration_ms));
            if !was_flying {
                assert_eq!(last.duration_ms, full.duration_ms);
            }
        }
    }
    assert_eq!(rig.camera(now + FlightTiming::default().max_ms), rig.destination());
}

/// Timing that gives a flight no time, or a NaN of it, cuts instead of
/// flying, and timing whose shortest is past its longest doesn't panic.
#[test]
fn timing_without_time_cuts() {
    let (from, to) = (Camera { cx: 0.0, cy: 0.0, w: 1.6 }, Camera { cx: 20.0, cy: -3.0, w: 1.6 });
    let base = FlightTiming::default();
    for timing in [
        FlightTiming { min_ms: 0.0, max_ms: 0.0, ..base },
        FlightTiming { max_ms: -5.0, min_ms: -10.0, ..base },
        FlightTiming { ms_per_unit: f64::NAN, min_ms: f64::NAN, max_ms: f64::NAN, ..base },
    ] {
        assert_eq!(Flight::new(from, to, 0.0, &timing, false), None, "{timing:?}");
        let mut rig = CameraRig::new(from);
        rig.fly_to(to, 0.0, &timing);
        assert!(!rig.flying(0.0) && rig.camera(0.0) == to);
    }
    let inverted = FlightTiming { min_ms: 900.0, max_ms: 100.0, ..base };
    assert_eq!(Flight::new(from, to, 0.0, &inverted, false).unwrap().duration_ms, 100.0);
}

/// Between cameras with no finite path (one 0 wide or less, or not
/// finite), there is no flight: the camera cuts rather than going NaN.
#[test]
fn cameras_without_a_finite_path_cut() {
    let good = Camera { cx: 0.0, cy: 0.0, w: 1.6 };
    for bad in [
        Camera { w: 0.0, ..good },
        Camera { cx: 20.0, w: 0.0, ..good },
        Camera { cx: 20.0, w: -1.0, ..good },
        Camera { cx: f64::NAN, ..good },
        Camera { w: f64::INFINITY, ..good },
    ] {
        for (from, to) in [(good, bad), (bad, good)] {
            assert_eq!(Flight::new(from, to, 0.0, &FlightTiming::default(), false), None, "{from:?} to {to:?}");
        }
    }
}

/// Flights ease out: halfway through its time, a flight is seven eighths of
/// the way along its path.
#[test]
fn flights_ease_out() {
    let (from, to) = (Camera { cx: 0.0, cy: 0.0, w: 1.6 }, Camera { cx: 20.0, cy: -3.0, w: 1.6 });
    let flight = Flight::new(from, to, 100.0, &FlightTiming::default(), false).unwrap();
    assert_eq!(ease_out(0.5), 0.875);
    assert!(same(flight.camera(100.0 + flight.duration_ms / 2.0), flight.path.at(0.875)));
}

// ---------- logical layout ----------

fn entity(kind: &str, id: impl std::fmt::Display) -> EntityRef {
    EntityRef::new("p", kind, id.to_string())
}

fn segment(loop_name: &str, entity: EntityRef) -> PlacementSegment {
    PlacementSegment { loop_name: loop_name.to_owned(), entity }
}

fn entry(section: &str, segments: Vec<PlacementSegment>, workspace: u64) -> OverviewEntry {
    OverviewEntry {
        section: section.to_owned(),
        labels: segments.iter().map(|s| format!("{} {}", s.entity.kind, s.entity.id)).collect(),
        key: PlacementKey(segments),
        workspace_id: tab_workspace_id(workspace),
        presented: Presented { status: Some("running".into()), ..Presented::default() },
    }
}

/// A sidebar's worth of live workspaces: a tree of projects, convoys and
/// vessels, some of whose nodes are open, some projects under a local
/// section as local groups are; some open workspaces also listed flat in an
/// attention section; some nothing places.
fn draw_entries(tc: &TestCase) -> Vec<OverviewEntry> {
    let mut next = 0;
    let mut open = || {
        next += 1;
        next
    };
    let mut tree = vec![];
    for p in 0..tc.draw(gs::integers::<usize>().max_value(4)) {
        let section =
            tc.draw(gs::booleans()).then(|| segment("section", entity(LOCAL_SECTION_KIND, tc.draw(gs::integers::<usize>().max_value(1)))));
        let path = |rest: Vec<PlacementSegment>| section.iter().cloned().chain(rest).collect::<Vec<_>>();
        let project = segment("project", entity("project", p));
        if tc.draw(gs::booleans()) {
            tree.push(entry("tree", path(vec![project.clone()]), open()));
        }
        for c in 0..tc.draw(gs::integers::<usize>().max_value(4)) {
            let convoy = segment("convoy", entity("convoy", format!("{p}.{c}")));
            if tc.draw(gs::booleans()) {
                tree.push(entry("tree", path(vec![project.clone(), convoy.clone()]), open()));
            }
            for v in 0..tc.draw(gs::integers::<usize>().max_value(3)) {
                if tc.draw(gs::booleans()) {
                    let vessel = segment("vessel", entity("vessel", format!("{p}.{c}.{v}")));
                    tree.push(entry("tree", path(vec![project.clone(), convoy.clone(), vessel]), open()));
                }
            }
        }
    }
    let mut attention = vec![];
    for listed in &tree {
        if tc.draw(gs::booleans()) {
            let last = listed.key.0.last().unwrap().clone();
            let mut flat = entry("attention", vec![segment("attention", last.entity)], 0);
            flat.workspace_id = listed.workspace_id;
            attention.push(flat);
        }
    }
    let unplaced: Vec<_> = (0..tc.draw(gs::integers::<usize>().max_value(2)))
        .map(|_| {
            let id = open();
            entry(UNPLACED_WORKSPACES_SECTION, vec![segment(UNPLACED_WORKSPACES_SECTION, entity(".workspace", id))], id)
        })
        .collect();
    let mut all = if tc.draw(gs::booleans()) { [attention, tree].concat() } else { [tree, attention].concat() };
    all.extend(unplaced);
    all
}

/// How many local sections a path starts with.
fn sections(key: &PlacementKey) -> usize {
    key.0.iter().take_while(|s| s.entity.kind == LOCAL_SECTION_KIND).count()
}

fn shown(layout: &LogicalLayout) -> Vec<WorkspaceId> {
    layout.groups.iter().flat_map(|g| g.clusters.iter()).flat_map(|c| c.tiles.iter()).map(|t| t.workspace_id).collect()
}

/// Every open workspace is in the layout once, shown or hidden, at its
/// deepest place in the sidebar: its group is the top of that path below
/// any local sections, headed by that node's label, and its cluster the
/// path's first two steps below them. Workspaces nothing places are grouped
/// together under one heading.
#[hegel::test(test_cases = 300)]
fn each_workspace_is_placed_once_by_its_hierarchy(tc: TestCase) {
    let entries = draw_entries(&tc);
    let layout = logical_layout(&entries, None, &BTreeSet::new());
    let mut seen = BTreeMap::new();
    for group in &layout.groups {
        for cluster in &group.clusters {
            assert!(cluster.hidden.is_empty(), "nothing is collapsed");
            for tile in &cluster.tiles {
                assert!(
                    seen.insert(tile.workspace_id, (group.key.clone(), cluster.key.clone())).is_none(),
                    "{:?} twice",
                    tile.workspace_id
                );
            }
        }
    }
    let open: BTreeSet<_> = entries.iter().map(|e| e.workspace_id).collect();
    assert_eq!(seen.keys().copied().collect::<BTreeSet<_>>(), open);
    for (workspace, (group, cluster)) in seen {
        // The first of its deepest places.
        let places = || entries.iter().filter(|e| e.workspace_id == workspace);
        let depth = places().map(|e| e.key.0.len()).max().unwrap();
        let deepest = places().find(|e| e.key.0.len() == depth).unwrap();
        if deepest.section == UNPLACED_WORKSPACES_SECTION {
            assert_eq!(group.head, None);
            let heading = &layout.groups.iter().find(|g| g.key == group).unwrap().heading;
            assert_eq!(heading, OTHER_WORKSPACES);
        } else {
            let start = sections(&deepest.key);
            assert_eq!(group.head.as_ref(), deepest.key.0.get(start).map(|s| &s.entity));
            assert_eq!(cluster.0, deepest.key.0[..deepest.key.0.len().min(start + 2)]);
            let heading = &layout.groups.iter().find(|g| g.key == group).unwrap().heading;
            assert_eq!(heading, &deepest.labels[start]);
        }
    }
    // A group head's own workspace is its own cluster; no two clusters share a key.
    for group in &layout.groups {
        let keys: BTreeSet<_> = group.clusters.iter().map(|c| &c.key).collect();
        assert_eq!(keys.len(), group.clusters.len());
        for cluster in &group.clusters {
            assert_eq!(cluster.is_group_head, group.key.head.is_some() && cluster.key.0.len() == sections(&cluster.key) + 1);
        }
    }
}

/// Tiles keep sidebar order: the order of each workspace's deepest place.
#[hegel::test(test_cases = 200)]
fn tiles_keep_sidebar_order_within_their_cluster(tc: TestCase) {
    let entries = draw_entries(&tc);
    let layout = logical_layout(&entries, None, &BTreeSet::new());
    let place = |id: WorkspaceId| {
        let deepest = entries.iter().filter(|e| e.workspace_id == id).map(|e| e.key.0.len()).max().unwrap();
        entries.iter().position(|e| e.workspace_id == id && e.key.0.len() == deepest).unwrap()
    };
    for cluster in layout.groups.iter().flat_map(|g| g.clusters.iter()) {
        let order: Vec<_> = cluster.tiles.iter().map(|t| place(t.workspace_id)).collect();
        assert!(order.windows(2).all(|p| p[0] < p[1]), "{order:?}");
    }
}

/// A collapsed cluster shows only its first workspace, unless it holds the
/// current one; the current workspace is always shown, and collapsing
/// moves nothing between clusters.
#[hegel::test(test_cases = 300)]
fn collapsing_hides_all_but_the_first_and_never_the_current(tc: TestCase) {
    let entries = draw_entries(&tc);
    let open = logical_layout(&entries, None, &BTreeSet::new());
    let keys: Vec<PlacementKey> = open.groups.iter().flat_map(|g| g.clusters.iter()).map(|c| c.key.clone()).collect();
    let collapsed: BTreeSet<PlacementKey> = keys.iter().filter(|_| tc.draw(gs::booleans())).cloned().collect();
    let all = shown(&open);
    let current = (!all.is_empty() && tc.draw(gs::booleans())).then(|| all[tc.draw(gs::integers::<usize>().max_value(all.len() - 1))]);
    let layout = logical_layout(&entries, current, &collapsed);
    if let Some(current) = current {
        assert!(shown(&layout).contains(&current));
        let tiles: Vec<_> = layout.groups.iter().flat_map(|g| g.clusters.iter()).flat_map(|c| c.tiles.iter()).collect();
        assert_eq!(tiles.iter().filter(|t| t.current).count(), 1);
        assert!(tiles.iter().any(|t| t.current && t.workspace_id == current));
    }
    for (before, after) in open.groups.iter().flat_map(|g| g.clusters.iter()).zip(layout.groups.iter().flat_map(|g| g.clusters.iter())) {
        assert_eq!(before.key, after.key);
        let ids = |tiles: &[Tile]| tiles.iter().map(|t| t.workspace_id).collect::<Vec<_>>();
        assert_eq!([ids(&after.tiles), ids(&after.hidden)].concat(), ids(&before.tiles));
        let holds_current = before.tiles.iter().any(|t| Some(t.workspace_id) == current);
        let hides = collapsed.contains(&after.key) && !holds_current && before.tiles.len() > 1;
        assert_eq!(after.tiles.len(), if hides { 1 } else { before.tiles.len() });
    }
}

/// From a real sidebar, where a convoy of one vessel shows as its vessel: a
/// workspace bound to a vessel is grouped under its project, in its own
/// cluster, titled by its label; one nothing binds is under the other
/// workspaces.
#[test]
fn a_sidebars_workspaces_group_by_project() {
    let shape = Shape { projects: 2, convoys_per_project: 2, change_request_every: 0, role_every: 0, active_every: 1, attention_every: 0 };
    let mut sidebar = Sidebar::new(CONFIG).unwrap();
    sidebar.apply(0, PROVIDER, fleet(shape, 4)).unwrap();
    let workspaces = (0..3)
        .map(|position| Workspace {
            id: tab_workspace_id(position + 1),
            position: position as usize,
            name: format!("w{position}"),
            selected: position == 0,
        })
        .collect();
    sidebar.observe(workspaces, vec![]);
    for (tab, vessel) in [(1, 0), (2, 3)] {
        let id = format!("fleet/convoy-{vessel:08x}");
        let mut bind = patch("vessel", &id, vec![(".host.kind".into(), text("vessel")), (".host.id".into(), text(id.clone()))]);
        bind.target = MetadataTarget::Tab(tab_workspace_id(tab));
        bind.source_id = "host".into();
        sidebar.apply(0, PROVIDER, [bind]).unwrap();
    }
    let entries = entries(&sidebar.snapshot_shared().surface);
    let layout = logical_layout(&entries, Some(tab_workspace_id(1)), &BTreeSet::new());
    let find = |id: u64| {
        layout
            .groups
            .iter()
            .find_map(|g| {
                g.clusters.iter().find_map(|c| c.tiles.iter().find(|t| t.workspace_id == tab_workspace_id(id)).map(|t| (g, c, t)))
            })
            .unwrap_or_else(|| panic!("workspace {id} isn't in {layout:#?}"))
    };
    let (group, cluster, tile) = find(1);
    assert_eq!(group.heading, "project 0");
    assert_eq!(cluster.key.0.last().unwrap().entity.id, "fleet/convoy-00000000");
    assert_eq!((tile.title.as_str(), tile.kind.as_str(), tile.current), ("work", "vessel", true));
    assert_eq!(find(2).0.heading, "project 1");
    assert_eq!(find(3).0.heading, OTHER_WORKSPACES);
}

// ---------- scene ----------

fn draw_options(tc: &TestCase) -> SceneOptions {
    SceneOptions {
        mode: if tc.draw(gs::booleans()) { ViewMode::Rows } else { ViewMode::Grid },
        aspect: tc.draw(gs::floats::<f64>().min_value(0.5).max_value(3.5).allow_nan(false).allow_infinity(false)),
        packing: if tc.draw(gs::booleans()) { GridPacking::Shelves } else { GridPacking::Treemap },
        keep_clusters_whole: tc.draw(gs::booleans()),
        size_limit: tc.draw(gs::floats::<f64>().min_value(1.0).max_value(3.0).allow_nan(false).allow_infinity(false)),
        readable_share: tc.draw(gs::sampled_from(vec![0.0, 0.05, 0.12, 0.3, 1.0])),
        ..SceneOptions::default()
    }
}

fn draw_slides(tc: &TestCase, layout: &LogicalLayout) -> BTreeMap<GroupKey, f64> {
    layout
        .groups
        .iter()
        .filter(|_| tc.draw(gs::booleans()))
        .map(|g| (g.key.clone(), tc.draw(gs::floats::<f64>().min_value(-200.0).max_value(50.0).allow_nan(false).allow_infinity(false))))
        .collect()
}

/// Every shown workspace has one tile, the viewport's shape, with its label
/// under it: 1 high in rows; in a grid, the same size within a group, the
/// largest 1 high, and one size everywhere on shelves. Tiles, labels and
/// headings never overlap one another; each cluster's region covers its
/// tiles; everything is inside the scene's bounds and its group's.
#[hegel::test(test_cases = 300)]
fn scenes_place_every_shown_workspace_once_without_overlaps(tc: TestCase) {
    let entries = draw_entries(&tc);
    let layout = logical_layout(&entries, None, &BTreeSet::new());
    let options = draw_options(&tc);
    let scene = scene(&layout, &options, &draw_slides(&tc, &layout));
    let ids: Vec<_> = scene.tiles.iter().map(|t| t.workspace_id).collect();
    assert_eq!(ids, shown(&layout));
    let mut boxes = vec![];
    let grid = options.mode == ViewMode::Grid;
    let tallest = scene.tiles.iter().map(|t| t.rect.height()).fold(0.0, f64::max);
    if !scene.tiles.is_empty() {
        assert!(close(tallest, 1.0), "the largest tiles are {tallest} high");
    }
    for tile in &scene.tiles {
        assert!(close(tile.rect.width(), options.aspect * tile.rect.height()), "{tile:?} isn't the viewport's shape");
        assert!(if grid { tile.rect.height() <= 1.0 + 1e-9 } else { close(tile.rect.height(), 1.0) });
        let first = scene.tiles.iter().find(|t| t.group == tile.group).unwrap();
        let shelves = options.packing == GridPacking::Shelves;
        assert!(close(tile.rect.height(), if grid && !shelves { first.rect.height() } else { tallest }));
        assert!(tile.label.y0 >= tile.rect.y1 && close(tile.label.x0, tile.rect.x0));
        let group = &scene.groups[tile.group];
        assert!(group.bounds.contains(&tile.rect) && group.bounds.contains(&tile.label) && scene.bounds.contains(&group.bounds));
        let region = &group.regions[tile.cluster];
        let is_head = layout.groups[tile.group].clusters[tile.cluster].is_group_head;
        assert_eq!(region.is_empty(), is_head);
        assert!(is_head || region.iter().any(|r| r.contains(&tile.rect)), "{tile:?} outside its region {region:?}");
        boxes.push(tile.rect);
        boxes.push(tile.label);
    }
    for group in &scene.groups {
        // A grid's heading runs across its block, a row's across the row.
        boxes.push(SceneRect { y1: group.heading.y1 - 1e-6, ..group.heading });
    }
    for (i, a) in boxes.iter().enumerate() {
        for b in &boxes[i + 1..] {
            assert!(!a.overlaps(b), "{a:?} overlaps {b:?}");
        }
    }
}

/// Sizes that can't make a scene (a zero, negative or NaN aspect) take
/// their defaults and no columns is one, so every tile and camera stays
/// finite.
#[hegel::test(test_cases = 100)]
fn unusable_sizes_take_their_defaults(tc: TestCase) {
    let entries = draw_entries(&tc);
    let layout = logical_layout(&entries, None, &BTreeSet::new());
    let bad = || tc.draw(gs::sampled_from(vec![0.0, -1.0, f64::NAN, f64::INFINITY]));
    let options = SceneOptions { aspect: bad(), row_window_tiles: bad(), size_limit: bad(), readable_share: bad(), ..draw_options(&tc) };
    let scene = scene(&layout, &options, &BTreeMap::new());
    let defaults = SceneOptions::default();
    let chosen =
        SceneOptions { mode: options.mode, packing: options.packing, keep_clusters_whole: options.keep_clusters_whole, ..defaults };
    assert_eq!(scene.options, chosen);
    let finite = |c: Camera| c.cx.is_finite() && c.cy.is_finite() && c.w.is_finite() && c.w > 0.0;
    assert!(finite(scene.overview_camera(None, None)));
    for tile in &scene.tiles {
        assert!(close(tile.rect.width(), defaults.aspect * tile.rect.height()) && tile.rect.height() > 0.0);
        assert!(finite(scene.rest_camera(tile.workspace_id).unwrap()));
        assert!(finite(scene.overview_camera(Some(tile.workspace_id), None)));
    }
}

/// Rows: a row's slide stays within its range, which lets its last tile
/// reach the overview's window and no further; a row that fits doesn't
/// slide. A cluster's tiles sit side by side, in order.
#[hegel::test(test_cases = 300)]
fn rows_slide_within_their_range(tc: TestCase) {
    let entries = draw_entries(&tc);
    let layout = logical_layout(&entries, None, &BTreeSet::new());
    let options = SceneOptions { mode: ViewMode::Rows, ..draw_options(&tc) };
    let scene = scene(&layout, &options, &draw_slides(&tc, &layout));
    for (gi, group) in scene.groups.iter().enumerate() {
        assert!(group.slide <= 0.0 && group.slide >= group.slide_min);
        let tiles: Vec<_> = scene.tiles.iter().filter(|t| t.group == gi).collect();
        let window = scene.row_window(gi).unwrap();
        if let Some(last) = tiles.last() {
            let unslid = last.rect.x1 - group.slide;
            assert!(close(group.slide_min, (window.x1 - unslid).min(0.0)));
        }
        assert!(tiles.windows(2).all(|p| p[0].rect.x1 < p[1].rect.x0 && close(p[0].rect.y0, p[1].rect.y0)));
    }
}

/// A workspace at rest fills the camera exactly. Rows: sliding a tile into
/// view puts it inside its row's window, and does nothing when it is
/// already there. The overview's camera shows the current workspace's tile
/// once its row has slid to it, and a group's camera shows the whole group.
/// Grid: the overview fits the whole scene unless the current tile would
/// be narrower than the readable share of it; then the tile is exactly that
/// share, and the camera stays within the scene.
#[hegel::test(test_cases = 300)]
fn cameras_show_what_they_are_for(tc: TestCase) {
    let entries = draw_entries(&tc);
    let layout = logical_layout(&entries, None, &BTreeSet::new());
    let options = draw_options(&tc);
    let mut slides = draw_slides(&tc, &layout);
    let first = scene(&layout, &options, &slides);
    if first.tiles.is_empty() {
        return;
    }
    let pick = first.tiles[tc.draw(gs::integers::<usize>().max_value(first.tiles.len() - 1))].workspace_id;
    let rest = first.rest_camera(pick).unwrap();
    assert!(close(rest.view(options.aspect).width(), first.tile(pick).unwrap().rect.width()));
    assert!(same(Camera::fit(&first.tile(pick).unwrap().rect, options.aspect, 1.0), rest));
    if let Some((key, slide)) = first.slide_into(pick) {
        slides.insert(key, slide);
    }
    let scene = scene(&layout, &options, &slides);
    assert_eq!(scene.slide_into(pick), None);
    let tile = scene.tile(pick).unwrap();
    if options.mode == ViewMode::Rows {
        assert!(scene.row_window(tile.group).unwrap().contains(&SceneRect { y0: tile.rect.y0, y1: tile.rect.y1, ..tile.rect }));
    }
    let overview = scene.overview_camera(Some(pick), None).view(options.aspect);
    assert!(overview.contains(&tile.rect), "the overview {overview:?} doesn't show {tile:?}");
    if options.mode == ViewMode::Grid {
        let fit = Camera::fit(&scene.bounds, options.aspect, 1.0);
        if tile.rect.width() < options.readable_share * fit.w {
            assert!(close(tile.rect.width() / overview.width(), options.readable_share));
            let inside = |lo: f64, hi: f64, v0: f64, v1: f64| v0 >= lo - 1e-9 && v1 <= hi + 1e-9 || close((lo + hi) / 2.0, (v0 + v1) / 2.0);
            let b = scene.bounds;
            assert!(
                inside(b.x0, b.x1, overview.x0, overview.x1) && inside(b.y0, b.y1, overview.y0, overview.y1),
                "{overview:?} leaves {b:?}"
            );
        } else {
            assert!(same(scene.overview_camera(Some(pick), None), fit));
        }
    }
    let group = tc.draw(gs::integers::<usize>().max_value(scene.groups.len() - 1));
    assert!(scene.group_camera(group).unwrap().view(options.aspect).contains(&scene.groups[group].bounds));
}

// ---------- grid packings ----------

fn tile_count(group: &Group) -> usize {
    group.clusters.iter().map(|c| c.tiles.len()).sum()
}

fn block_rect(block: &grid::Block, aspect: f64) -> SceneRect {
    let size = grid::Geometry { aspect };
    SceneRect {
        x0: block.x,
        y0: block.y,
        x1: block.x + size.width(block.columns) * block.t,
        y1: block.y + size.height(block.rows) * block.t,
    }
}

/// Both packings put every group's block inside the region (the viewport's
/// shape, 1 high), in sidebar order, reading across then down, without
/// overlapping; shelves give every tile one size. Each tile has its own
/// cell, in order across its block's rows. Kept whole, a cluster no wider
/// than a row is on one row.
#[hegel::test(test_cases = 300)]
fn grid_packings_fit_the_region_in_order(tc: TestCase) {
    let groups = logical_layout(&draw_entries(&tc), None, &BTreeSet::new()).groups;
    let aspect = tc.draw(gs::floats::<f64>().min_value(0.5).max_value(3.5).allow_nan(false).allow_infinity(false));
    let keep_whole = tc.draw(gs::booleans());
    let shelves = tc.draw(gs::booleans());
    let limit = tc.draw(gs::floats::<f64>().min_value(1.0).max_value(3.0).allow_nan(false).allow_infinity(false));
    let blocks = if shelves { grid::shelves(&groups, aspect, keep_whole) } else { grid::treemap(&groups, aspect, keep_whole, limit) };
    assert_eq!(blocks.len(), groups.len());
    let region = SceneRect { x0: 0.0, y0: 0.0, x1: aspect, y1: 1.0 };
    let rects: Vec<_> = blocks.iter().map(|b| block_rect(b, aspect)).collect();
    for ((block, group), rect) in blocks.iter().zip(&groups).zip(&rects) {
        assert!(block.t > 0.0 && block.t.is_finite(), "{block:?}");
        assert!(region.contains(rect), "{rect:?} is outside {region:?}");
        if shelves {
            assert!(close(block.t, blocks[0].t));
        }
        assert_eq!(block.cells.len(), tile_count(group));
        assert!(block.cells.iter().all(|&(c, r)| c < block.columns && r < block.rows));
        assert!(block.cells.windows(2).all(|p| (p[0].1, p[0].0) < (p[1].1, p[1].0)), "cells out of order: {:?}", block.cells);
        let mut cells = block.cells.iter();
        for cluster in &group.clusters {
            let rows: BTreeSet<usize> = cluster.tiles.iter().map(|_| cells.next().unwrap().1).collect();
            if keep_whole && cluster.tiles.len() <= block.columns {
                assert_eq!(rows.len(), 1, "a cluster of {} split in {} columns", cluster.tiles.len(), block.columns);
            }
        }
    }
    for (i, a) in rects.iter().enumerate() {
        for b in &rects[i + 1..] {
            assert!(!a.overlaps(b), "{a:?} overlaps {b:?}");
        }
    }
    for pair in rects.windows(2) {
        let (a, b) = (pair[0], pair[1]);
        assert!(b.x0 >= a.x1 - 1e-9 || b.y0 >= a.y1 - 1e-9, "{b:?} comes before {a:?}");
    }
}

/// The treemap's breaks give the most tile area of any breaks whose tile
/// sizes keep within the limit, or of any breaks at all when none do.
#[hegel::test(test_cases = 200)]
fn the_treemap_picks_the_best_breaks(tc: TestCase) {
    let groups = logical_layout(&draw_entries(&tc), None, &BTreeSet::new()).groups;
    let g = groups.len();
    if g == 0 {
        return;
    }
    let aspect = tc.draw(gs::floats::<f64>().min_value(0.5).max_value(3.5).allow_nan(false).allow_infinity(false));
    let keep_whole = tc.draw(gs::booleans());
    let limit = tc.draw(gs::floats::<f64>().min_value(1.0).max_value(3.0).allow_nan(false).allow_infinity(false));
    let area = |blocks: &[grid::Block]| blocks.iter().zip(&groups).map(|(b, g)| tile_count(g) as f64 * aspect * b.t * b.t).sum::<f64>();
    let within = |blocks: &[grid::Block]| {
        let ts = blocks.iter().map(|b| b.t);
        ts.clone().fold(0.0, f64::max) <= ts.fold(f64::INFINITY, f64::min) * limit * (1.0 + 1e-9)
    };
    let chosen = grid::treemap(&groups, aspect, keep_whole, limit);
    let others: Vec<_> = (0..1usize << (g - 1))
        .map(|mask| {
            let starts: Vec<usize> = std::iter::once(0).chain((1..g).filter(|k| mask & (1 << (k - 1)) != 0)).collect();
            grid::treemap_with_breaks(&groups, aspect, keep_whole, &starts)
        })
        .collect();
    let any_within = others.iter().any(|o| within(o));
    assert_eq!(within(&chosen), any_within);
    for other in others.iter().filter(|o| !any_within || within(o)) {
        assert!(area(&chosen) >= area(other) * (1.0 - 1e-9), "{} beats the chosen {}", area(other), area(&chosen));
    }
}

/// A one-tile group beside a hundred-tile one still gets tiles inside its
/// rectangle: the margin never leaves it no room.
#[test]
fn a_tiny_group_beside_a_huge_one_fits() {
    let mut entries = vec![entry("tree", vec![segment("project", entity("project", 0))], 1)];
    for v in 0..100 {
        entries.push(entry("tree", vec![segment("project", entity("project", 1)), segment("vessel", entity("vessel", v))], v + 2));
    }
    let groups = logical_layout(&entries, None, &BTreeSet::new()).groups;
    assert_eq!(groups.iter().map(tile_count).collect::<Vec<_>>(), [1, 100]);
    for aspect in [0.5, 1.6, 3.5] {
        let region = SceneRect { x0: 0.0, y0: 0.0, x1: aspect, y1: 1.0 };
        let blocks = grid::treemap(&groups, aspect, true, 2.0);
        assert_eq!(blocks.len(), 2);
        for block in &blocks {
            assert!(block.t > 0.0 && block.t.is_finite(), "{block:?}");
            assert!(region.contains(&block_rect(block, aspect)), "{block:?} is outside {region:?}");
        }
    }
}

/// A grid zoomed in on a tile too narrow to read pans both ways, as far as
/// the scene goes; rows only pan down.
#[test]
fn a_zoomed_grid_pans_both_ways() {
    let project = || segment("project", entity("project", 0));
    let entries: Vec<_> = (1..=30).map(|i| entry("tree", vec![project(), segment("convoy", entity("convoy", i))], i)).collect();
    let mut window = OverviewWindow::default();
    window.set_view_mode(ViewMode::Grid, 0.0);
    window.set_grid(GridPacking::Shelves, 0.4, 0.0);
    window.update(entries, Some(tab_workspace_id(1)), 1.6, 0.0);
    window.open(0.0);
    let bounds = window.scene().bounds;
    let start = window.target().unwrap();
    assert!(start.w < bounds.width(), "the overview zooms in on the current tile");
    window.pan(0.5, 0.5);
    let moved = window.target().unwrap();
    assert!(moved.cx > start.cx && moved.cy > start.cy, "{start:?} to {moved:?}");
    window.pan(1e6, 1e6);
    let far = window.target().unwrap().view(1.6);
    assert!(close(far.x1, bounds.x1) && close(far.y1, bounds.y1), "{far:?} isn't at the end of {bounds:?}");
    window.set_view_mode(ViewMode::Rows, 0.0);
    let rows = window.target().unwrap();
    window.pan(5.0, 0.0);
    assert_eq!(window.target().unwrap().cx, rows.cx);
}

// ---------- a window's overview ----------

fn near(a: Camera, b: Camera) -> bool {
    let tolerance = 1e-6 * a.w.max(b.w);
    (a.cx - b.cx).abs() <= tolerance && (a.cy - b.cy).abs() <= tolerance && (a.w - b.w).abs() <= tolerance
}

/// Switching workspaces flies the camera from the old workspace's tile to
/// the new one's, and without motion it cuts.
#[test]
fn switching_flies_between_tiles_unless_motion_is_off() {
    let entries = vec![
        entry("tree", vec![segment("project", entity("project", 0))], 1),
        entry("tree", vec![segment("project", entity("project", 0)), segment("convoy", entity("convoy", 0))], 2),
    ];
    for motion in [true, false] {
        let mut window = OverviewWindow::default();
        window.set_motion(motion);
        window.update(entries.clone(), Some(tab_workspace_id(1)), 1.6, 0.0);
        let first = window.scene().rest_camera(tab_workspace_id(1)).unwrap();
        assert_eq!(window.camera(0.0), Some(first));
        assert!(window.settled(0.0));
        window.update(entries.clone(), Some(tab_workspace_id(2)), 1.6, 1000.0);
        let second = window.scene().rest_camera(tab_workspace_id(2)).unwrap();
        assert_eq!(window.flying(1000.0), motion);
        assert_eq!(window.camera(1000.0), Some(if motion { first } else { second }));
        assert_eq!(window.settled(1000.0), !motion);
        assert_eq!(window.camera(2000.0), Some(second));
        assert!(window.settled(2000.0));
    }
}

/// Whatever the host and the user do, in any order and at any time:
/// - the camera is always somewhere finite once there is anything to show;
/// - flying moves (a switch, opening, going back, a group, a pick) and
///   changes of layout (workspaces coming and going, a collapse, a view
///   mode, a packing) never jump it, while motion is on;
/// - at rest, once the layout has settled, it is where the mode says, and
///   the host draws the workspace as itself exactly when it is open and
///   still;
/// - every flight ends within the longest flight's time, and every change
///   of layout within its own;
/// - panning the overview cuts, even mid-flight; changing the view mode or
///   the grid's packing flies, and cuts without motion;
/// - going back from the overview opens the current workspace, and back
///   from an open workspace does nothing.
#[hegel::test(test_cases = 300)]
fn a_windows_camera_follows_its_mode(tc: TestCase) {
    let mut entries = draw_entries(&tc);
    let mut window = OverviewWindow::default();
    let mut now = 0.0;
    let mut aspect = 1.6;
    let mut motion = true;
    let pick = |tc: &TestCase, window: &OverviewWindow| {
        let tiles = &window.scene().tiles;
        (!tiles.is_empty()).then(|| tiles[tc.draw(gs::integers::<usize>().max_value(tiles.len() - 1))].workspace_id)
    };
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(25)) {
        now += tc.draw(gs::sampled_from(&[0.0, 16.0, 100.0, 300.0, 1000.0][..]));
        let before = window.camera(now);
        let flies = match tc.draw(gs::integers::<u8>().max_value(12)) {
            0 => {
                let current = pick(&tc, &window).or_else(|| entries.first().map(|e| e.workspace_id));
                let switched = current != window.current();
                window.update(entries.clone(), current, aspect, now);
                switched
            }
            1 => {
                entries = draw_entries(&tc);
                let current = window.current().filter(|c| entries.iter().any(|e| e.workspace_id == *c));
                window.update(entries.clone(), current, aspect, now);
                true
            }
            2 => {
                window.open(now);
                true
            }
            3 => {
                let mode = window.mode().clone();
                let handled = window.back(now);
                assert_eq!(handled, mode != OverviewMode::Workspace);
                if mode == OverviewMode::Overview {
                    assert_eq!(window.mode(), &OverviewMode::Workspace);
                }
                true
            }
            4 if !window.scene().groups.is_empty() => {
                window.zoom_group(tc.draw(gs::integers::<usize>().max_value(window.scene().groups.len() - 1)), now);
                true
            }
            5 => {
                if let Some(id) = pick(&tc, &window) {
                    window.pick(id, now);
                    assert_eq!(window.current(), Some(id));
                }
                true
            }
            6 if !window.scene().groups.is_empty() => {
                let group = tc.draw(gs::integers::<usize>().max_value(window.scene().groups.len() - 1));
                window.slide(
                    group,
                    tc.draw(gs::floats::<f64>().min_value(-20.0).max_value(20.0).allow_nan(false).allow_infinity(false)),
                    now,
                );
                false
            }
            7 => {
                // An overview with nothing to show has nowhere to pan to.
                let overview = *window.mode() == OverviewMode::Overview && window.target().is_some();
                let by = || tc.draw(gs::floats::<f64>().min_value(-10.0).max_value(10.0).allow_nan(false).allow_infinity(false));
                window.pan(by(), by());
                // Panning the overview is a cut, even mid-flight.
                assert!(!overview || !window.flying(now));
                false
            }
            11 => {
                let packing = if tc.draw(gs::booleans()) { GridPacking::Shelves } else { GridPacking::Treemap };
                let changes = window.scene().options.packing != packing && window.target().is_some();
                window.set_grid(packing, tc.draw(gs::sampled_from(vec![0.0, 0.1, 0.3, f64::NAN])), now);
                assert_eq!(window.scene().options.packing, packing);
                // Without motion a new packing is a cut, even mid-flight.
                assert!(motion || !changes || !window.flying(now));
                changes
            }
            10 => {
                let mode = if tc.draw(gs::booleans()) { ViewMode::Rows } else { ViewMode::Grid };
                let changes = window.scene().options.mode != mode && window.target().is_some();
                window.set_view_mode(mode, now);
                assert_eq!(window.scene().options.mode, mode);
                // Without motion a new view mode is a cut, even mid-flight.
                assert!(motion || !changes || !window.flying(now));
                changes
            }
            8 if !window.layout().groups.is_empty() => {
                let group = tc.draw(gs::integers::<usize>().max_value(window.layout().groups.len() - 1));
                let clusters = window.layout().groups[group].clusters.len();
                window.toggle_cluster(group, tc.draw(gs::integers::<usize>().max_value(clusters - 1)), now);
                true
            }
            9 if tc.draw(gs::booleans()) => {
                window.close(now);
                assert_eq!(window.mode(), &OverviewMode::Workspace);
                true
            }
            9 => {
                aspect = tc.draw(gs::floats::<f64>().min_value(0.5).max_value(3.5).allow_nan(false).allow_infinity(false));
                window.update(entries.clone(), window.current(), aspect, now);
                false
            }
            _ => {
                motion = !motion;
                window.set_motion(motion);
                false
            }
        };
        let camera = window.camera(now);
        if let Some(camera) = camera {
            assert!(camera.cx.is_finite() && camera.cy.is_finite() && camera.w.is_finite() && camera.w > 0.0, "{camera:?}");
        }
        if let (true, true, Some(before), Some(after)) = (flies, motion, before, camera) {
            assert!(near(before, after), "a flying move jumped the camera from {before:?} to {after:?}");
        }
        if !window.flying(now) && !window.morphing(now) {
            if let (Some(camera), Some(target)) = (camera, window.target()) {
                assert!(near(camera, target), "at rest at {camera:?}, not {target:?}");
            }
        }
        let open = *window.mode() == OverviewMode::Workspace && window.current().is_some_and(|id| window.scene().tile(id).is_some());
        assert_eq!(window.settled(now), open && !window.flying(now));
        assert!(!window.flying(now + FlightTiming::default().max_ms));
        assert!(!window.morphing(now + MORPH_MS));
    }
}

/// The scene moving under the camera: a workspace arriving mid-flight moves
/// the destination tile, and the flight lands where it went; sliding the
/// open workspace's row keeps the camera on it.
#[test]
fn the_camera_keeps_up_with_a_moving_scene() {
    let project = || segment("project", entity("project", 0));
    let one = entry("tree", vec![project()], 1);
    let two = entry("tree", vec![project(), segment("convoy", entity("convoy", 1))], 2);
    let three = entry("tree", vec![project(), segment("convoy", entity("convoy", 0))], 3);
    // Enough more to overflow the row, after the destination.
    let more: Vec<_> = (4..12).map(|i| entry("tree", vec![project(), segment("convoy", entity("convoy", i))], i)).collect();
    let mut window = OverviewWindow::default();
    let before_three = [vec![one.clone(), two.clone()], more.clone()].concat();
    window.update(before_three.clone(), Some(tab_workspace_id(1)), 1.6, 0.0);
    window.update(before_three, Some(tab_workspace_id(2)), 1.6, 1000.0);
    let before = window.scene().rest_camera(tab_workspace_id(2)).unwrap();
    assert!(window.flying(1100.0));
    window.update([vec![one, three, two], more].concat(), Some(tab_workspace_id(2)), 1.6, 1100.0);
    let after = window.scene().rest_camera(tab_workspace_id(2)).unwrap();
    assert_ne!(before, after, "the new workspace comes before the destination");
    assert!(window.flying(1100.0));
    assert!(near(window.camera(5000.0).unwrap(), after));
    assert!(window.settled(5000.0));

    window.slide(0, -10.0, 5000.0);
    let slid = window.scene().rest_camera(tab_workspace_id(2)).unwrap();
    assert_ne!(slid, after, "the row slid");
    assert!(near(window.camera(5000.0).unwrap(), slid));
}

// ---------- a change of layout ----------

fn near_rect(a: &SceneRect, b: &SceneRect) -> bool {
    let tolerance = 1e-9 * (1.0 + a.x0.abs().max(a.x1.abs()).max(a.y0.abs()).max(a.y1.abs()));
    (a.x0 - b.x0).abs() <= tolerance
        && (a.y0 - b.y0).abs() <= tolerance
        && (a.x1 - b.x1).abs() <= tolerance
        && (a.y1 - b.y1).abs() <= tolerance
}

/// Where `r` sits against `within`, as shares of its size.
fn relative(r: &SceneRect, within: &SceneRect) -> [f64; 4] {
    let (w, h) = (within.width(), within.height());
    [(r.x0 - within.x0) / w, (r.y0 - within.y0) / h, (r.x1 - within.x0) / w, (r.y1 - within.y0) / h]
}

/// A change of layout drawn part way, between any two scenes of the same
/// shape (other workspaces, view modes, packings): it starts with each tile
/// where it was and each new one a point in the middle of its place, and
/// ends as the new scene. In between every tile keeps the viewport's shape,
/// its label keeps its place under it, its cluster's region still holds it,
/// each heading keeps its place in its group, and the scene's order is the
/// new one's.
#[hegel::test(test_cases = 300)]
fn a_change_of_layout_moves_each_tile_from_where_it_was(tc: TestCase) {
    let from_options = draw_options(&tc);
    let to_options = SceneOptions { aspect: from_options.aspect, ..draw_options(&tc) };
    let from = scene(&logical_layout(&draw_entries(&tc), None, &BTreeSet::new()), &from_options, &BTreeMap::new());
    let to = scene(&logical_layout(&draw_entries(&tc), None, &BTreeSet::new()), &to_options, &BTreeMap::new());

    let start = morph(&from, &to, 0.0);
    for tile in &start.tiles {
        let goes = to.tile(tile.workspace_id).unwrap();
        match from.tile(tile.workspace_id) {
            Some(was) => assert!(near_rect(&tile.rect, &was.rect), "{tile:?} didn't start where it was, {was:?}"),
            None => {
                let middle = |r: &SceneRect| ((r.x0 + r.x1) / 2.0, (r.y0 + r.y1) / 2.0);
                assert!(tile.rect.width() == 0.0 && tile.rect.height() == 0.0);
                assert!(close(middle(&tile.rect).0, middle(&goes.rect).0) && close(middle(&tile.rect).1, middle(&goes.rect).1));
            }
        }
    }
    assert!(near_rect(&start.bounds, &from.bounds));

    let end = morph(&from, &to, 1.0);
    assert_eq!(end.tiles.len(), to.tiles.len());
    for (drawn, goes) in end.tiles.iter().zip(&to.tiles) {
        assert!(near_rect(&drawn.rect, &goes.rect) && near_rect(&drawn.label, &goes.label));
    }
    for (drawn, goes) in end.groups.iter().zip(&to.groups) {
        assert!(near_rect(&drawn.bounds, &goes.bounds) && near_rect(&drawn.heading, &goes.heading));
        for (a, b) in drawn.regions.iter().flatten().zip(goes.regions.iter().flatten()) {
            assert!(near_rect(a, b));
        }
    }
    assert!(near_rect(&end.bounds, &to.bounds));

    let t = tc.draw(gs::floats::<f64>().min_value(0.01).max_value(0.99).allow_nan(false).allow_infinity(false));
    let mid = morph(&from, &to, t);
    let ids = |s: &Scene| s.tiles.iter().map(|t| (t.workspace_id, t.group, t.cluster)).collect::<Vec<_>>();
    assert_eq!(ids(&mid), ids(&to));
    assert_eq!(mid.groups.iter().map(|g| &g.key).collect::<Vec<_>>(), to.groups.iter().map(|g| &g.key).collect::<Vec<_>>());
    for (drawn, goes) in mid.groups.iter().zip(&to.groups) {
        let (a, b) = (relative(&drawn.heading, &drawn.bounds), relative(&goes.heading, &goes.bounds));
        assert!(a.iter().zip(&b).all(|(a, b)| close(*a, *b)), "{:?}'s heading moved against it", drawn.key);
    }
    for (drawn, goes) in mid.tiles.iter().zip(&to.tiles) {
        let (w, h) = (drawn.rect.width(), drawn.rect.height());
        assert!(w > 0.0 && (w - from_options.aspect * h).abs() <= 1e-9 * (1.0 + w), "{drawn:?} isn't the viewport's shape");
        let (a, b) = (relative(&drawn.label, &drawn.rect), relative(&goes.label, &goes.rect));
        assert!(a.iter().zip(&b).all(|(a, b)| close(*a, *b)), "{drawn:?}'s label moved against it");
        let region = &mid.groups[drawn.group].regions[drawn.cluster];
        let widened = |r: &SceneRect| SceneRect { x0: r.x0 - 1e-9, y0: r.y0 - 1e-9, x1: r.x1 + 1e-9, y1: r.y1 + 1e-9 };
        assert!(region.is_empty() || region.iter().any(|r| widened(r).contains(&drawn.rect)), "{drawn:?} outside its region {region:?}");
    }
}

/// An open workspace stays open while workspaces come and go: the camera
/// stays on its tile as it moves, starting where it was, and the host keeps
/// drawing it as itself. A flight taking it elsewhere leaves from where it
/// is. Without motion the layout changes at once.
#[test]
fn an_open_workspace_stays_open_while_the_layout_moves() {
    let project = || segment("project", entity("project", 0));
    let two = entry("tree", vec![project(), segment("convoy", entity("convoy", 2))], 2);
    let three = entry("tree", vec![project(), segment("convoy", entity("convoy", 3))], 3);
    let one = entry("tree", vec![project(), segment("convoy", entity("convoy", 1))], 1);
    for motion in [true, false] {
        let mut window = OverviewWindow::default();
        window.set_motion(motion);
        window.update(vec![two.clone(), three.clone()], Some(tab_workspace_id(3)), 1.6, 0.0);
        let was = window.camera(0.0).unwrap();
        assert!(window.settled(0.0));
        window.update(vec![one.clone(), two.clone(), three.clone()], Some(tab_workspace_id(3)), 1.6, 1000.0);
        let goes = window.scene().rest_camera(tab_workspace_id(3)).unwrap();
        assert!(!near(was, goes), "the new workspace comes before the open one");
        assert_eq!(window.morphing(1000.0), motion);
        assert!(window.settled(1000.0) && window.settled(1100.0));
        assert!(near(window.camera(1000.0).unwrap(), if motion { was } else { goes }));
        let drawn = window.scene_at(1100.0).rest_camera(tab_workspace_id(3)).unwrap();
        assert!(near(window.camera(1100.0).unwrap(), drawn));
        assert_eq!(near(drawn, goes), !motion);
        assert!(!window.morphing(1000.0 + MORPH_MS));
        assert!(near(window.camera(1000.0 + MORPH_MS).unwrap(), goes));

        // Mid-change, opening the overview flies from where the camera is.
        window.update(vec![one.clone(), three.clone()], Some(tab_workspace_id(3)), 1.6, 2000.0);
        let shown = window.camera(2100.0).unwrap();
        window.open(2100.0);
        assert_eq!(window.flying(2100.0), motion);
        assert_eq!(near(window.camera(2100.0).unwrap(), shown), motion);
    }
}

/// Changing the view mode moves each tile to its new place while the
/// camera flies to the overview of the new layout; without motion both cut.
#[test]
fn a_new_view_mode_moves_the_tiles_and_flies() {
    let project = |p: u64| segment("project", entity("project", p));
    let entries: Vec<_> = (1..8u64).map(|i| entry("tree", vec![project(i % 3), segment("convoy", entity("convoy", i))], i)).collect();
    for motion in [true, false] {
        let mut window = OverviewWindow::default();
        window.set_motion(motion);
        window.update(entries.clone(), Some(tab_workspace_id(1)), 1.6, 0.0);
        window.open(0.0);
        let rows = window.camera(5000.0).unwrap();
        let before = window.scene().clone();
        window.set_view_mode(ViewMode::Grid, 5000.0);
        assert_eq!(window.morphing(5000.0), motion);
        assert_eq!(window.flying(5000.0), motion);
        assert!(near(window.camera(5000.0).unwrap(), if motion { rows } else { window.target().unwrap() }));
        let tile = |s: &Scene| s.tile(tab_workspace_id(4)).unwrap().rect;
        let drawn = tile(&window.scene_at(5100.0));
        if motion {
            assert!(!near_rect(&drawn, &tile(&before)) && !near_rect(&drawn, &tile(window.scene())), "{drawn:?} isn't on its way");
        } else {
            assert!(near_rect(&drawn, &tile(window.scene())));
        }
        assert!(near(window.camera(20000.0).unwrap(), window.target().unwrap()));
    }
}
