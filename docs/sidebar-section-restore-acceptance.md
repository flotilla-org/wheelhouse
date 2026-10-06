# Section restoration and chip access

The Sections menu lives in the persistent sidebar footer. It lists only current
declarations that have no docked or floating View at the owning Controlled Split
level. Restoring uses the declaration's current host hint and shared placement
checker, clears its closed record, and allocates the new panel a sibling share.
Existing panels keep their relative allocations and nested structure; malformed
or zero saved weights receive a positive minimum before normalization. Existing
Views are never duplicated. Intentional closes remain closed across restart.

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

## Kiwi review

The disposable candidate is `/tmp/wheelhouse-restore-review/Section Restore Review.app`.
Its user and project settings are `/tmp/wheelhouse-restore-review/user` and
`/tmp/wheelhouse-restore-review/project`. Each restart was announced. The daily
driver was not replaced or restarted.

On 2026-10-06, the operator accepted the dotted grips and chose to retain ended
pinned cards until explicitly closed (#202). The first restore-menu review
reported that Sections did nothing. Native press/release diagnostics reproduced
a clipped footer hit area and popup layout inherited from the notice rectangle;
the fixed rectangle now applies only to the notice container. The menu opens
upward. Restoring Attention in the refreshed candidate also exposed an excessive
allocation in a manually sized layout, which is covered by the sibling-share
normalization check.

Final restoration, merged-tab and narrow trackpad/keyboard acceptance is pending
on the corrected candidate.

## Automated evidence

An isolated Linux release build runs on feta under
`prlimit --as=8589934592 --`, with the existing single-CPU preload adapter,
`--async_thread_count:1`, `LP_NUM_THREADS=1` and `MALLOC_ARENA_MAX=2`.
Section placement exercises closed and restored config serialization, repeated
restore, unknown declarations, provider loss, owning-level isolation, all-closed
arrangements, malformed/zero sibling allocations and a merged host with leaf
settings, selected tab and focused panel preserved. Panel diagnostics drive the
footer's actual press/release path and check that the menu stays above its anchor
using the measured menu height, including a font-size change
while open. Twelve-tab ordinary and compact strips accept precise horizontal
scrolling and reveal the final tab grip.
Chip diagnostics cover protected focus, horizontal precise input, nested scroll
ownership, overflow-button focus, removal/rebuild refocus, resize and fixed
status geometry at 90, 140, 240, 320 and 600 pixels.
The native ABI suite, docking policy suite, section placement, sidebar and panel
diagnostics pass. The macOS candidate debug build also passes.

macOS rejects `ulimit -v` and `ulimit -d` with Invalid argument. No successful
macOS address-space cap is claimed. Its automated diagnostic execution remains
pending the operator's watchdog choice. The AppKit translation fixture now
covers horizontal and vertical axes in both precise and line units.
