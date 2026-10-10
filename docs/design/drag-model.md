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

## Sections and groups as data

Settled with the operator on 2026-10-08. Worked examples are in [sections-groups-examples.html](sections-groups-examples.html).

**The model.** Sections and groups you make are Andamento entities. Wheelhouse publishes them from the window's layout, which stays their source of truth, as it publishes a local workspace's home (part A).

- **Names.** A leading `.` marks Andamento's own system kinds and facts. Facts producers share by convention, such as `display.label` and `flotilla.project`, keep their names.
- **`.section`** is a section you made. Its only fact is `display.label`, and the label is optional.
- **`.group`** is a group. Facts: `display.label`, and `.section` naming its section.
- **`.workspace`** is a host's local workspace (part A's `wheelhouse.workspace`). It has one home:
  - `.group` for a local group;
  - or `flotilla.project` to live with a project.

  Setting one home clears the other.
- **`.ref`** is a ghost. Facts: `.group` and `.target`. Andamento presents it as its target, taking label, status, live state and details from it, but keeps the ghost's own key. So one entity can have any number of ghosts, even in one group.
  - A group's homed workspaces and ghosts form one sibling run, so they interleave and follow the host-owned sibling order.
- **Presentation stays in Wheelhouse.** A ghost's form (compact row or card), and later its card details and profiles, sit on the ghost's own config node and are never published. What is saved where can be revisited when terminal or web frontends want to share it.

**Placement.** One KDL region holds every section you made. A `for` loop marked `layout="section"` makes each iteration its own section:

- Wheelhouse gives each one its own docked View. Its rule changes from one View per region to one View per section, where a section is a region or a `layout="section"` node.
- The terminal frontend shows each one as a headed section.

Groups are placed with `match ".section" of="section"`, and workspaces and ghosts with `match ".group" of="group"`.

**The default Workspaces group** is a real section and group, with reserved ids and marked `.default`.

- Every local workspace without another home lives there.
- Andamento covers leftover tabs into it, instead of into a separate section.
- A subject workspace whose subject vanishes stays where it was until it is closed.
- It can be renamed and hidden, and its section docked anywhere, but it can't be deleted or moved out of its section, which hosts New workspace.

**Drop intent.**

- A local workspace dropped on a group moves there. Holding Option/Alt while dropping adds a ghost instead and leaves it at home.
- Data rows and cards become ghosts, and ghosts move between groups.
- The drop preview names the outcome ("Move → Builds", "Ghost → Builds").

**Reference or home.** The difference shows on each row, never on the section:

- a reference has the ↗ mark and a muted icon;
- hovering it says "Lives in …";
- its hover card names its home.

Sections and groups look the same whoever made them.

**Closing and deleting.**

- A section's × hides it, and it is restored from Sections…. The × may become "minimise", with Sections… as a tray.
- Deleting is explicit, from the section and group menus:
  - right-click a section's title or hold its ×; right-click a group's header;
  - the menus hold Rename, New group, Hide, Delete, Reset order and New workspace here;
  - a section showing one group carries that group's actions;
  - right-click menus open at the pointer; a held × opens its menu at the ×;
  - Rename… (or double-clicking a title) renames in place: Enter or clicking away applies, Esc cancels.
- Deleting a group moves its homed workspaces to the default group and drops its ghosts. It asks for confirmation only when workspaces would move.
- The default section and group can't be deleted.

**New group and New workspace** are footers that open at a group's last row, designed in [sidebar-headers.md](sidebar-headers.md) along with the group header itself.

**Migration.** Each `pinned_cards` area becomes a section of your own holding one group, keeping its label. Its pins become `.ref`s, keeping their ghost ids and forms. The `pinned_cards` View type is retired. Migration is best-effort: saved layouts aren't precious yet.

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
| Data row or card | Workspaces | Not a valid target |
| Local group header | Another section, between its groups | Move the group there |
| Local group header | A section's tab strip | New section of its own, a tab at the gap |
| Local group header | A dock edge in the sidebar | New section that borrows the group's name |
| Section of yours, or one of its tabs | Another of your sections, between its groups | Its groups move there in order; the section goes |
| Section of several tabs, by its grip | A list | Not a valid target (it docks) |
| Section of data (Projects, Attention…) | A list | Not a valid target (it docks) |
| Project group header | Its own section | Reorder |
| Section | Docking targets | Move (docking, unchanged) |

Dragging a chip drags the chip's own subject (an issue or PR), not the row it sits in.

## Gesture

- **Starting a drag.** Press on the item and move past a small threshold (about 4px). A plain click keeps its current meaning:
  - rows drag from anywhere on the body;
  - sections drag from the title (#210), with the grip shown on hover only as a hint; cards drag from the title line (a float from its cap too), and have no grip;
  - tabs are unchanged.
- **Spring-loading.** Hovering a drag over a collapsed section, project or group for about 0.6s expands it. A title-bar overview (home) target opens the overview so you can drop into it, then drag back out. Leaving a target re-collapses only what the drag itself opened.
- **Invalid targets never highlight.** They come from the same declared validity checker used for docking.
- **A refused drop says so.** Over a target that can't take the drag, a line in the refusal colour (the theme's `bad_pop`, as Close Panel's) marks where it would have gone, and the dragged item carries a "Not allowed" badge with the reason, for example "rows stay in their project" (#282).
- **Groups go between groups.** A drag carrying groups (a group's header, or one of your sections) claims the gap between another section's groups. A section showing one group has a gap above and below it: dropped there, it becomes a list of groups (#282).
- **Cancelling.** Esc cancels the drag. Releasing over nothing valid snaps the item back.
- **Releasing never removes anything.** Removal is always an explicit action.

### Drop-target visuals

Settled with the operator on 2026-10-09 (#257, #262), using `drop-targets-prototype.html`. The tab parts came with the sidebar header redesign (sidebar-headers.md, "A panel's Views share its header").

- **Targets sit at the edges they act on.** A panel's middle belongs to its View, so a drop there means whatever that View says: an insertion line in a sidebar group, or later a placed item in an overview or dashboard. No panel has a centre target, and a body that claims nothing takes nothing.
- **Boundaries nest.** Every boundary docking site is a thin bar along its line. Where several sites share a line (the window's edge, the sidebar's edge and a sidebar row's end, say), the outermost, the split covering the most, straddles the line. Each deeper one stacks inward on its own side, a little shorter. At the window's edge the outermost sits just inside. Nothing overlaps, and the order reads outside in.
- **A panel's own splits stack innermost.** Over a panel, a split pill shows on each edge its parent doesn't already split along (RAD's rule), just inside that edge's bars. The pill shows two boxes, the new half filled. Bars and pills don't move while the drag is over their panel.
- **Tabs drop on a tab strip.** Over a tab strip, the gap under the pointer takes the drop, with a tab-shaped slot there; the strip may be at the top or bottom. A sidebar panel has no strip: its section header is one, holding all its Views, so even a one-View section takes a drop first or second (the ghost strip of the mockup, built in). This gives a one-group section's title drag its "join as a tab" target beside "merge into its group" (#262). Pills on the strip's edge start past it, never over it.
- **One look.** Every target, sidebar insertion lines and the tab strip's slot included, uses the selection accent: bars and pills faintly at rest and solid when hot. The rect growing into the area a drop will fill takes the same accent.
- **Sidebar drops preview their outcome,** following RAD's idea: a faded copy of the item at the place it will land, labelled with what will happen ("Reorder", "Ghost → Pinned", "Move → andamento", "New section", "Float"). A move dims the original; a ghost leaves it untouched.
- **Later:** tinting each region's targets differently at rest, perhaps by depth, so stacks read at a glance.

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
- **Pin.** A card's Pin adds a ghost to the first group, other than Workspaces, of the topmost local section that is showing (its panel's selected tab). With none showing, Pin makes a section at the top of the sidebar. Pin never targets the default Workspaces group. If the entity already has a ghost in that group, Pin reads "Show pin" and reveals it. A ghost in another group doesn't count, so Pin adds one here too: an entity may have any number of ghosts.

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
- **When a second group joins,** a section without a name of its own shows its groups' names, joined ("Builds, Tests"), and both groups show their headers. Naming the section replaces that. A name you gave it sticks; clearing it goes back to the groups' names. Later, a namer could suggest what the groups have in common.
- **A section with a name of its own** (an Andamento-declared region such as Projects, or any section you renamed) always shows its title, and each of its groups shows its own header.
- **A section shown as a tab** shows its title on the tab only (#211).

A section's name can therefore come from you, from its single group, or from a namer. Automatic naming, and later automatic layout, are delegation hooks; the namer could be built in or come through Flotilla.

## Persistence

- **Scope.** Arrangements are saved per window, alongside existing pins and section positions in the window's docking tree in the user config. Multi-window behaviour is still RAD-inherited and unexplored. Sharing an arrangement across windows can come later as an explicit action.
- **Entity ghosts and order lists** are keyed by Andamento entity `(kind, id)`. Order lists are also keyed by the group's identity: a project's entity key, or a local group's id.
- **The "lives with project X" annotation** is stored on the local workspace's own config node. If the project disappears, the workspace falls back to the default group, keeps the annotation, and returns when the project reappears. This is the same "unobserved is not ended" rule that retained rows follow.
  - *How it is placed* (settled 2026-10-08, #249 and andamento#139): each local workspace is an Andamento **host entity**, kind `wheelhouse.workspace` (to become `.workspace`, above), with an id saved on the workspace: its Workspace ID (`workspace_id`; before #306 a GUID of its own, `local_entity`).
    - Wheelhouse publishes its label and, when homed, `flotilla.project` (`lives_with`), and tags its tab with `host.entity.*`.
    - A KDL rule in the project template (`match "flotilla.project" of="project"`) places it in the project's group as a row. Chips stay for workspaces about the project, such as its overview and roles (#254).
    - Its rows are live for its tab, but it is not the tab's subject, so closing it destroys it and retracts the entity.
    - Being an entity also gives it details, so it has a hover card (#252).
- **View ghosts** are keyed by the View's runtime identity plus its source workspace.
- **Drags are never written back into KDL.**

## Compatibility with existing cards

Floating and pinned cards already exist (`uishell_detached_cards.c`, `docs/detached-card-acceptance.md`). The model must keep the following invariants:

- Today a pin is identified by `(kind, entity)` and is unique per window. Pinning again reveals the existing pin, moving a pin keeps the same node, and copied layouts are deduplicated. Decision 1 below deliberately relaxes the uniqueness.
- Pins are `card{kind, entity, label, source, ghost}` inside local groups, which Andamento owns as local `.ref` entities in its dashboard record; Wheelhouse edits a working copy of them (they were in the window's `sidebar_local` node until state model step 3, and inside `pinned_cards` Views before 2026-10-08). Unknown or incomplete entries are tolerated.
- Only an explicit Close removes a pin. A missing subject shows "No longer present", with no timeout. This is the same as the ghost lifecycle above.
- Every target goes through `rd_dock_check` and `rd_dock_can_create`. A centre drop joins or creates an area, and a directional drop splits the panel.
- An emptied group remains; its section's Close control hides it.
- Floats survive mouse-out and the loss of their source. Inline cards close with their source row. Neither is persisted.
- `floating_panels` is a placement fallback for sections, not a rendered host. A group in a floating panel would therefore be a new surface, not something being preserved.

Where the model and today's behaviour differed, the operator agreed the following on 2026-10-07:

1. **An entity may have any number of ghosts.** A ghost is a separate object that holds a reference to its source; it is not a flag on the source, and the source doesn't know its ghosts exist. Different views of one subject serve different purposes, for example a live Watch card for monitoring alongside a compact reminder row. Removing one ghost never affects the source or the other ghosts. Each ghost therefore needs its own identity:
   - a ghost id stored on the `card` node, next to the existing `kind` and `entity`;
   - migration gives each existing pin a ghost id, so current layouts load unchanged;
   - layout deduplication keys on the ghost id rather than `(kind, entity)`, so copied layouts still don't double up;
   - dropping a subject into a group is explicit placement, so it always creates a new ghost. Pin on a subject that already has a ghost shows the existing one, so duplicates stay deliberate: a second ghost is made by dropping. ("Pin another", by holding Pin, was agreed on 2026-10-07 and dropped on 2026-10-10.)

   Dragging a ghost to another group moves that ghost, as before.
2. **"Dock under source" becomes the row's own expand-in-place.** It is an action, not a drop target: the row's ⤢, and the expanded card's ⤡ back to the row in its cap (#269). Expansion replaces the row with the card (see the card prototype). Until the row's ⤢ lands, dropping a card on its source row still expands it there. A ghost dropped on its source's home therefore stays an invalid target.
3. **Floating stays exactly as it is today.** You drag the card itself and release it where you want it; releasing outside the sidebar floats it there. There are no new float targets or visuals. "Releasing over nothing valid snaps back" applies only inside the sidebar.
4. **Cards drag from the title line,** past the threshold; a floating card also from its cap's background. A click on the card body keeps its focus or keep-open meaning, and the card's action buttons never start a drag.

### Card settings

A card's controls live in its cap (#269; `sidebar-cards-and-docking.md`, "Peek and engaged"), which is where card settings (⚙) will go too.

Card features and profiles (`card-features-prototype.html`) can reuse RAD's per-View settings:

- **Schemas.** Add schema entries for `pinned_cards` (per-area defaults) and for `card` (per-card overrides). That provides typed storage on the config node, `@default` values, and the existing settings lister as the "all settings" view.
- **No enum type.** A small enum is a `string` with a lister, or a `u64 @range`.
- **Profiles.** There is no preset mechanism other than theme presets. Profiles would copy that pattern: a table of named presets plus a string setting.
- **Popover.** A compact custom popover is the everyday editor; the RAD lister is kept as the full view.

## Slices

1. **Gesture.** One drag-start rule (threshold, title as handle, chips drag their subject), spring-loading, and Esc to cancel. This is the drag part of #210. *Done, except spring-loading.*
2. **Row reorder.** The saved order list with the merge rule, at every level, plus Reset order. *Done (#245, andamento#137).*
3. **Local groups.**
   - *Done:* local workspaces as host entities, homed in project groups by annotation (#249, andamento#139).
   - *Next:* sections and groups as data, above, in three steps:
     1. **Andamento:**
        - `.`-prefixed system names;
        - the `layout="section"` loop;
        - `.ref` entities presented as their targets;
        - the `.default` group covering leftover tabs.
     2. **Wheelhouse, the model at parity** (done in the local-groups PR):
        - publish sections, groups, refs and homes;
        - one View per section;
        - the default Workspaces section and group;
        - migrate and retire `pinned_cards`;
        - today's drags on the new model.
     3. **Wheelhouse, group management** (the group-management PR):
        - section and group menus;
        - dragging a group's header to another section or a dock edge;
        - the title rules.

        A section showing one group has no group header: it is that group, so its title's drag is the group's too. Over another section's group, the group moves in and the emptied section goes; on a docking site it docks as before.
4. **Entity ghosts.** *Done:* compact ghost rows, the ghost marker, explicit removal, positioned drops (#249). *Left:* the ended and unavailable lifecycle, and Clear ended.
5. **Group drag and titles.** Folded into step 3.3.
6. **View ghosts.** Read-only mirrors, after #89's attachment policy.
7. **Ghost card modes and presets.** With #89. Their state stays in Wheelhouse (above).
8. **Overview drop target.** With #190.

## Open

- **Drag-target visuals.** Docking targets are settled (above, from `drop-targets-prototype.html`). The sidebar previews are prototyped in `sidebar-header-controls-prototype.html`.
- **Interactive View ghosts** and the "upgrade" to a fresh instance.
- **Dragging between windows.** Ephemeral OS windows, with floating things promoted to real OS windows when dragged out. The likely first use is an overview on a second monitor.
- **Automatic naming and layout through a delegate.**

## Prototype

`sidebar-header-controls-prototype.html` already covers rows, groups and local moves under the older "move anywhere" rule. The next pass should change it to match this note:

- data rows reorder only;
- ghosts in local groups, with the compact-to-card expansion borrowed from `sidebar-cards-prototype.html`'s cards;
- dragging a group out to create a section, with title borrowing;
- chips dragging their subject.
