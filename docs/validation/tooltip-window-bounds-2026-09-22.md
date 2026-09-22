# Tooltip window bounds

Issue #5 originally pointed at missing tooltip clamping. The current UI core
already clamps floating roots, but the standard tooltip card was marked floating
inside its own tooltip root. Children-sum sizing excludes floating children, so
the root measured zero by zero while the visible card extended off screen.

The native diagnostic reproduced a 184-by-104 card at `[2530,1148,2714,1252]`
inside a 2560-by-1152 client window, with a zero-size root. Removing the inner
floating flag lets the existing root layout measure the card.

Bounded tooltips now leave two logical pixels for their border, flip above their
anchor (or cursor) when they fit there, and shift inward at the other edges.
Oversized cards are capped to the client area and clip excess content. This does
not introduce scrolling or interaction within hover cards. The explicit overflow
mode used by drag previews remains available. Context-menu placement is unchanged.

Validation:

- `--tooltip_diagnostics` exercises the production `UI_Tooltip` and
  `ui_end_build` paths: bottom/right anchoring, ordinary placement, cursor
  placement, oversized content, and explicit overflow. It failed before the fix
  and passes afterward. Linux and macOS CI run this diagnostic.
- Native screenshots in an isolated 900-by-550 window show the workspace preview
  opening above a bottom sidebar row and the rightmost toolbar tooltip shifting
  left with its full border visible.
- Existing sidebar and scroll-region diagnostics pass.

Temporary screenshots are in `/tmp/wh-tooltip-probe/`. Native visual inspection
was on macOS at 2x scale; the placement diagnostic does not depend on that scale.
