# Repeatable overview rendering workload

This is the rendering regression loop for [#103](https://github.com/flotilla-org/wheelhouse/issues/103).
It complements the [initial draw-cost investigation](overview-render-cost-2026-09-27.md).
It runs in a separate process with fresh user/project files and does not attach
sessions or connect to Flotilla. The initial native driver is macOS-only.

## Run and compare

Build Wheelhouse with the usual pinned dependencies, then run from the checkout:

```sh
python3 tools/benchmark-overview.py --output local/overview-before
```

The default runs 1, 16 and 48 workspaces twice each. The output directory must
not exist; results are never overwritten. `summary.json` contains per-phase
percentiles, work counts, checkpoint hashes and the binary SHA-256. Each run
keeps `frames.csv`, a process log, isolated configuration, and eight PPM images.
PPMs show the actual composed Metal stage, not a separate replay of draw passes.

After making a change and rebuilding:

```sh
python3 tools/benchmark-overview.py --output local/overview-after \
  --baseline local/overview-before/summary.json --max-regression-percent 15
```

The runner exits nonzero for replay failures, differing pixels/work counts
between repetitions of the same binary, or the requested performance budget.
It does not require before/after work counts to match: reducing them is the goal.
Compare before/after checkpoint images when changing rendering. Timing limits
are explicit local budgets, not portable CI thresholds. Keep hardware, window
scale, fonts, build flags, and competing load consistent.

For a short single-size run or an absolute CPU-build budget:

```sh
python3 tools/benchmark-overview.py --output local/overview-48 \
  --counts 48 --repeats 2 --max-build-p95-ms 16.7
```

`--no-screenshots` removes checkpoint readback; comparisons must use the same
setting. Screenshot work is outside frame timing, but synchronizes the GPU and
can affect subsequent scheduling. Use the no-screenshot mode for additional
performance confirmation after visual validation. Each process has a timeout.

## Workload and phases

The replay injects real Cleat render-update structures into Wheelhouse's normal
cell-cache consumer. Terminal provider creation/polling is bypassed only for
explicit benchmark views. The normal terminal font lookup, retained draw buckets,
workspace layout, overview animation, surface cache and GPU submission execute.

Workspaces repeat four kinds of output:

- Static text, with a hidden cursor.
- Cursor-only updates every 30 virtual frames.
- One row replaced every six virtual frames.
- Scrolling and a new bottom row every two virtual frames.

The single-workspace control is static. Text, colour, cursor position and update
versions derive from workspace index and virtual frame. Catching up a scrolling
view uses a canonical full grid if intermediate updates were not consumed.
Actual window resizing changes terminal grid dimensions, as with a live view;
preview scale does not change the replay's logical terminal dimensions.

The animation timestep is fixed at 1/60 second; replay runs as fast as the normal
frame loop permits. The 240-frame script has eight 30-frame phases:

| Phase | Action |
| --- | --- |
| warmup | Selected workspace at normal size; cold startup excluded from budgets. |
| open | Open overview. |
| select | Select the last workspace while staying in overview. |
| expand | Expand that workspace through the normal zoom animation. |
| return | Reopen overview, selecting the first workspace. |
| resize_small | Resize native content area to 900 × 600 points. |
| resize_large | Restore 1200 × 800 points. |
| drain | Publish one final update, then keep overview open with no further output. |

Actions set the same window state as overview controls. They do not simulate
physical clicks. The benchmark suppresses incidental UI events/mouse hover in
its own process. Do not interact with its windows while it is running.

## Measurements and limits

`frame_us` measures the frame call through normal submission/end-frame. It can
include driver/presentation waits; it is not isolated GPU execution time or a
physical input-to-photon latency measurement. `build_us` stops immediately before
render submission. `action_frame_ms` reports the first frame of each phase, not
keyboard/mouse input latency.

Work counters include retained-bucket rebuilds, grid cells in rebuilt buckets,
terminal visits, consumed replay updates, workspace surface allocations, and the
sum of currently cached workspace-surface pixels. Cell counts describe rebuild
input size, not the number of final glyph quads. Surface pixels exclude font
atlases, depth/staging resources and other process memory; they must not be
reported as total GPU bytes or process footprint.

Checks require all 240 frames, every workspace rendered, successful overview
entry/exit and resizing, continued dynamic updates, no settled static rebuilds,
final quiet-state convergence, valid nonblank checkpoints, and identical workload counts and checkpoint bytes
between repetitions. Screenshot equality applies on the same host/build, not
across renderers or font configurations. Budget violations give the loop a
repeatable failure signal without changing production caching to suit the test.

This fixture deliberately excludes PTY parsing, remote transport, asynchronous
producer timing and real input dispatch. Fixed virtual-time replay can stretch
in wall time under load; it does not model backlog from a real-time producer.
Add a live-source acceptance layer when testing that boundary. Likewise, this
ASCII workload does not replace emoji/image correctness diagnostics or a
representative mixed-glyph performance case.

## First measured baseline, 27 September 2026

These numbers precede the final-phase silence and image-cache-consumer additions.
Use matching new runs for comparisons; the updated control and refresh experiment
are recorded in [preview refresh budget](preview-refresh-budget.md).

On the current macOS host, with the C debug build and existing pinned libraries:

| Workspaces | Worst phase build p95, run 1 / run 2 | Total rebuilds per run |
| --- | --- | --- |
| 1 | 2.33 / 1.66 ms | 4 |
| 16 | 17.84 / 17.87 ms | 614 |
| 48 | 80.11 / 79.77 ms | 1822 |

All eight images and all per-phase update/build/allocation counts matched between
repetitions for each size. The 16-workspace overview checkpoint was visually
inspected. Results are in the investigation worktree's `local/replay-final`.

A separate `--max-build-p95-ms 0` run returned exit status 1 with budget failures,
confirming that the runner can fail a performance threshold. All seven existing
native diagnostics (sidebar, scroll region, preview, tooltip, panel, managed
content and terminal glyphs) passed with benchmark mode disabled.

Next experiments should change one variable at a time: optimized C build,
size-aware refresh cadence, then damage-granular storage/drawing. Preserve the
checkpoint images and the static reuse control while reducing rebuild work.

Transition follow-up: [submitted-frame stalls and demand-sized surfaces](overview-transition-cost.md).
Use `--max-frame-ms` to catch individual submission/presentation stalls that CPU
build percentiles miss. `--preview-surface-budget` enables the separate spatial
experiment; both preview policies remain opt-in.

The [render admission experiment](preview-render-budget.md) adds
`--preview-render-budget`, interrupted initialization (`--interrupt`), and an
all-scrolling workload (`--busy`). Extra frame-31/33/37 checkpoints record partially
initialized and interrupted transitions. The runner checks admission limits,
selected-surface freshness, queue fairness and final drain as well as pixels.
