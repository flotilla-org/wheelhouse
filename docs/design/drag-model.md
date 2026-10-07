# One drag model: move, reorder or ghost

Design for [#239](https://github.com/flotilla-org/wheelhouse/issues/239), settled with the operator on 2026-10-07. It joins the drag questions from #210 (header chrome), #211 (one panel model), #89 (pinned preview cards) and #190 (overview arrangement). It extends [Sidebar rows, hover cards and docking](sidebar-cards-and-docking.md) and keeps its two rules: *what may dock where is declared, never decided in gesture code*, and *KDL owns what, layout owns where*.

## The rule

A drop does one of three things. **The target decides which; the gesture never does.**

- **Reorder.** The thing stays where it lives and changes position among its siblings.
- **Move.** The thing changes where it lives. Only things Wheelhouse owns can move: Views, sections, local groups and local workspaces.
- **Ghost.** A linked reference appears at the target, and the original stays where it lives. A ghost is its own object pointing at the source, so one source can have any number of ghosts. Ghosts appear only on reference surfaces: local groups, and later the overview.

Data things never leave their home. A convoy, vessel or other Andamento entity stays where Andamento places it. Inside its home it can only be reordered; anywhere else it can only be ghosted.

## Levels

- **Sections** are dockable containers (Views). The docking system moves, closes, tabs and splits them (#211). A section displays a list of groups.
- **Groups** are cards inside a section:
  - *Project groups* come from Andamento. They can be reordered within their section but not moved out of it.
  - *Local groups* are owned by Wheelhouse. They can be renamed, moved between sections, or dragged to a dock edge to become their own section.
  - The *default Workspaces group* is the local group that exists from the start. It is unbordered and uncoloured but laid out like a card.
  - *Attention* is a projection group: rows can be dragged out of it, never in.
- **Items** sit inside groups. They are data rows, local workspaces, and ghosts.

A local group holds both local workspaces, which live there, and ghosts. Today's pinned area becomes a section holding one local group named "Pinned", and its saved `pinned_cards` become that group's entity ghosts.

## Drop table

| Dragged | Dropped on | Result |
|---|---|---|
| Data row (convoy, vessel) | Its own group or parent | Reorder |
| Data row | Another project, or Workspaces | Not a valid target |
| Data row or chip subject | A local group | Ghost of the entity |
| Local workspace | A project group | Move, annotated "lives with project X" |
| Local workspace | A local group | Move |
| Ghost | Another local group, or a new position | Move the ghost |
| Ghost | The tree, or its source's home | Not a valid target |
| Attention row | A local group | Ghost |
| Anything | Attention | Not a valid target |
| Tab or View | Panel or tab strip | Move (docking, unchanged) |
| Tab or View | A local group | View ghost |
| Local group header | Another section | Move the group |
| Local group header | A dock edge in the sidebar | New section that borrows the group's name |
| Project group header | Its own section | Reorder |
| Section | Docking targets | Move (docking, unchanged) |

Dragging a chip drags the chip's own subject (an issue or PR), not the row it sits in.

## Gesture

- **Starting a drag.** Press on the item and move past a small threshold (about 4px). A plain click keeps its current meaning:
  - rows drag from anywhere on the body;
  - sections and cards drag from the title (#210), with the grip shown on hover only as a hint;
  - tabs are unchanged.
- **Spring-loading.** Hovering a drag over a collapsed section, project or group for about 0.6s expands it. A title-bar overview (home) target opens the overview so you can drop into it, then drag back out. Leaving a target re-collapses only what the drag itself opened.
- **Invalid targets never highlight.** They come from the same declared validity checker used for docking.
- **Cancelling.** Esc cancels the drag. Releasing over nothing valid snaps the item back.
- **Releasing never removes anything.** Removal is always an explicit action.

### Drop-target visuals

- **Panel, tab and split drops keep RAD's visuals:** boundary drop-site pills, and an animated rect that grows into the area the new panel will occupy. They are judged and restyled natively, not recreated in a mock-up.
- **Sidebar drops preview their outcome,** following RAD's idea: a faded copy of the item at the place it will land, labelled with what will happen ("Reorder", "Ghost → Pinned", "Move → andamento", "New section", "Float"). A move dims the original; a ghost leaves it untouched. Sidebar sites share RAD's `drop_site` colour and animation rate, so both read as one system.
- **Sections that hold content have three zones,** decided by the pointer's position (agreed 2026-10-07, answering #211's centre-drop question):
  - *into the body* adds to the section's group, with an insertion preview;
  - *onto the header or tab strip* adds as a tab, with a tab-shaped preview in the strip;
  - *at an edge* splits, using RAD's pills and growing rect.

  Sections that accept content have no centre pill. Content panels in the workspace keep RAD's centre pill, where "add as tab" is the only meaning a centre drop has.

## Reorder

The first reorder in a group snapshots the group's current visible order into a saved list of entity keys. After that:

- A row the list hasn't seen is inserted after its nearest predecessor, in data order, that the list does know. With no such predecessor, it goes first. New rows therefore land near where Andamento would put them.
- Rows that disappear are dropped from the list.
- Ordering is only among siblings, at every level: projects, convoys within a project, vessels within a convoy.
- "Reset order" in the group menu clears the list.

The expected common use is moving the things you are watching to the top of their project.

## Ghosts

### Entity ghosts

An entity ghost is a reference to an Andamento entity.

- **Compact form.** By default it appears as a row with live status and the same margin control as other rows.
- **Expanded form.** It can expand in place into the full card, which is today's detached card.
- **Presentation modes.** The expanded card offers modes such as details, labels, preview and facts (possibly grouped), with presets to cycle through. The aim is a small control surface that shows what you need and no more. The modes themselves are part of the card design in #89.

### View ghosts

A View ghost is a reference to one live pane inside a workspace. Example: a convoy draws a running loss graph in a Jackstay or TUI View, and you pin just that View.

- **First form: a read-only graphical mirror.** It reuses the source View's rendered surface, as workspace previews do, and follows #89's attachment policy (when attaching is allowed, budgets, showing the last frame).
- **Clicking it** focuses the source View in its workspace.
- **It never takes the View from its workspace.**
- **Identity** is the View's runtime and content identity (a Cleat session or a Jackstay endpoint), not its position. The ghost therefore reconnects after a restart, or after its workspace is detached and reopened.
- **Later: "upgrade".** An upgrade makes a fresh instance of the same underlying thing, such as a second Cleat connection or a new luchs page, which can also be interactive. Cheap Views (text, binary, image) might upgrade by default.

### Lifecycle

- **Nothing disappears on its own by default.**
  - When a subject ends, its ghost shows the ended state: dimmed, marked ×.
  - When a subject vanishes, or a View's source closes, the ghost shows "unavailable" with its last known label or last frame, plus Remove and Reopen.
- **A per-group setting can remove ended ghosts after a delay.** It is off by default, and #159's expiry policy plugs in here.
- **A ghost's margin control removes the ghost, never its source.** Holding it opens a menu with "Focus source" and "Close source workspace…" as explicitly named actions. A ghost carries a subtle "lives elsewhere" marker, and its tooltip makes the difference from a homed row clear.
- **Deleting a local group** moves its homed workspaces to the default Workspaces group, so nothing is destroyed, and drops its ghosts. It asks for confirmation only when the group has homed workspaces.

## Titles

Each View, section and group shows one title, never two:

- **A section created by dropping a group** has no name of its own. It borrows its only group's name, and that group's header is omitted. Renaming the section's title renames the group.
- **When a second group joins,** the section takes an editable placeholder name ("Pinned", "Pinned 2", and so on) and both groups show their headers. If you never renamed the section, it borrows again when it drops back to one group. A name you gave it sticks.
- **A section with a name of its own** (an Andamento-declared region such as Projects, or any section you renamed) always shows its title, and each of its groups shows its own header.
- **A section shown as a tab** shows its title on the tab only (#211).

A section's name can therefore come from you, from its single group, or from a namer. Automatic naming, and later automatic layout, are delegation hooks; the namer could be built in or come through Flotilla.

## Persistence

- **Scope.** Arrangements are saved per window, alongside existing pins and section positions in the window's docking tree in the user config. Multi-window behaviour is still RAD-inherited and unexplored. Sharing an arrangement across windows can come later as an explicit action.
- **Entity ghosts and order lists** are keyed by Andamento entity `(kind, id)`. Order lists are also keyed by the group's identity: a project's entity key, or a local group's id.
- **The "lives with project X" annotation** is stored on the local workspace's own config node. If the project disappears, the workspace falls back to the default group, keeps the annotation, and returns when the project reappears. This is the same "unobserved is not ended" rule that retained rows follow.
- **View ghosts** are keyed by the View's runtime identity plus its source workspace.
- **Drags are never written back into KDL.**

## Compatibility with existing cards

Floating and pinned cards already exist (`uishell_detached_cards.c`, `docs/detached-card-acceptance.md`). The model must keep the following invariants:

- Today a pin is identified by `(kind, entity)` and is unique per window. Pinning again reveals the existing pin, moving a pin keeps the same node, and copied layouts are deduplicated. Decision 1 below deliberately relaxes the uniqueness.
- Pins are stored as `card{kind, entity, label, source}` inside a `pinned_cards` View in the window's sidebar dock tree. Unknown or incomplete entries are tolerated.
- Only an explicit Close removes a pin. A missing subject shows "No longer present", with no timeout. This is the same as the ghost lifecycle above.
- Every target goes through `rd_dock_check` and `rd_dock_can_create`. A centre drop joins or creates an area, and a directional drop splits the panel.
- An emptied pinned area remains as a placeholder.
- Floats survive mouse-out and the loss of their source. Inline cards close with their source row. Neither is persisted.
- `floating_panels` is a placement fallback for sections, not a rendered host. A group in a floating panel would therefore be a new surface, not something being preserved.

Where the model and today's behaviour differed, the operator agreed the following on 2026-10-07:

1. **An entity may have any number of ghosts.** A ghost is a separate object that holds a reference to its source; it is not a flag on the source, and the source doesn't know its ghosts exist. Different views of one subject serve different purposes, for example a live Watch card for monitoring alongside a compact reminder row. Removing one ghost never affects the source or the other ghosts. Each ghost therefore needs its own identity:
   - a ghost id stored on the `card` node, next to the existing `kind` and `entity`;
   - migration gives each existing pin a ghost id, so current layouts load unchanged;
   - layout deduplication keys on the ghost id rather than `(kind, entity)`, so copied layouts still don't double up;
   - dropping or pinning a subject that already has a ghost creates a new ghost. The menu also offers "Reveal existing" when one exists.

   Dragging a ghost to another group moves that ghost, as before.
2. **"Dock under source" becomes the row's own expand-in-place.** It is an action, not a drop target: the card's dock button, or the row's ⤢. Expansion replaces the row with the card (see the card prototype). A ghost dropped on its source's home therefore stays an invalid target.
3. **Floating stays exactly as it is today.** You drag the card itself and release it where you want it; releasing outside the sidebar floats it there. There are no new float targets or visuals. "Releasing over nothing valid snaps back" applies only inside the sidebar.
4. **Cards drag from the title line,** past the threshold. A click on the card body keeps its focus or keep-open meaning, and the card's action buttons never start a drag.

### Card settings

Card features and profiles (`card-features-prototype.html`) can reuse RAD's per-View settings:

- **Schemas.** Add schema entries for `pinned_cards` (per-area defaults) and for `card` (per-card overrides). That provides typed storage on the config node, `@default` values, and the existing settings lister as the "all settings" view.
- **No enum type.** A small enum is a `string` with a lister, or a `u64 @range`.
- **Profiles.** There is no preset mechanism other than theme presets. Profiles would copy that pattern: a table of named presets plus a string setting.
- **Popover.** A compact custom popover is the everyday editor; the RAD lister is kept as the full view.

## Slices

1. **Gesture.** One drag-start rule (threshold, title as handle, chips drag their subject), spring-loading, and Esc to cancel. This is the drag part of #210.
2. **Row reorder.** The saved order list with the merge rule, at every level, plus Reset order.
3. **Local groups.** The default Workspaces group as a local group; local workspaces homed in project groups by annotation; migrating the pinned area to a "Pinned" local group.
4. **Entity ghosts.** Compact ghost rows in local groups, the ghost marker, and the lifecycle (ended and unavailable states, explicit removal, Clear ended, deleting a group).
5. **Group drag and titles.** Moving groups between sections, creating a section at a dock edge, and the title-borrowing rule.
6. **View ghosts.** Read-only mirrors, after #89's attachment policy.
7. **Ghost card modes and presets.** With #89.
8. **Overview drop target.** With #190.

## Open

- **Drag-target visuals.** The sidebar previews are prototyped in `sidebar-header-controls-prototype.html`. The three section zones, tabs, splits and floating are RAD docking and are designed in the native code; an HTML prototype would either rebuild RAD's drop system or mislead.
- **Interactive View ghosts** and the "upgrade" to a fresh instance.
- **Dragging between windows.** Ephemeral OS windows, with floating things promoted to real OS windows when dragged out. The likely first use is an overview on a second monitor.
- **Automatic naming and layout through a delegate.**

## Prototype

`sidebar-header-controls-prototype.html` already covers rows, groups and local moves under the older "move anywhere" rule. The next pass should change it to match this note:

- data rows reorder only;
- ghosts in local groups, with the compact-to-card expansion borrowed from `sidebar-cards-prototype.html`'s cards;
- dragging a group out to create a section, with title borrowing;
- chips dragging their subject.
