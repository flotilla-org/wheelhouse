# The overview scene and switching workspaces

Status: design, worked through with Robert on 2026-10-29 for #450, with a
prototype. It answers part of the overview brainstorm (#190) and replaces the
"Switching workspaces" sketch in
[subject-workspaces.md](subject-workspaces.md). The prototype is
[overview-camera-prototype.html](overview-camera-prototype.html).

Steps 1 to 3 of [Building it](#building-it) are built: the core's `overview`
module (with `OverviewWindow`, a window's overview, behind ABI 16's
`wheelhouse_overview_*`), and `native/src/uishell/uishell_overview.c`, which
draws the region through the core's camera. Clicking a group's heading already
fits the group. "Where we are" below describes the overview before them.

## Where we are

The overview is a grid of workspace previews in creation order. Each is titled
by its vessel or role name, so most read `work`. It opens and closes with one
zoom: the selected workspace's composite moves between the full region and its
tile while the others fade (`shell_core.c`, `workspace_zoom_t`). A carousel
variant draws the same previews as 3D cards through a perspective camera.
Switching workspaces from the sidebar, the tool strip or the palette is a cut.

## The model

The overview is one **scene** that every workspace sits in, and the workspace
region is a **camera** on it. A workspace is open when the camera fits its
tile exactly; at that zoom the tile is the workspace, live. Opening the
overview, picking a tile, zooming to a group and switching workspaces are all
the camera moving through the same scene.

Three steps make the picture, each a function of the one before:

1. **Logical layout:** the item hierarchy plus context gives groups and
   workspaces, each with labels in named roles.
   - The hierarchy is the sidebar's placements: projects, then convoys, then
     vessels. Later it can follow the sidebar's visual layout too, for example
     sections pulled out into their own panels.
   - The context is which workspaces exist, which is current, the view mode
     and, later, filters and search.
   - Label roles are things like group heading, workspace title and subtitle.
     The host maps each role to a text style.
2. **Scene layout:** the logical layout, the view mode and the viewport give
   each group and workspace a place in the scene.
3. **Transition:** the scene, the workspace leaving and the workspace entering
   give a camera path over time.

The whole picture is the transition applied to the scene layout of the logical
layout. Each step can grow on its own: a new view mode is a new scene layout,
and stranger transitions are a new third step, without unpicking the others.
All three are pure functions of data, so they belong in the Rust core with its
tests; the native host draws the scene through the camera it is given.

### Groups, clusters and titles

- A **group** is a project, or a local group someone made in the sidebar. Its
  heading is the project's or group's name. The local sections that hold
  local groups only arrange the sidebar, so they aren't a level here.
- A convoy's workspace and its vessels' workspaces sit together as a
  **cluster** in the group, convoy first, then vessels in sidebar order.
- A workspace's **title** says what distinguishes it within its group, such as
  the convoy's name or the vessel's role, never `work`. Its subtitle gives its
  kind and the status mark its sidebar chip shows.
- A convoy with several vessels can be **collapsed** to its own workspace. Its
  label then lists its steps (`coder ✓ → reviewer … → shepherd`).

Later, the collapsed form should show the convoy's workflow between its agents
or vessels properly, and the same hierarchical view, cut down, can be a View
inside a convoy's own workspace. The floating cards' profiles (#270) can add
detail to tiles once they exist; until then they would be too much.

### View modes

Both of these read well in the prototype, so the scene layout takes the view
mode as an input rather than picking one:

- **Rows:** groups stacked vertically, each a horizontal strip that slides
  sideways when it overflows (drag, its end buttons, or the wheel). A
  cluster's tiles are outlined together.
- **Grid:** groups as blocks, each a grid of tiles. A cluster's cells are
  shaded together, so it reads as one rectilinear region.

The carousel is a third mode for later. The camera is 2D, a rectangle in the
scene, for now; a 3D camera is for whichever mode needs one.

The overview gets its own controls later: switching view mode first, then
filters and search.

## Transitions

A transition is a camera flight from where the camera is to where it should
be, along the zoom-and-pan path of van Wijk and Nuij ("Smooth and efficient
zooming and panning", 2003; the same as `d3.interpolateZoom`). The path pulls
back just far enough to show both ends, so a neighbour is a shallow move and a
workspace in another group a wider one. Child, parent and sibling need no cases
of their own; how a switch looks follows from where the scene layout puts
things.

Settled with the prototype:

- **Easing and length:** ease out. The duration is the path's length times
  about 280ms, kept between about 240ms and 580ms. The pull-back parameter
  (rho) stays near 1.4.
- **Interruptible:** a switch during a flight flies from where the camera is
  now, half as long. Nothing queues, and holding a cycle key doesn't chain
  flights.
- **Input** goes to the entering workspace as soon as the switch is made, also
  mid-flight.
- **Group zoom:** clicking a group's heading flies the camera to fit that
  group. `Esc` goes back out to the whole overview, then to the workspace.
- **Opening the overview** flies out to fit the visible part of the scene, with
  the current workspace's group in view. In rows, a row slides so its selected
  tile is visible rather than the camera flying off its edge.
- **Off:** a setting turns transitions off, and so does the system's reduce
  motion option. A switch is then a cut.

### Changes of layout

Every tile is the same workspace before and after a change of layout, so a
change is drawn as movement: each tile moves from where it was drawn to its
new place, over about 350ms, eased out. Its label and its cluster's shading
move with it, and a heading with its group. A workspace new to the scene
grows from the middle of its place; one that has gone is simply gone. This
is one mechanism in core for a new view mode or packing, workspaces coming
and going, and a convoy collapsing.

- **A new view mode or packing** also flies the camera to the same place in
  the new layout, since the two frame the overview differently.
- **Otherwise the camera stays on what it shows** as that moves. An open
  workspace stays open and is drawn as itself throughout, so workspaces
  arriving elsewhere never interrupt it. If the camera was off what it shows
  when a change began (an earlier change still under way), it eases onto it.
- **A move during a change** (a switch, opening the overview, a group) flies
  from where the camera is drawn, and the tiles carry on to their places.
- **Off:** without motion, a change of layout is a cut, as a switch is.

Mid-flight, the tiles are previews at their own resolution, so they may look
soft until the camera lands. The full-resolution surface takes over when it
does, as the overview's zoom does today.

## Building it

In order, each step usable on its own:

1. **Core scene model:** the logical layout from the sidebar's placements, the
   rows scene layout and the camera path, with titles by distinction. Property
   tests for the path (it ends where it should, its pull-back covers both ends,
   an interrupted path starts where the camera was) and for the layouts (every
   open workspace has one tile, clusters are contiguous, nothing overlaps).
2. **Native overview from the scene:** the overview draws the scene through the
   core's camera in place of today's grid, and opening, closing and picking a
   tile become camera flights.
3. **Switching:** every switch, from the sidebar, the tool strip, the palette
   or a cycle key, is a flight. Add the setting and the reduce-motion check.
4. **Grid view mode and group zoom**, with the overview's first control.

Built so far (#523, #524, #525, #528): steps 1 to 4. Beyond the plan: only the
workspaces the camera sees are built; rows and the overview can be dragged,
glide when let go of, and stretch past their ends and spring back (in core
since #527, [below](#dragging-and-zooming-by-hand)); the controls are a
Rows/Grid switch, the user's `overview_view_mode` setting, and, while a
group is fitted, a back button. A click outside a fitted group also backs
out.

Since built: the grid's layout rule, below (#550), and animating a change of
layout, as in [Changes of layout](#changes-of-layout) (#555): core keeps the
scene as it was drawn when a change began and draws the way between it and
the new one; the host draws whatever the frame gives it until it stops
moving.

## Dragging and zooming by hand

Worked through with Robert on 2026-11-01 (#527).

- **A scroller in core**, for one axis and knowing no units: a place and the
  range it keeps to, grabbed, dragged and let go of. Let go while moving it
  glides on, its speed falling as e^(-t/325ms); past an end it stretches,
  less the further it goes and never the region's whole length, and springs
  back critically damped. A glide into an end carries past it the same way.
  All of it is solved exactly for any frame time, so how often the host
  draws changes only how smooth it looks. Its units are the caller's, as
  #581's scroll bars are.
- **Core owns the overview's drags.** The host says what a press is on (a
  row, or between rows), how far it has moved as shares of the region, and
  when it ends; it decides when a press has moved far enough to be a drag.
  Core picks what the drag moves, judging its direction as drawn: sideways a
  row too long for the region, else a grid wider than it; up or down an
  overview taller than it. The frame comes with the stretch drawn in.
- **Catching:** a press during a glide or a spring-back holds it where it is
  drawn, and isn't a click.
- **Zooming by hand**, a grid's: pinch (macOS magnify events, passed through
  the window layer), Cmd+scroll (Ctrl+scroll on Linux and Windows, where a
  touchpad's pinch arrives as one), and a Fit all toggle beside Rows/Grid.
  From everything fitting to the narrowest tile filling the region; a pinch
  or Cmd+scroll zooms about the pointer, as a cut, and Fit all flies out to
  the whole grid, or back to where the grid starts. Zoom and pan go back to
  the default view when the overview reopens, the view mode changes or the
  readable width changes, and are kept across resizes and changes of
  layout, within the scene. Rows don't zoom: a row's slide is how they move
  sideways, and a camera narrower than the row window would need a second
  way.

Left for later: touch, and which device owns a gesture when there are
several; trackpad scroll phases, and with them a stretch from the wheel (a
stretch can't tell when to spring back without them); which other views pan
when grabbed; and #581's scroll bars on a zoomed grid.

## The grid's layout rule

Worked through with Robert on 2026-10-31, with
[overview-grid-prototype.html](overview-grid-prototype.html). Until now every
group was a block three tiles wide in two block columns, whatever the
region's shape or the group's size.

- **What the grid is for:** every workspace at once, laid out in the region's
  shape, like a disk-usage treemap. Zooming is the point: each level is a
  rectangle the camera can fit. Rows stay the mode for scanning one group at
  a time.
- **Levels:** the layout walks the hierarchy, and each level has a policy.
  It either *partitions* (its children get rectangles of their own) or
  *flows* (its leaves go in one even grid, child nodes as contiguous runs).
  For now the scene partitions into groups and each group flows its tiles.
- **Packing**, the user's `overview_grid_packing` setting:
  - **Shelves**, the default: one tile size everywhere. Each group's block
    takes the columns that make it closest to the region's shape, blocks go
    onto shelves in sidebar order, and the tile size is the largest at which
    the shelves fit the region.
  - **Treemap:** an ordered treemap. Groups go into horizontal strips in
    sidebar order, each group's rectangle sized by its tile count, and its
    grid fitted inside the rectangle, so tile sizes differ between groups.
    The strip breaks are the ones that give the most tile area. Tile sizes
    may differ by at most a limit (1.5×), so a one-workspace group doesn't
    shrink to nothing.
- **Clusters** are kept whole on a row when they fit in one: a convoy that
  doesn't fit in what's left of a row starts the next. A cluster wider than a
  row starts a row and wraps. Always flowing is the other policy.
- **The layout never grows to fit.** It's always the region's shape. The
  default camera fits it all when the current workspace's tile is at least a
  readable width (a setting, about 160px); otherwise it zooms in until that
  tile is, centred on it and kept within the scene. The overview then pans in
  both directions. Zooming out to the whole grid by hand comes with
  [zooming by hand](#dragging-and-zooming-by-hand).
- **Sidebar order** is kept in both packings, so adding or removing a
  workspace moves little.

The prototype also has an ordered strip treemap with one tile size per strip,
a choice for later.

## Later

The overview is a basis for more than workspace previews. Robert's directions
(2026-10-31):

- **What each tile shows.** Not always the live preview: the same facts the
  cards show (#270's profiles applied to tiles), stats such as a convoy's cost
  over time, and eventually a summary, from the decision model, of what
  matters in it.
- **How to group.** By project, as now, or by status (active, waiting,
  landed) and other facts: the grouping-as-a-query question below.
- **What data it shows.** The same scene, camera and controls over other
  data, such as the forge's issues and PRs (see the Presentations direction
  in #505). Either the overview generalises, or other specialised views share
  its building blocks.

- **Grouping within a project.** Convoys by mission or lane, for example. It
  depends on the data the overview is fed, so what it uses for presentation
  should stay generic.
- **Vessels under their convoy.** A cluster could partition rather than flow:
  the convoy's tile takes the lead cell and its vessels tile smaller beside or
  under it, recursively, so a convoy takes one cell's worth of space until the
  camera zooms down into it.
- **Overview profiles.** The choices here compose: the arrangement (rows,
  grid, later the carousel), the packing, the cluster policy, what tiles show
  and how to group. A profile names a set of them, as card profiles do (#270),
  and you move between profiles quickly. Some combinations need adapting: a
  row that slides inside a treemap cell would fade at its edges.
- **Facts on tiles.** With the cards' information per subject: where a
  workspace runs, which model, as badges, or as another grouping.

The controls grow with these: what tiles show, how to group, then filters and
search. It comes together over several rounds as the data matures.

## Open questions

- How the camera reads the preview budget mid-flight: which tiles render, and
  at what resolution, while the camera passes over many of them.
- Whether workspaces not opened in this window (a subject's workspace that has
  never been materialized) appear in the scene, and how.
- How the overview and the sidebar relate when both show the same hierarchy
  (#190's third direction).
- Projects, local groups and sections need thinking through together.
  Projects and local groups are most alike, and in the end the hierarchy is
  probably a generic tree. Grouping that is a query over workspaces and their
  facts has to be reconciled with groups people drag things between.
