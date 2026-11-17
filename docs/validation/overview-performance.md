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
| Terminal preview glyph buckets | Rebuilt once the preview's surface has drawn the last build, so surface admission sets the rate; foreground and full-resolution workspaces bypass this. |
| Jackstay preview frame uptake | A new frame once the preview's surface has drawn the last, so surface admission sets the rate; foreground and full-resolution transitions bypass this limit. |

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
ongoing changes are not bounded by the version-based queue. Jackstay and web
Views version their image and the chrome drawn from producer state; before a
session exists they remain unversioned. A preview takes a mailbox frame, and
uploads or imports it, only after its surface has drawn the previous one. Its
rate is therefore the rate at which admission draws it, with no fixed cadence.
Terminal previews follow the same rule for their glyph buckets: a content
change is built once the surface has drawn the previous build, and the preview
keeps requesting frames until it has been. Preview size sets no rate.

Intermediate frames replace each other in the existing latest-frame mailbox.
Unsampled native frames retain that mailbox's immediate-release behavior. The
displayed native frame stays leased until replacement, with the normal GPU
completion fence. Preview sampling does not alter the producer's logical viewport.
A pending final image requests further shell frames until it is taken, so a
producer that then goes quiet cannot leave its preview stale indefinitely.

`--preview_diagnostics` publishes 60 changing CPU images through the real mailbox
and builds the Jackstay preview before a terminal fixture using the production
panel builder. It stands in for render admission and checks that a queued
preview takes nothing until drawn, then the newest image; preserved text events,
final-image convergence after the producer goes quiet, foreground bypass and
display admission. This is a work-admission regression, not a
measurement of physical keyboard latency or a native producer integration test.
Producer-side cadence, resolution and consumer-demand aggregation remain a
[cross-stack follow-up](../design/jackstay-presentation-demand.md).

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

### Damage-granular terminal caching (#750)

Measured on 2026-11-17 for #103 on an Apple M4: release builds, 48
workspaces, all three budgets. Before is `caa283d1`, the parent of #750, and
after is `ccdeef84`, #750 itself. The two share pins. Both binaries carry the
harness and overview fixes from the same change, without which neither replay
runs on the camera overview.

The default overview, rows, slides each group sideways, so a settled replay
draws only one to five of the 48 terminals and dilutes per-terminal costs.
These numbers use the grid with the whole of it fitted:

```sh
python3 tools/benchmark-overview.py --output local/overview-grid --counts 48 \
  --overview-view-mode grid --overview-readable-width 0 \
  --preview-refresh-budget --preview-surface-budget --preview-render-budget
```

Settled phases then draw all 48 terminals. Work counts and pixels come from
one run with screenshots (two repetitions). Timings come from three further
interleaved rounds without screenshots: six samples per phase per binary,
reported as medians, with glyph time as the range.

| Phase | Cells built | Glyph time per phase, median (range), ms | Build p95 (ms) |
| --- | --- | --- | --- |
| open | 477,750 → 389,753 | 27.4 (22.8–45.7) → 40.7 (33.5–54.9) | 19.6 → 20.5 |
| select | 188,370 → 9,919 | 11.5 (9.2–23.6) → 7.0 (4.4–8.0) | 21.3 → 19.8 |
| expand | 40,950 → 1,365 | 3.6 (3.3–4.8) → 1.7 (1.2–3.7) | 18.6 → 17.9 |
| return | 611,520 → 399,490 | 31.9 (20.7–40.7) → 32.3 (28.2–59.4) | 20.0 → 19.7 |
| resize_small | 302,720 → 124,416 | 18.6 (13.4–25.2) → 31.0 (25.4–38.6) | 19.6 → 18.7 |
| resize_large | 573,300 → 229,138 | 32.0 (28.2–41.5) → 40.4 (30.6–51.3) | 20.7 → 22.9 |
| drain | 98,280 → 4,004 | 5.4 (4.4–7.2) → 2.7 (1.8–3.7) | 16.9 → 17.8 |

- All eight phase checkpoints match before and after byte for byte, in both
  repetitions.
- Where terminals are on screen and only their cursor or rows change, cells
  built fall to the damaged rows: drain ×24, select ×19, expand ×30. Glyph
  time there roughly halves.
- Where every row of many terminals is rebuilt (open, the resizes), glyph
  time rises 25–65% although fewer cells are built; resize_small's ranges
  don't overlap. Rebuilding through the row cache costs more per row than the
  old full draw did. That is the next cost to take out of this path: a full
  rebuild should cost no more than it did before.
- Build p95 doesn't separate the builds. At the replay's 60 Hz it sits on
  frame pacing, so glyph time is the CPU measure here.
- In the rows overview (the default, a few terminals settled) the pattern is
  the same and smaller: select glyph time 26.2 → 36.5 ms, drain 0.5 → 0.2 ms.
- Repeats of one binary sometimes differ in `rect_instances`,
  `surface_allocations` or, once on the after build, cells built. This
  happens before and after alike, so the replay is not fully deterministic
  under timing.

Harness fixes: the replay checked that the overview opened through
`workspace_zoom_t`, which only the cover flow (`DEV_coverflow_overview`)
eases. The frame CSV now reports the camera overview's open state, which it
writes into `workspace_zoom_open`. The grid replay also found that a grid
setting flew the camera on the first frame while the overview was closed,
drawing the workspace through the scene before its surface existed. A new
layout while at rest on the workspace is now a cut.

### Row cache rebuild cost

A profile of the fitted-grid replay on main put the row cache's own work
above the drawing it caches. Of 136 samples under the row-cache draw:
- `dr_append_bucket` took 64, copying instances one push at a time;
- releasing and reserving an arena per rebuilt row took about 39;
- building the rows took 53.

The fix copies appended batches with one `memcpy` each, and hands the
arenas of dropped rows to the rows being built. Measured against #750's
parent, which carries #750's arena fix so it doesn't crash on macOS: three
interleaved rounds, six samples per phase per binary, each binary pinned to
its own Rust libraries.

| Phase | Glyph time before #750, median (range), ms | With the fix, median (range), ms |
| --- | --- | --- |
| open | 24.4 (23.5–26.6) | 36.2 (33.3–45.5) |
| select | 8.9 (8.6–10.3) | 4.6 (3.3–5.5) |
| expand | 3.3 (1.9–5.8) | 1.2 (1.0–1.3) |
| return | 27.6 (21.1–34.2) | 25.2 (19.1–27.5) |
| resize_small | 17.6 (14.0–21.0) | 14.6 (14.0–17.9) |
| resize_large | 30.5 (26.0–46.6) | 23.4 (18.7–26.3) |
| drain | 4.6 (4.3–5.5) | 2.3 (1.7–2.9) |

- Every phase but open now costs no more than before #750, and most cost
  less.
- Open draws 48 terminals for the first time. It still pays for building
  each row in its own bucket and copying it into the frame: in the profile,
  `memmove` is 19 of 99 row-cache samples. The old full draw wrote straight
  into the frame. Appending a row's batches by reference rather than by copy
  would remove that.
- `powf`, which converts each cell's text colour, shows up at about 8% of
  the row-cache samples. It was there before #750 too.

Next work: isolate the startup event cost, apply demand-aware consumption to
Jackstay, and take the copy out of the row cache's first draw.
Multiple simultaneous overview windows and large-image streams need separate
workloads. The captures do not justify changing the Ghostty fork or font renderer.
