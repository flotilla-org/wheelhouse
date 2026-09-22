# Preview isolation and image freshness

Investigation of Wheelhouse #9 and #11 after sidebar and tooltip work merged in
`efe9c60`. Neither issue is closed by this change.

## Overlay scrollbar hover

The scroll-region builder reads `ui_mouse()` to choose overlay visibility and
width. Unlike `ui_signal_from_box`, that path did not check `IgnoreInteraction`
on its ancestors. Workspace previews have that flag, but their layout uses full
window coordinates before the offscreen texture is composited into a smaller
preview. A pointer over live content therefore also animated an overlapping
preview's scrollbar.

The regression builds live and preview scroll regions at the same coordinates,
with distinct keys, through the production builder. After four synthetic frames,
the live overlay should be visible and the preview overlay absent. Before the
fix it reported `live=1 preview=1`; afterward it reports `live=1 preview=0`.
Preview content signals also remain inert. The diagnostic uses a separate UI
state and sends no input to the OS.

Run the regression with `build/wheelhouse --scroll_region_diagnostics`, supplying
temporary `--user` and `--project` paths. Existing Linux and macOS CI jobs already
run this diagnostic. The full diagnostic passes locally on macOS.

The fix skips pointer-driven overlay furniture and its animation updates under
an `IgnoreInteraction` ancestor. Classic scrollbars retain their existing layout
and signal-based interaction guard. This does not establish that all view code
is free of side effects during preview rendering.

## Kitty image updates

A separate 900-by-550 native test window ran a local in-process cleat terminal.
The source hid the cursor, transmitted a 16-by-16 RGB Kitty image with image ID 1
and placement ID 1, then replaced its pixels between red and blue. There was no
accompanying text output. The source remained alive, and updates were triggered
through a file without moving the pointer or sending input to Wheelhouse.

Screenshots changed to the requested colour in all three cases:

- The visible terminal, red to blue.
- Its overview tile, blue to red.
- Its hover preview while another workspace was selected, red to blue.

Pixel counts used broad red/blue thresholds to allow for display colour
management. The visible image had about 31,700 matching pixels, the overview
image 7,056, and the hover image about 6,300. Each captured update replaced the
previous colour. Source and screenshots are temporary artifacts under
`/tmp/wh-preview-freshness/`; this was a local acceptance probe, not a CI test.

This did not reproduce #9. It does not cover remote cleat sessions, deletion or
movement without replacing pixels, offscreen-to-visible transitions after long
idle periods, or resizing. A retained preview with no active demand is allowed
to remain stale; that is separate from a demanded preview failing to refresh.

## Remaining work

Issue #11 also records inactive-panel dimming in previews and changes to active
workspace interaction state. The overlay regression proves one input-dependent
preview effect; it does not prove cross-workspace scroll or focus mutation.
Those need a full workspace/overview transition test before changing the broader
render context or claiming the issue resolved. Inactive-panel scrims still run
in `rd_panel_area_ui` for preview builds.
