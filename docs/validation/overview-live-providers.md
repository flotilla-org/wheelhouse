# Live Cleat preview workload

The synthetic replay established rendering improvements but bypassed provider
snapshot work and drove every frame itself. This check uses 48 ordinary in-process
Cleat terminal providers, including their PTYs, VT engines, output notifications
and render-update API. It needs no Flotilla connection or existing user sessions.

```sh
python3 tools/benchmark-overview-live.py --output local/live-check --transitions
```

The source prints coloured output containing box drawing, arrows, CJK and emoji,
then a `FINAL UPDATE` marker followed by silence while the process remains alive.
The runner checks all 48 caches received that marker, all preview queues drained,
and the frame count stopped increasing for at least two seconds. It terminates
only its own isolated Wheelhouse process afterward. Artifacts include frame CSV,
application logs and a JSON summary. Output directories must be new.

With `--transitions`, the native test driver selects another workspace, expands
it, reopens overview, then resizes smaller and larger at two-second intervals
while real output continues. This exercises the normal window/layout state with
real providers; it still does not simulate physical mouse input.

## Findings and changes

The first live check (`local/live-provider-check`) reached 0.0% sampled process
CPU and a fixed frame count after output stopped. However, its first frame spent
690 ms building all 48 providers. With the preview render budget enabled,
background provider starts are now limited to one per frame. Selected terminals
remain immediate; pending views display `Starting terminal…`. All views continue
to build their ordinary UI.

The first live transition run then exposed another cost that synthetic cells
could not reproduce: fetching 48 render updates at once. Reopening spent 86 ms in
the update path; resize bursts spent 40–70 ms there. An instrumented check found
zero resizes with an empty view layout, ruling out that hypothesis.

A FIFO now admits four background terminal snapshot requests per frame. A
selected terminal bypasses it. Deferred requests retain their last cache and do
not mark the provider generation observed. Cleat continues reading and processing
PTY output; only Wheelhouse's snapshot consumption is delayed. The queue requests
frames until it drains, including after a producer's final wake. Hidden requests
age out of the head, and runtime release unlinks a view before destroying it.
The preview diagnostic exercises recurring producers, foreground promotion,
release from the middle of the queue, stale hidden heads and eventual service.

## Event waiting is not rendering work

The real event loop can sleep while the selected terminal is quiet. The original
frame timer included that sleep, producing apparent 75–80 ms frames during the
expanded phase even though window building took only a few milliseconds.

The macOS window manager now reports time in its first blocking native event
wait, separately from event dispatch and drawing. The live runner subtracts that
reported wait for its active-frame gate; raw elapsed time and wait time remain
in the CSV. This is not a physical input-latency or input-to-photon measurement.
Other window-manager backends currently leave the wait measurement at zero; the
native overview test driver remains macOS-only.

First paint is reported separately because it includes native-window and common
font initialization. The active-frame gate covers every subsequent frame,
including the remaining background provider starts.

## Measured queued run

`local/live-queued-transitions` consumed the final marker in all 48 terminals,
drained and went idle. First paint took 197.85 ms. Worst active frame by phase:

| Phase | Maximum |
| --- | --- |
| Background initialization | 26.04 ms |
| Selection | 10.10 ms |
| Expansion | 9.57 ms |
| Return to overview | 19.53 ms |
| Smaller resize | 11.11 ms |
| Larger resize | 15.04 ms |

This passed the local 50 ms stall gate. The one-provider synchronous start can
still exceed a 60 Hz frame budget; the result does not establish perfect pacing.
For stricter checks, pass `--max-frame-ms`. All final cache markers are checked,
but mixed-content pixels and physical interaction still need acceptance before
default enablement.

## Native window access

A temporary diagnostic confirmed Cocoa reported the fixture window visible with
a valid window number on its first three frames. Computer-use nevertheless
returned `cgWindowNotFound` for the isolated app bundle. The diagnostic was removed
and the temporary interactive process stopped. This is evidence of a tool/window
lookup problem, not evidence that mouse-driven acceptance has succeeded.

## Final checks for this iteration

The matched-binary check (`local/live-release-v2-on` and
`local/live-release-v2-control`, binary SHA-256 prefix `3abcb9e2c1e8`) passed with
the budget enabled and failed the same 50 ms gate with it disabled. The active
frame maxima were 26.54 ms and 429.46 ms respectively; the control's largest stall
occurred on return to overview. Both consumed all final markers and became idle.
This demonstrates the gate catches the unbounded update burst even after event
waits are separated. It is one matched pair, not a statistical latency guarantee.

All seven native diagnostics passed (`local/live-release-diagnostics`), including
the queue lifetime tests. Two all-scrolling interrupted synthetic replays also
passed (`local/live-changes-replay`). The final candidate's live-provider rerun
is recorded separately in `local/live-final-candidate`.

## Image replacement and deletion

The live source also supports `--images`: it alternates two 16×16 RGBA images
under one Kitty image ID, deletes them every third update, and deletes all
placements before its final text marker. This exercises texture replacement while
other workspace surfaces are queued, without turning the workload into a decode
throughput test.

```sh
python3 tools/benchmark-overview-live.py \
  --output local/live-images-check --transitions --images --screenshots
```

The runner checks that every terminal consumed an image placement, same-ID
replacement did not accumulate resources, final placements disappeared, and the
frame loop went idle. Optional Metal readbacks capture each transition phase and
the settled final overview. Pixel checks require both source colours to appear
and neither to remain in the final capture. These are simple solid-colour
lifecycle checks, not general image fidelity tests. Readbacks synchronize the GPU;
omit `--screenshots` for timing runs without that additional interference.

The first version of the test incorrectly required cached resources to disappear
with placements. The current provider ABI exposes placement-derived resources;
Wheelhouse deliberately retains unplaced assets below its quota. The correct
expectation here is zero placements and at most one cached resource per terminal.
The first final capture also ran on the frame that consumed the last update,
before the composed preview reflected it. Final readback now waits for a frame
with no updates, bucket rebuilds, surface admissions or pending preview work.

`local/live-images-settled` passed with all 48 final markers, bounded resources,
no final image-colour pixels, and eventual idle. Worst active frame was 32.03 ms;
first paint was 197.72 ms. This uses the same optimized-C/debug-Rust build as the
previous measurements. Neither small-image coverage nor scripted transitions
establish physical mouse responsiveness or large-image decode performance.

The final runner, including its automated pixel assertions, passed separately in
`local/live-images-pixel-gate`: 36.42 ms worst active frame, 189.87 ms first paint,
all 48 final markers and no failed checks.

## Supported release build

The next run used the existing `bash build.sh wheelhouse release` configuration:
C at `-O2`, `BUILD_DEBUG=0`, and Cargo's release profile for all dependencies. No
compiler wrapper is required. The local dependency checkout/target-directory
overrides remained the same. `local/live-release-native` passed the 48-terminal
image, pixel, transition and idle checks: 37.96 ms maximum active frame and
212.36 ms first paint. All seven native diagnostics also passed in
`local/native-release-diagnostics`.

The isolated interactive app uses this release binary with all three preview
policies enabled. The policies remain opt-in pending hands-on acceptance. The
computer-use service still reports `cgWindowNotFound` when binding the app; a
running process and advancing metrics establish rendering, not mouse usability.
