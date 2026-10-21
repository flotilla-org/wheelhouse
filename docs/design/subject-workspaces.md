# Subject workspaces: vessels, convoys and projects

Status: design, grilled with Robert on 2026-10-21 and 2026-10-22 for #90 and
the mission #401. It builds on the View Spec and Slot decisions (#297), the
Workspace Overlay (#298), workspace controls (#122) and
[suggested-layouts.md](https://github.com/flotilla-org/andamento/blob/main/docs/sidebar-design/suggested-layouts.md)
in Andamento. The first stage is below; later stages are recorded so the first
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

Andamento already reads Suggested Layouts and plans slots
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
  (`src/shell/shell_core.c`).

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
  tool strip's views-to-add section and reappear when a layout includes them.
- The Workspace Overlay keeps Slot edits once and arrangement edits per layout:
  rearranging one layout leaves the others alone.
- Which layout a workspace starts with is picked by simple rules for now:
  subject kind first, then hints such as `layout.hint` on a convoy.

### Who defines what

- **Flotilla publishes what exists:** Slots and their content (agents and their
  terminals, vessels, files, promises, artifacts, linked issues and PRs), plus
  hints. Its internal presentation manager probably goes away, apart from
  exposing the PM connection's change stream.
- **Andamento holds the layouts** as templates, like the sidebar templates,
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
3. **Views to add**: the subject's view-opening controls and the Slots the
   current layout leaves out. A click places one in its default position; a
   drag places it where it is dropped.
4. **Related workspaces**: parent and children (project, convoys, vessels,
   the governor), each with its status. A click opens or switches to it.
5. **Linked items**: the PR, issues, artifacts and, later, promises. A click
   runs the item's default action, such as opening the PR in a Luchs Slot.
   Hovering or right-clicking opens a small palette of its other actions.

The first cut has the subject, related workspaces, views to add and linked
items. Layouts appear once a subject has more than one.

The strip is designed to be orientation independent, so it can later go on any
edge of its section of the Controlled Split. Once it works well, the tab strip's
`+` retires; the tab strip itself is due some work anyway (easy to hide,
especially with one View).

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
3. **Layout templates and selection rules** in Andamento, with Flotilla
   publishing content and hints (flotilla#3012 reworked).
4. **Tool strip, first cut** (#122 reworked), then layout switching.
5. **One Cleat client per session**, with remote Cleat (#104).

The lifecycle cleanup in #391 (#229, #389, #390, #69) continues alongside; #69
overlaps the Slot Host's placeholders.
