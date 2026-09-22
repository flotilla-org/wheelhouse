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

The review follow-up starts a real thumb drag with a synthetic press, makes that
same region inert while moving the pointer, and checks that its scroll position
does not change. Omitting the overlay prunes its thumb at end-build; the next
begin-build clears the stale active key. The diagnostic verifies that cleanup.

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

## Inactive-panel dimming and panel-builder isolation

The production panel builder applied inactive-panel scrims to offscreen workspace
previews. It now checks the existing workspace surface's presentation role before
drawing that focus cue. Live surfaces and direct rendering still dim inactive
panels; overview tiles and hover previews do not.

`--preview_diagnostics` builds two actual two-panel terminal-fixture workspaces
through `rd_panel_area_ui`. It moves through live-plus-preview, overview, and
direct-live-plus-preview presentation over twelve synthetic builds. It counts
whole-panel scrims and checks that preview builds preserve the live panel/view
command target, hot/active keys, and pending input. The initial regression found
one scrim in each workspace, where the preview should have none. The fixed
sequence is `(1,0)`, `(0,0)`, `(1,0)`, and the interaction checks pass.

This diagnostic runs inside the frame evaluation context. Calling the panel
builder after the frame returns is invalid because its evaluation maps have
already been released. A one-shot diagnostic callback gives the test that
lifetime without reconstructing the evaluator. Linux and macOS CI run the test.

These checks use fixture terminal views and exercise panel construction, not the
whole overview command and GPU-composition pipeline. They do not prove that every
view type is free of side effects, or reproduce the historical cross-workspace
scroll/focus mutation from #11. Keep that issue open pending broader evidence.

## Direct input leaks during preview builds

A follow-up to #11 added pending mouse-press and file-drop events to the panel
diagnostic. It reproduced two effects that the original text-event fixture did
not catch:

- `rd_view_ui` inspected pending presses directly. Because preview and live
  panels share full-window coordinates before composition, a press in the live
  terminal also set the preview view's `contents_are_focused` state. The view
  now skips that press check under an `IgnoreInteraction` ancestor. A live view
  still accepts the press.
- `rd_panel_area_ui` inspected file drops directly. During overview, the first
  preview panel consumed a file drop and retargeted the window's drop-completion
  state. Preview panels now skip file-drop handling. The diagnostic checks that
  overview previews leave the event and drop target unchanged while a direct
  live workspace still handles the drop.

The same macOS diagnostic went red for each effect before its fix and passes
afterward. The scrollbar diagnostic also passes. This exercises panel and view
builders with synthetic input, not OS file delivery or every view type. #11
remains open for wider input and focus isolation work.
