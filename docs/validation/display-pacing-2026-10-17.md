# Display-driven presentation, 17 October 2026

Wheelhouse previously selected external images while building UI, before the
renderer could wait for presentation capacity. The Mac measurement fixture
showed about 92 ms median capture-request-to-presentation age at a 60 fps source
cap. That includes WebKit capture time; it is not all host queueing.

## Interface and scheduling

`r_window_request_frame(window, renderer, requested)` arms or cancels native
presentation admission. `r_window_frame_ready` reports whether the caller can
build a displayed frame. Both run on the UI thread. Backends without asynchronous
admission remain immediately ready and retain their current presentation path.

The shell requests opportunities before pumping events. When no window is ready,
the same native pump waits for input, content changes, or presentation readiness.
Input can build and update UI while a window is waiting. Jackstay then reads
state and routes input, leaving the latest image in its mailbox until the host
window has an opportunity. Only ready windows submit their rebuilt draw buckets. Offscreen builds without a
host window retain immediate image acquisition; looking up admission never
creates a window.

A pending window keeps its own request until submission, even when another
window consumes the last globally requested animation tick. Presentation wakes
are distinct from content changes: otherwise windows on different clocks would
continually dirty one another. Animation delta time follows elapsed update time,
with an idle-start baseline and a 100 ms cap; deterministic replays retain their
explicit fixed delta.

## macOS

On macOS 14 and later, each Metal window uses `CAMetalDisplayLink` on the main
run loop. The delegate retains at most one drawable and posts a presentation
wake. It never builds UI or recursively enters the shell. Normal submission uses
that supplied drawable without calling `nextDrawable` again. The link requests
one frame of latency and follows the window's screen refresh rate. The drawable
is released after submission; normal command-buffer tracking still protects
upload-buffer and retired-resource lifetimes.

New windows are shown while their first display-link drawable is pending; they
never mix display-link acquisition with `nextDrawable`. Older macOS retains the
existing path. Idle and minimized windows pause their links. Resize/backing-scale changes discard an obsolete
opportunity and update layer geometry before acquiring its replacement. Window
teardown invalidates the link before freeing its delegate or reusing renderer
state. `--legacy_presentation` selects the previous Mac path for comparison.

Windows D3D11, Windows OpenGL and Linux GLX/EGL implement immediate admission.
Waitable DXGI swap chains and Linux presentation feedback remain follow-up work
requiring measurements on their native platforms. Shared GPU frame import on
Metal/OpenGL is also separate from presentation scheduling.

## Validation

The `--presentation_diagnostics` mode exercises actual native admission with two
windows, cancellation and idle pause, ordinary wakes while paused, consumption
of a drawable, resize, moves through available displays, minimize/restore, and
teardown with pending opportunities. It runs with the macOS UI checks in CI.

Paired measurements use the same executable and source with and without
`--legacy_presentation`, reverse order on the second trial, and retain raw
upload/presentation logs. Source timestamps and Metal presentation times share
the uptime clock. They measure capture request age, not the original page paint
or the physical panel's scanout. The fixture has one web View, so its source
version can be correlated with the window's presentations.

The 60 fps cap, 1284 × 1224 pixel fixture produced these two paired runs (second
pair reversed the mode order):

| Pair | Path | Distinct images presented/sec | Median request age | p95 request age | p95 image gap |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Legacy | 52.34 | 95.52 ms | 104.79 ms | 33.34 ms |
| 1 | Display link | 55.79 | 85.68 ms | 87.01 ms | 33.34 ms |
| 2 | Legacy | 40.49 | 78.50 ms | 94.29 ms | 50.00 ms |
| 2 | Display link | 53.29 | 65.08 ms | 79.68 ms | 33.34 ms |

All counted presentations had vsync enabled. These are shared-machine results,
not an isolated capacity test. One display-link run usually presented one refresh
after its supplied target; the other usually met it. Display admission therefore
does not guarantee meeting every deadline, or eliminate WebKit snapshot costs.
The earlier exploratory pair measured 89.83 → 73.24 ms median age.

The two-window static fixture used the 60 Hz LG and 50 Hz 34UM95 displays. Its
presentation count stayed at 97 from seconds 10 to 13, with a 0.3% host CPU
sample. This checks that presentation wakes do not keep other windows rendering.

The measurements above used the unbundled executable. The initial normal app
bundle produced almost entirely zero `presentedTime` values in both modes;
those runs were rejected. The same source built unbundled and the previously
measured executable both produced valid values. A separate bundle identity is
used for the preview; the precise cause of the normal bundle's behaviour is not
established. No zero-time callbacks are treated as displayed frames.

Raw fixtures, scripts, rejected runs, CSVs and temporary instrumentation patches
are in the local diagnostic directory `local/diagnostics/web-performance/display-pacing`.
Temporary measurement instrumentation is not part of the source change.
