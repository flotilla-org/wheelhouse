# Scroll Regions Own Scrollbar Layout and Interaction

**Status:** implemented.

Classic and overlay scrollbars use one scroll-region module in `src/ui/`.
The region calculates the viewport and both bar rectangles, builds the bars
before the clipped content box, and returns requested scroll positions.
Callers no longer choose a placement path for each style.

Layout and building are separate operations. Wrapped text needs the viewport
width before counting visual lines; a terminal needs it before choosing its
cell grid. `ui_scroll_region_layout` is a pure calculation. Its result is used
to measure content and then passed to `ui_scroll_region_build` with the content's
scroll metrics. Layout coordinates are pixels relative to the parent. Scroll
metrics use the caller's units: an inclusive range of legal positions and a
visible extent, expressed in rows or pixels as appropriate.

Each axis is Off, Always, or Auto. Always reserves a classic gutter even when
there is nothing to scroll, preserving existing list and terminal layout.
Use Always for content whose measurement depends on the viewport, such as
wrapped text. Auto accepts a separately measured content extent in pixels.
It resolves both gutters together because one gutter can cause overflow on the
other axis. Auto does not perform text reflow or call back into content layout.
Every layout calculation starts afresh, so gutters disappear when content fits.

Overlay bars leave the viewport unchanged. They appear while the pointer is
inside the viewport and expand near their respective edges. The two bars have
separate animation identities and leave room for their expanded footprints at
the shared corner. An active drag keeps its own bar visible outside the region
until release. An overlay is absent when its legal position range is empty.
Classic Always bars remain visible but disabled in that case.

The content owns its authoritative position, wheel events, keyboard navigation,
and offset animation. A workspace list applies a requested pixel offset to its
UI box. A virtualized list applies a row position to its list state. A terminal
sends a viewport command to Cleat and reads back the backend's next snapshot.
The region does not consume wheel events or translate the terminal canvas.

Thumb length uses the visible extent divided by the legal travel plus the
visible extent. Text uses a one-em right inset inside the calculated viewport
in both styles; classic text no longer adds a second style-specific inset.

The shell supplies the active style through UI configuration. The module does
not look up Wheelhouse settings or depend on terminal types. Wheelhouse and
raddebugger remain self-contained repositories; useful changes can be ported
manually without maintaining shared code or compatible interfaces.

`--scroll_region_fixture` opens a grid that exercises both axes, style switching,
and fitting content. `--scroll_region_diagnostics` checks layout and synthetic
UI interactions using a separate UI state. Existing views retain their current
supported axes; this change does not add horizontal content navigation to the
text or binary viewers.
