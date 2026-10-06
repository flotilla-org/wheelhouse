# Section restoration and chip access

The Sections menu lives in the persistent sidebar footer. It lists only current
declarations that have no docked or floating View at the owning Controlled Split
level. Restoring uses the declaration's current host hint and shared placement
checker, clears its closed record, and allocates the new panel a sibling share.
Existing panels keep their relative allocations and nested structure; malformed
or zero saved weights receive a positive minimum before normalization. Existing
Views are never duplicated. Malformed saved duplicates retain the first valid
View per section id at its owning Controlled Split; extra copies are removed
without changing unrelated panel contents or saved ratios. Intentional closes remain closed across restart.
Restore failures leave the menu open and show a footer notice; native diagnostics
exercise both failure and success through the production footer action. Menu width grows
with declaration titles, bounded by the window width.

Empty placed sections retain their header. Flat vertical manual stacks repair
undersized leaves to keep headers visible while preserving healthy sibling
proportions; nested and horizontal manual layouts retain their saved sizing.

All tab presentations use the existing dotted left-side grip with the same
ten-point threshold. Selection, collapse, display controls and close remain
separate controls. Ordinary tab targets reserve an extra 1.5em for the grip;
compact tab floors reserve both grip and close control widths. Overflow stays
in the existing horizontally scrollable tab strip. The measured drop checker
still excludes child workspaces.

Protected chips retain a scrollable viewport at narrow widths. Tab and Shift-Tab
reveal the focused chip or overflow button; ordinary toolkit activation applies.
Horizontal trackpad events scroll the chip viewport before its enclosing section
can claim them. The fixed trailing status slot remains outside that viewport,
and quiet actions share the existing overflow menu. When chips consume the
name column, the fixed row icon opens the same full-label hover card and retains
row activation.

## Acceptance record

The owner accepted the dotted grips and chose ended pinned-card retention until
explicit close (#202), then reported #207 merged. Final physical restoration,
compact-tab and narrow trackpad/keyboard answers were requested but not relayed;
the merge report does not constitute explicit acceptance of those interactions.

Short native macOS diagnostics and capped Linux diagnostics passed. No successful
macOS address-space cap or final physical input acceptance is claimed. Historical candidate paths, host details and per-run notes are preserved in the
[#215 validation record](https://github.com/flotilla-org/wheelhouse/issues/215#issuecomment-6023625179).
