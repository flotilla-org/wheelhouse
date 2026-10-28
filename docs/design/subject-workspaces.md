# Subject workspaces: vessels, convoys and projects

Status: design, grilled with Robert on 2026-10-21 and 2026-10-22 for #90 and
the mission #401. It builds on the View Spec and Slot decisions (#297), the
Workspace Overlay (#298), workspace controls (#122) and
[suggested-layouts.md](../core/sidebar-design/suggested-layouts.md)
in the core. The first stage is below; later stages are recorded so the first
doesn't block them.

A rough mockup of the tool strip and the layouts is in
[subject-workspaces-prototype.html](subject-workspaces-prototype.html).

## Where we are

Opening a project gives a scoped Flotilla TUI. Opening a vessel, or a convoy of
one vessel, attaches a terminal to its crew. Both depend on the subject's
`action.primary.recipe`: without it the subject isn't openable, and the
sidebar says "no opening recipe". Flotilla withdraws that recipe whenever it
re-checks whether a session can be attached, so vessels flip between openable
and not (drydock-vessel-agent-51 did this hundreds of times in one capture).

The core already reads Suggested Layouts and plans slots
(`suggested_layout.rs`, `slots.rs`), and Wheelhouse stores a slot per View.
Nothing publishes a layout of more than one slot yet (flotilla#3012).

## The model

### A subject's workspace always opens

Opening never depends on whether everything in the workspace can connect. The
layout always applies; parts that can't connect yet are shown as unavailable
and connect when they can. The opening recipe stops being a gate and becomes
one Slot's content.

### Slot Host

Every Slot's View is drawn inside a **Slot Host**, which stays for the Slot's
whole life:

- When the Slot is Current, the host draws the real Renderer with no chrome of
  its own.
- Otherwise it draws a placeholder for that kind of content. A Renderer may
  register a placeholder hook in `WH_RENDERERS`; the terminal's says, for
  example, "crew on feta: can't attach yet (checking the session)" or "session
  ended", with the actions that apply (retry, start a new crew). Kinds without
  a hook get the generic placeholder that exists today.
- An Unavailable Slot fills in on its own when its content resolves. Only a
  Failed Slot waits for the user, as #297 decided.
- Losing resolution while a live instance is showing doesn't tear it down. The
  live terminal stays, with a thin "disconnected" or "unverified" band from the
  host. Only a change of content triggers a rebind.
- Commands sent to a Slot that hasn't connected yet are held and replayed when
  it does, as the file view's `pending` view does today
  (`native/src/shell/shell_core.c`).

This is why the host wraps rather than replaces itself, as `pending` does: a
file loads once, but a Slot can lose its connection and regain it many times.
Views relative to a vessel or convoy (files, diffs) get the same behaviour by
supplying a placeholder that says something about their own content.

### One Cleat client per session

Wheelhouse is one Cleat client for a session, however many Views show it. The
grid takes the size of the View last interacted with, and the other Views draw
the same local screen state, letterboxed. They are not extra Cleat watchers.

Today each View probably makes its own attachment: a vessel terminal's recipe is
a `flotilla attach --host … <session>` command, so each View runs its own ssh
connection and Cleat attach. Doing this properly depends on remote Cleat
connections (#104).

### Layouts

A **Workspace Layout** is a named, task-focused arrangement of a Workspace's
Slots. A subject can have several, and the tool strip switches between them.

- A Workspace has **one set of Slots**; a layout only arranges them. Switching
  layouts moves the existing Views, so live terminals, scroll positions and
  Cleat attachments carry over.
- A layout may leave some Slots out. They aren't destroyed: they wait in the
  tool strip's views section and reappear when a layout includes them.
- The Workspace Overlay keeps Slot edits once and arrangement edits per layout:
  rearranging one layout leaves the others alone.
- Which layout a workspace starts with is picked by simple rules for now:
  subject kind first, then hints such as `layout.hint` on a convoy.

### Who defines what

- **Flotilla publishes what exists:** Slots and their content (agents and their
  terminals, vessels, files, promises, artifacts, linked issues and PRs), plus
  hints. Its internal presentation manager probably goes away, apart from
  exposing the PM connection's change stream.
- **The core holds the layouts** as templates, like the sidebar templates,
  keyed by subject kind and hint. A template arranges Slots by their kind and
  role ("agents as tabs on the left, files on the right"), and the selection
  rules live with the templates. Every frontend shares them, and changing a
  layout needs no Flotilla release.
- **The arrangement hint** in suggested-layouts.md becomes an optional fallback
  for when no template matches.
- **Wheelhouse** draws the Slots, their hosts and the tool strip.

### Membership changes

Ended and removed are different:

- **A new agent or vessel** gets a Slot placed by the current layout's rule,
  for example appended to the agents' tabs. It doesn't take focus; the strip
  marks it as new for a moment.
- **An ended agent stays a Slot.** Flotilla keeps listing it as ended, and its
  host shows the gone placeholder. It goes when the user closes it or when the
  convoy is cleaned up.
- **A removed Slot**, one Flotilla stops listing because the agent was
  reassigned or deleted, follows #298: untouched Slots go away; overridden ones
  are kept and flagged.
- A layout can hide ended agents ("live only") without removing them.

## What each workspace holds

These are starting layouts. Each subject can have more.

**Vessel.** The agent view on the left and the vessel's files on the right. The
agent view will become a richer view of its own that, most of the time, uses a
terminal; until it exists, the slot holds the crew's terminal. Several agents in
one vessel go in tabs, which people can drag apart. A shell in the vessel is
offered from the strip, not in the baseline.

**Convoy.** Convoys vary too much for one baseline, so they are where layouts
matter first. The starting layout puts a tab per vessel's agent on the left,
ordered by the convoy's plan, and an overview of the vessels (status, PRs,
progress) on the right, built from the shared table and markdown components.
Files stay per vessel; the strip offers them for the selected vessel. The
vessel workspaces remain, reached through the strip.

**Project.** The scoped Flotilla TUI as the main slot, with files and a terminal
in the project's local checkout beside it. The standing convoys' agents
(today, governors) go in tabs on the left once the agent view exists. Linked
items (the repo, open PRs and issues) open from the strip. The TUI later gives
way to native table views over binnacle; both are just Slots, so swapping them
changes nothing structural.

## Tool strip

The tool strip is #122's workspace controls: a thin strip at the edge of the
Workspace Region. From the start, sections are separated by clear dividers:

1. **Subject**: its icon and status. Hovering shows its hover card; its own
   commands are in its palette (below).
2. **Layouts**: the layouts for this subject, the current one marked.
3. **Views**: the subject's view-opening controls, and the Slots the current
   layout doesn't place by itself. They stay in the strip once open, drawn
   as toggled on (the look needs refining). A click on a closed view places it
   in its default position; a drag places it where it is dropped. A click on
   an open view makes it visible, for example by selecting its tab; later it
   can also highlight it. Some views can be open more than once, such as a
   terminal: for those, a drag or a double-click always opens another one.
4. **Related workspaces**: parent and children (project, convoys, vessels,
   the governor), each with its status. Hovering shows the workspace's hover
   card; a click opens or switches to it.
5. **Linked items**: the PR, issues, artifacts and, later, promises. A click
   runs the item's default action, such as opening the PR in a Luchs Slot.
   Hovering or right-clicking opens a small palette of its other actions.

Each section has a fixed area, which you resize by dragging the divider after
it, so items don't jump around as views open and close or when you switch
workspaces. A section stays in place even when it is empty. A section with more
items than fit scrolls; later it could collapse its overflow into a "more"
item.

The first cut has the subject, related workspaces, views and linked items. The
layouts area fills in once a subject has more than one layout.

### The first cut

Settled for #122 on 2026-10-23 (`native/src/uishell/uishell_tool_strip.c`):

- **Where:** a column of icons, about 3.5 ems wide, between the sidebar and the
  Workspace Region. It shows on every workspace, with or without a subject,
  and stays while the sidebar is collapsed. *Toggle Tool Strip* (Window menu
  and palette) hides it in every window: it is the user setting
  `show_tool_strip`.
- **Section sizes** are heights in items, fractions allowed, and global, in the
  user settings `tool_strip_subject_size`, `_layouts_size`, `_views_size` and
  `_related_size` (defaults 1, 2, 6 and 4). The linked items take the rest.
  Dragging a divider resizes the section above it smoothly, following the
  pointer; double-clicking it restores the default. A section with more items
  than fit fades out its last visible item and scrolls by whole items.
- **Icons only:** each item has a tooltip, or its hover card, which opens beside
  the strip, level with the item.
- **Subject:** drawn as its sidebar chip, by what its card presents (#447). A
  click reveals its row in the sidebar, opening the sidebar if it's collapsed.
- **Layouts:** a layout's `glyph`, else its name's first letter, with the
  current one marked. A click shows that layout. The right-click menu has
  *Show this layout*, *Show the default layout* and, for a rearranged current
  layout, *Reset layout*. Later a layout's icon could default to a small
  picture of its shape (its slots and their proportions).
- **Views:** every slot of the workspace, in slot order, with an icon by role.
  A placed View is drawn normally, and marked when its tab is selected; a click
  selects its tab. An unplaced one is drawn weak, and a click shows it in the
  panel its layout's matchers would give it (`WheelhouseNote.panel`). Closing
  it from a panel it was alone in closes that panel too, collapsing the
  layout (#445). Dragging
  a View into place waits for creation drags into the Workspace Region, which
  the docking checker refuses today. Repeatable views will be **view
  factories**: a factory opens another View each time.
- **Related workspaces and linked items** are what the subject kind's `strip`
  lists (the core's `docs/core/sidebar-design/tool-strip.md`):
  - vessel: its project, then its convoy;
  - convoy: its project, then its vessels in plan order;
  - project: its governors, then its convoys that haven't landed;
  - no siblings.

  The linked items are a convoy's PRs, then its issues. A vessel links none
  until its convoy's PRs and issues reach it: they aren't its own facts, and a
  strip loop can't follow the convoy to them. A project links none yet. Each
  item is drawn as its entity's sidebar chip, whether or not the sidebar shows
  one: the chip's icon, or a workspace's mark (`…`, `×`) while it is opening or
  ended, in the chip's colour, so a subject is coloured by its status (#447).
  A project's item is its icon, not its chip's overview glyph, since it opens
  the project. A workspace not open yet is drawn weak. A click runs the item's primary
  action, which opens a workspace or switches to it, or opens a URL. Its hover
  card has the other actions. Whether ancestors and children need telling
  apart (a vessel's project and convoy are above it, a convoy's vessels below)
  is for trying out.
- The tab strip's `+` keeps *Show …* until the strip has proved itself.

The strip is designed to be orientation independent, so it can later go on any
edge of its section of the Controlled Split. Once it works well, the tab strip's
`+` retires; the tab strip itself is due some work anyway (easy to hide,
especially with one View).

## Switching workspaces

Switching straight from one workspace to another is disorienting, so a switch
gets a short transition (#450), built on the overview's existing workspace
zoom:

- **To a child** (project to convoy, convoy to vessel): zoom into where the
  child sits, with the current workspace fading underneath.
- **To a parent**: zoom out, the reverse.
- **To a sibling or anything further away**: a quick zoom out of the current
  workspace, then into the new one.

Transitions are optional, and off under reduced motion.

This ties into the overview (#190). Today it lays workspaces out in creation
order. By default it should follow the same hierarchy as the sidebar, with
each workspace showing a sensible amount of information about its subject:
the card profiles in #270. Once the overview has that layout, the zoom out
and in can pass through each workspace's real position in it.

## Cases to work out

Plenty of fiddly cases will turn up in use. Known so far:

- **A Luchs view navigated away from its linked item** (a PR page, say) stops
  being that item's view: it becomes unlinked, a Detached Slot, and the linked
  item's next click opens a fresh view rather than showing the one now
  elsewhere. The fresh view binds the item's Slot again; the unlinked one stays
  open as its own Detached Slot until closed.

## Developing against the dummy fleet

These views are hard to use before their affordances exist, and real convoys
rarely go through every motion. Build them against Flotilla's dummy fleet
(`flotilla-dummy-fleet`, `[dummy_fleet]` in `daemon.toml`), which already has
scripted crews, a dummy VCS and forge and a workflow catalog. It needs scenario
scripts for the convoy shapes and their motions: several vessels, a lead agent
using other vessels, agents ending, joining and being reassigned, and attach
capability coming and going, so the Slot Host's placeholders are exercised on
purpose. A Wheelhouse profile pointed at that daemon is the fixture for both
development and diagnostics.

## The wider model

Convoys are where most of this is heading, and they are barely developed yet:

- A convoy is a workflow between agents: uniquely named seats filling roles
  (experimenter-3 in the experimenter role). The workflow can be fixed,
  scripted or ad hoc.
- Roles have promises to keep ("a PR doing this slice", "a screenshot of the
  feature", "a live demo"). They can make new ones, transfer them and keep
  them.
- Agents exchange messages through inboxes, to any permitted agent, not only
  within a vessel or convoy. Some messages expect replies.
- Vessels are sandboxes or machines the agents work in. The agent loop runs in
  the vessel today, but needn't: an agent may later use one or more vessels,
  and allocate or destroy them.

Today is nearly always the degenerate case: one agent, in its vessel, keeping
one or a few promises. So the current vessel orientation is probably temporary,
and the design doesn't presuppose a workflow.

That splits what there is to show in two:

- **Agent-scoped:** promises, inbox and message history. These can appear
  anywhere the agent does.
- **Vessel-scoped:** files, changes, and the machine (size, provider, basic
  performance).

In the simple case the two combine into one coherent view; in general they
don't. That suggests a **composed agent view** whose subviews are embedded but
can still be dragged out, rather than relying only on RAD's tabs and panels.
It is for prototyping, not yet decided.

## Later stages

- **A waiting layout**, applied before the real layout is known, with a waiting
  indicator, that transitions smoothly into the real one once connected.
- **A gone layout** for a subject that has ended: "this is gone", and
  eventually its history and what it did.
- **Saving layout preferences**, possibly as named layouts, perhaps with some
  intelligence to infer what a layout means ("all my agents in a grid").
- **Rich project views**, web first through Luchs (prototypes in
  `~/dev/codebase-insights`), possibly native everywhere in the end: running
  convoys, mission dependency DAGs, queues of work to dispatch or grill, the
  repos' issues and PRs.
- **Other main workspaces**, not yet worked out:
  - *fleet*: all convoys, hosts and sandboxes, their health, spending, and
    preferences such as LLM and sandbox providers;
  - *welcome / overview*: onboarding a project, eventually agent-guided.

## Order

1. **Slot Host** with per-kind placeholders, and the vessel's opening action no
   longer gated on its recipe. This is also the fix for "no opening recipe".
2. **Dummy-fleet scenarios** for the convoy shapes above (Flotilla).
3. **Layout templates and selection rules** in the core, with Flotilla
   publishing content and hints (flotilla#3012 reworked).
4. **Tool strip, first cut** (#122 reworked), then layout switching.
5. **One Cleat client per session**, with remote Cleat (#104).
6. **Switching transitions** (#450), on the overview's current layout, then
   through each workspace's real position once the overview follows the
   sidebar's hierarchy (#190).

The lifecycle cleanup in #391 (#229, #389, #390, #69) continues alongside; #69
overlaps the Slot Host's placeholders.
