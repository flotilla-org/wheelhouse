# Section placement

The core declares region identity and content. `default-host="sidebar"`,
`order=10` and `pinned` supply initial placement hints. Where sections
actually are is the Dashboard's sidebar arrangement (ADR 0012): one document in
the core's `dashboard` record, holding the dock's panel tree and the
sections closed on purpose. The core reconciles it with what
the template and the local sections declare; its rules are in the core's
`docs/core/sidebar-design/sidebar-arrangement.md`. A region's name is its section
key, a local section's is `.section:<id>`, and `.unplaced` is the workspace
fallback.

Wheelhouse's live copy is the window's dock arrangement
(`native/src/shell/shell_core.c`, "Live arrangements";
`native/src/uishell/uishell_sidebar_store.c`). Docking edits it as before, through
the shared docking checker; at the end of a gesture the whole document is
committed (`wheelhouse_sidebar_arrangement_commit`). Closing a section is
committing without it. When the core changes the document itself (a newly
declared section placed by its hints, a duplicate removed, another frontend's
commit), the live copy follows it. The Sections menu's Restore and
Reset To Default Panel Layout are the core's calls.

The rules the core applies, as Wheelhouse's C reconciliation did before:

- hints regardless of declaration order: explicit orders sort lower first, an
  omitted order is the declaration index, ties keep declaration order, and an
  unhinted `.unplaced` goes last;
- a new section goes before the subtree of its next hinted neighbour in the
  same host, keeping saved nested splits, selection and weights;
- duplicate tabs: the first in the dock wins, and a panel the removal empties
  goes; unrelated empty panels stay;
- the first commit of a sidebar saved before the core kept it adopts it,
  closing the declared sections it lacks;
- Restore gives an equal share of the dock, the others scaled to make room;
- a changed host hint moves nothing already placed.

Rules that changed when the core took this over:

- A region the template drops or renames keeps its tab, flagged (ADR 0013),
  instead of being removed with its closed record. Its View shows a
  placeholder with Remove; if the template has it again, it is where it was.
  An empty declaration flags every section instead of clearing them.
- Pinned regions sort before unpinned ones.
- `.unplaced` is always declared and placed. The sidebar hides it (no height)
  while it stands aside for the Workspaces section, instead of leaving it out.
- Floating Panels are gone (#380). Every section docks, whatever its
  `default-host`. A saved document's floating panels are dropped on import,
  and the sections only they held are docked by their hints.

What stays with Wheelhouse needs pixels or its own config: the docking checker
(a section never docks inside a child Workspace level); a section the startup
safety restore moved (`section_hint_pending`), which the next commit leaves
out and then restores by its hints; and KDL titles refreshing section Views'
labels. The sidebar's width, sizes set from content and collapsed sections are
this device's Presentation State, kept by section key in the presentation
file's `sidebar_presentation`; sizes set by dragging are the document's.

Run `python tools/test-section-placement.py` after building for the headless
lifecycle scenarios and the KDL source-integrity check. They use the real
the core ABI, docking checker and config codec, and restart through the
presentation file and the dashboard record: hints, reorder, duplicates,
add, close, restore, drift, reset, nested saved splits, invalid saved
placement, adoption, host and order variants, unusual keys and a merged leaf.
The operator performs the physical drag check after merge.
