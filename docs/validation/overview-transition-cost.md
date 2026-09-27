# Overview transition costs

Follow-up to the [refresh experiment](preview-refresh-budget.md), measured on
27 September 2026. The objective remains smooth interactive use with 48 terminal
previews, including entry, selection, expansion and resize. This is not achieved.

## Measure submitted frames, not only CPU builds

The optimized replay with temporal refresh budgeting can pass a 16.7 ms build-p95
limit while visibly stalling. Added `--max-frame-ms` checks every non-warmup frame
through submission/end-frame. This includes presentation waits, not only CPU
work; it is not a GPU execution timer or physical input-latency measurement.

```sh
python3 tools/benchmark-overview.py --output local/transition-frame-gate \
  --counts 48 --repeats 1 --preview-refresh-budget --preview-surface-budget \
  --no-screenshots --max-frame-ms 50
```

This returned 1: entry max 956.77 ms, selection max 200.23 ms, return max 114.01 ms.
The 50 ms limit is a local stall detector, not the final smoothness target.

Boundary counters now record cumulative surface lookup/allocation time, terminal
bucket construction time, and window-frame time. These are nested measurements;
do not add them together. In `local/transition-boundaries`, entry frames spent
373–964 ms through submission, despite only a few milliseconds building some of
those frames. Surface lookup/allocation was generally microseconds. A selection
frame took 140.58 ms building, with 3.02 ms in terminal bucket construction and
0.08 ms in surface lookup/allocation: that CPU outlier also needs further isolation.

A separate five-second process sample (`/tmp/wh-transition-sample.sample.txt`)
found 847 of 1,823 main-thread samples in Metal submission waiting for
`CAMetalLayer nextDrawable`, and another 188 waiting for an in-flight upload slot.
This establishes waiting in the submission path. It does not independently
identify GPU execution, presentation/occlusion, or resource pressure as the cause.
Raw replay artifacts are under `local/transition-sample`; the sample is also
retained at `local/evidence/transition-submission.sample.txt`.

## Demand-sized surfaces experiment

`--preview_surface_budget` makes unselected workspace surface sizes follow the
existing quantized preview-width demand instead of using half the window's
backing scale. The logical layout and terminal dimensions remain unchanged.
Selected/composited workspaces retain full resolution for their transition.
The replay runner exposes this as `--preview-surface-budget`.

With temporal refresh budgeting enabled on both sides, two no-screenshot runs:

| Measurement, 48 workspaces | Half-scale surfaces | Demand-sized surfaces |
| --- | --- | --- |
| Peak cached surface pixels | 52,219,632 | 10,948,885 |
| Entry frame maximum, run 1 / 2 | 964 / 653 ms | 655 / 537 ms |
| Return frame maximum, run 1 / 2 | 133 / 154 ms | 99 / 121 ms |

Artifacts: `local/transition-boundaries` and `local/transition-sized`. These are
noisy local observations, not a causal proof of the timing improvement. The
reduction in surface pixels is deterministic; these counts exclude other GPU
resources and must not be reported as total memory consumption. Surface sizing
alone does not solve the stalls.

The screenshot replay passed twice at 1, 16 and 48 workspaces. All checkpoint
pixels and workload counts matched between repetitions. Expanded-workspace
checkpoints matched the original control exactly; small previews intentionally
use a different sampling resolution. The 48-workspace entry checkpoint was also
visually inspected. Artifacts: `local/transition-sized-pixels`. All seven native
diagnostics passed using their normal fresh-default configuration with both flags
enabled (`local/surface-diagnostics-default`). An initial diagnostic invocation
using the replay's deliberately incomplete window configuration crashed in the
sidebar check; that configuration is intended for the replay, not these diagnostics.

## Next experiment

Bound offscreen preview work admitted per frame, starting with the initial burst
and resize. Keep valid old preview pixels while pending; show a labelled empty
preview when none exists. Selected/expanding workspaces must bypass that queue.

The boundary must preserve ordinary view lifetime, input isolation and provider
updates. Retaining a draw bucket containing released image handles is unsafe;
retaining completed surface pixels avoids that dependency. Fairness and final
quiet-state convergence must be checked explicitly, including selection of a
workspace that has not yet rendered. Nested surfaces/effects must not mark
unsubmitted content as current. Avoid implementing this by simply skipping all
workspace UI calls, because those calls also maintain view state.

The next loop should measure worst individual transition frames, not only p95,
and include partial-entry checkpoints and repeated interruptions. Live acceptance
is still required after the deterministic replay meets those conditions.
