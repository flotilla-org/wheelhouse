# Workspace preview performance

Regression loop for [#103](https://github.com/flotilla-org/wheelhouse/issues/103).
The three preview policies are enabled by default. Robert confirmed that the
isolated 48-terminal release fixture was visible and responsive during overview
transitions and resizing. The latest live checks still expose a startup hitch;
see the results below. This is not a guarantee of 60 Hz.

## Current policies

| Work | Policy |
| --- | --- |
| Background Cleat starts | One per frame, process-wide FIFO. |
| Background terminal snapshots | Four per frame, separate process-wide FIFO. |
| Background workspace surface rendering | Four pending surfaces per window per frame, oldest rendered first. |
| Preview surface size | Quantized displayed-size demand; selected workspaces retain full resolution. |
| Small terminal glyph buckets | Refresh every 200 ms up to 128 points wide, 100 ms up to 256; larger previews have no added delay. |

Selected, expanding and directly composited workspaces bypass background rendering
limits. Provider queues bypass foreground requests, expire undemanded heads and
unlink both requests on view release. They are process-wide because windows share
the UI thread; surface admission is per-window.

Terminal logical dimensions and PTY processing are unchanged. Deferred snapshots
do not mark producer generations observed. Deferred draws retain completed pixels
and request another frame until pending work drains, including after final output.
Missing previews retain their labels while waiting for their first image.

Surface admission includes producer versions, workspace geometry and animation.
Unversioned views retain byte-hash rendering after surface initialization; their
ongoing changes are not bounded by the version-based queue. Jackstay currently
falls into this category despite contributing a frame version. Its frame uptake
and uploads also precede surface admission; stream consumption needs its own policy.

Terminal geometry/font/selection changes refresh immediately. Current or retained
image-bearing glyph buckets bypass temporal reuse to avoid referencing replaced
textures. Terminal image uploads use dynamic textures to avoid Metal's synchronous
static-upload wait. These policies do not bound one very expensive view or reduce
large-image decoding, transport work or whole-grid snapshot copying.

Binary comparison controls:
`--no_preview_render_budget`, `--no_preview_surface_budget`,
`--no_preview_refresh_budget`. The render switch also disables provider admission.

## Reproduce

Use the pinned dependencies and the supported release build:

```sh
bash build.sh wheelhouse release
python3 tools/benchmark-overview.py --output local/overview-check \
  --preview-refresh-budget --preview-surface-budget --preview-render-budget \
  --max-frame-ms 50
python3 tools/benchmark-overview-live.py --output local/overview-live \
  --transitions --images --screenshots
```

Both native drivers currently require macOS. Each output directory must be new.
They create isolated configuration and terminate only their own process; they do
not use daily-driver sessions or connect to Flotilla. Keep hardware, scale, fonts,
build flags and competing load consistent. Each run records binary hashes, frame
CSV, logs, JSON results and optional composed-stage PPM readbacks.

The synthetic runner explicitly enables only the policies requested on its command
line. Its default is a control with all three disabled, unlike normal Wheelhouse.
It runs 1/16/48 workspaces twice, with 240 fixed-timestep frames per run. Eight
30-frame phases cover warmup, overview entry, selection, expansion, return, smaller
resize, larger resize and final drain. Output repeats static, cursor-only, row
replacement and scrolling cases through the real cell/image-cache consumers.

Useful synthetic options:

- `--counts 48 --repeats 2 --interrupt --busy`: interrupt initial rendering and
  make every terminal scroll.
- `--baseline PATH/summary.json --match-checkpoints expand drain`: compare timings
  and require active-view/final-preview pixel convergence. The default relative
  timing tolerance is 15%; inspect failures rather than discarding noisy runs.
- `--max-build-p95-ms 16.7`: CPU-build gate, separate from the individual-frame gate.
- `--no-screenshots`: omit GPU-synchronizing readbacks for timing confirmation.

Synthetic checks cover deterministic work/pixels between repeats, all workspaces
visited, selected-surface freshness, pending-work fairness, actual background
redraws as well as admissions, and final quiet convergence. Tiny intermediate
previews may intentionally differ from controls. The synthetic source bypasses
PTY/VT work and drives frames itself; it cannot establish real idle behavior.

The live runner uses ordinary Cleat/PTY/VT providers with coloured mixed glyphs,
optional Kitty same-ID image replacement/deletion, and a final text marker followed
by silence while each producer remains alive. It requires all final markers,
bounded starts/snapshots/redraws, drained queues and two seconds without new frames.
Image checks require every terminal to receive a placement, at most one cached
resource per terminal, no final placements, both test colours in intermediate
captures and neither in the settled final capture. Cached unplaced assets may
remain below the normal quota. This tests image lifetime, not general fidelity.

Use `--no-render-budget` for a live admission control. Omit `--screenshots` for a
run without readback synchronization. Live transitions occur every two seconds;
they change normal window state but do not inject physical input.

## Measurement and integration

`frame_us` includes submission/presentation waits; `build_us` stops before render
submission. Timings for providers, glyphs, surfaces and windows are nested, not
additive. Cached surface pixels exclude atlases, staging and other process memory.

The live active-frame gate subtracts only the explicitly measured blocking event
wait. Event dispatch remains included. First paint is reported separately; every
subsequent frame is gated, including background initialization. The synthetic
frame gate excludes its warmup phase. Neither measures physical input-to-photon
latency. Readbacks occur outside the frame timer but can affect subsequent frames.

Instrumentation is separate from the overview scenario:

- `RD_FrameMetrics` collects shell timings and surface counts. A collector attaches
  caller-owned storage through `RD_State.frame_metrics`, resets it at frame start
  and sets `begin_us`. A null pointer disables collection.
- `UIShell_TerminalMetrics` collects provider and glyph work for terminal views,
  independently of fixture tags. Its optional collector has the same storage and
  reset lifetime. These counters do not control rendering policy.
- `RD_FrameReplay` optionally supplies a fixed timestep, suppresses incidental input
  and prepares window state before UI construction. Zero fields preserve normal
  interaction. It contains no overview-specific actions or output paths.
- `uishell_overview_benchmark.*` owns fixture creation, synthetic terminal updates,
  the transition script, invariant checks, native readbacks and CSV export. The
  synthetic feed remains an explicit test path; it is not a new provider abstraction.

The overview collector attaches for the process lifetime. A future temporary
collector must detach before releasing its storage. Normal view/UI lifetimes run
even when surface rendering is deferred. CI runs native preview diagnostics with
budgets both enabled and disabled; machine-dependent timing gates run locally.

## Evidence and outstanding work

Local artifact paths below refer to the investigation worktree and are not checked
in. Detailed exploratory notes remain in Git history before their consolidation.

| Finding | Evidence |
| --- | --- |
| Original jank was dominated by Wheelhouse glyph construction | 45-second live sample: at least 73% of main-thread samples; Andamento under 1%. Debug C build. |
| Compiler optimization helps but does not remove scaling | Warm 48-grid CPU-only draw: 224 ms at `-O0`, 28 ms at `-O2`, with the same debug Rust libraries. Not a frame-time comparison. |
| Temporal reuse reduces work | Same-binary 48-terminal replay: 1,606 → 699 rebuilds; 5.44M → 2.39M rebuilt cells; 1,427 updates unchanged. `local/refresh-control`, `local/refresh-budget`. |
| Demand sizing reduces cached surfaces | 52.22M → 10.95M pixels. Sizing alone did not remove stalls. `local/transition-boundaries`, `local/transition-sized`. |
| Provider admission catches real update bursts | Matched live run: 429.46 ms maximum active frame without admission, 26.54 ms with it. Both became idle. `local/live-release-v2-control`, `local/live-release-v2-on`. |
| Layout changes could bypass the render queue | Actual-redraw assertion failed before layout-version tracking; 181 ms maximum. `local/live-redraw-gate-red`. |
| Layout fix bounds subsequent transitions | Selection/expansion/return/resize below 35 ms; redraw, image, final-content and idle checks pass. `local/live-layout-admission`, `local/live-layout-confirm`. |
| Startup is still above the 50 ms gate | Second frame 64.24 / 60.62 ms; first paint 204.82 / 195.49 ms in those runs. These runs fail the timing gate. |

A targeted probe measured 47.74 ms inside macOS event processing on the second
frame, versus 0.13 ms on the third (`local/live-init-events`). Sampling shows
AppKit activation/menu-bar handling and Core Animation transaction work. The
probe was removed; the timing gate has not been relaxed to exclude that work.

All nine native diagnostics and the policy-disabled preview control pass
(`local/layout-diagnostics`). Synthetic 1/16/48 transitions pass the 50 ms gate
(`local/layout-synthetic`); expanded and drained pixels match the control exactly
(`local/layout-control`). These establish bounded preview work, not universal
latency or large-installation memory bounds.

Next work: isolate the startup event cost, apply demand-aware consumption to
Jackstay, and measure damage-granular terminal caching before adopting it.
Multiple simultaneous overview windows and large-image streams need separate
workloads. The captures do not justify changing the Ghostty fork or font renderer.
