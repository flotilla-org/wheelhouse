# Sidebar group and section headers

Settled with the operator on 2026-10-08 (workstream A: #261, #263, #250 and
#210 item 1). The choices were tried in
[sidebar-header-controls-prototype.html](sidebar-header-controls-prototype.html),
preset "Groups redesign"; the operator's knob values are at the end.

## Groups

- **One look for every group.** Project groups and local groups look the
  same: an inset card. Any difference comes later from an icon or badge, or
  from what the group opens (opening recipes).
- **The card** is inset equally from the left and right. It keeps its side
  accent: a project's colour, a neutral one for a local group. The horizontal
  rule under the title goes.
- **The header** has no icon. Its title is semibold and starts just after
  the accent; the rows' icons line up with it, their own expand/collapse
  moved to the end of the row (to be polished with the card work). The collapse indicator follows
  the title and shows only on hover. A collapsed group shows its count,
  dimmed, after the title; a project keeps its chips.
- **Three levels:** section (small uppercase), group (semibold), row
  (regular, with an icon).
- **Gestures (provisional):** the whole header drags the group, past the
  usual threshold, except its buttons and chips. A click collapses it. A
  double-click renames a local group in place; a project's name is its data.
  Opening stays on dedicated targets: a project's overview chip, and later a
  group's opening recipe.
- **Actions** appear on hover after the title: `⋯` opens the same menu as a
  right-click.

## Making workspaces and groups

- **New workspace** is a footer. When the pointer reaches a group's last
  row, a "+ New workspace" row pushes in below it, and leaves with the
  pointer. A footer stays as it is while a button is held (dragging the
  scroll bar would otherwise open and close it as rows pass the pointer). Choosing it turns the row into a name field (Enter creates, Esc
  cancels). For a project, the workspace lives with the project.
- **New group** shares the trigger. At the section's last group, "+ New
  group" opens too, at the section's level: below the card when the group is
  a visible card, beside New workspace when it isn't (a one-group section,
  or Workspaces). Below the last card, anywhere under its last row keeps
  both open, so the pointer can reach New group. Choosing one makes it the
  name field, which takes the keyboard even from a focused terminal.
- **Workspaces** uses the same footer instead of a first-entry row.

## Mini tabs

In a sidebar panel, the tab strip's `+` adds a tab to that panel: **New
section** (made empty, its name field open), or one of the **closed
sections**, restored there. It offers no other Views until their state has
somewhere to live. Later, a tab should be the section's header rather than
repeat it.

## Collapse gives space back

A collapsed section's panel shrinks to its header along its parent's vertical
split, and the next open section below it (or above, at the end) takes the
space, so expanding restores one neighbour rather than all. Once all are
collapsed the last takes what's left. Expanding restores the size it had.
How space is shared may be tuned later.
In a side-by-side split, the row shrinks only once all of its panels are
collapsed. A size the operator set by hand is kept.

## Scrollbars

The overlay scrollbar shows while scrolling, while it is dragged, and while
the pointer is on its own strip at the outermost edge, then fades after about
a second. A faded thumb takes no clicks, and controls in a row's margin stop
short of its strip. This is a change to the shared scroll region, so every
scrolled area gets it.

On macOS it follows "Show scroll bars": when that is "Always", the bar keeps
the subtle style but its width is reserved.

## Not here

Card controls (minimum size, distinguishable glyphs, hover reveal when
docked; #210 item 4) belong with the card features and profiles work (#89).

## Prototype values

```json
{"gHead":"new","gWeight":"600","discHover":"hover","gActs":"hover","newWs":"footer",
 "trigger":"edge","newGroup":"beside","tabs":"on","scroll":"activity","plus":"footer",
 "btn":"quiet","toggles":"segmented","vis":"hoverKeepOn","secClose":"hoverX",
 "tipStyle":"unified","tipText":"rich","count":"collapsed","disc":"after","grab":"gripHover",
 "rowClose":"margin","status":"trailing","both":"detachClose","proj":"card","margin":18,
 "pdisc":"after","width":338}
```
