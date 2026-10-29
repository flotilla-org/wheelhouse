# The overview scene and switching workspaces

Status: design, worked through with Robert on 2026-10-29 for #450, with a
prototype. It answers part of the overview brainstorm (#190) and replaces the
"Switching workspaces" sketch in
[subject-workspaces.md](subject-workspaces.md). The prototype is
[overview-camera-prototype.html](overview-camera-prototype.html).

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

- A **group** is a project. Its heading is the project's name.
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

## Open questions

- How the camera reads the preview budget mid-flight: which tiles render, and
  at what resolution, while the camera passes over many of them.
- Whether workspaces not opened in this window (a subject's workspace that has
  never been materialized) appear in the scene, and how.
- How the overview and the sidebar relate when both show the same hierarchy
  (#190's third direction).
