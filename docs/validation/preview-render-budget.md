# Bounded workspace preview rendering

Follow-up to [transition measurements](overview-transition-cost.md). The first
scheduler experiment is opt-in with `--preview_render_budget` (runner spelling:
`--preview-render-budget`). It is independent of the temporal terminal bucket
policy and demand-sized surface policy.

## Scheduling boundary

All demanded materialized workspaces still build their ordinary UI. The subsequent
[live-provider work](overview-live-providers.md) also bounds background provider
starts and snapshot requests; selected terminals remain immediate. After layout, the shell admits at most four pending offscreen
workspace surfaces per frame. Pending means a missing/invalid surface, a changed
size, or a changed declared content version. The oldest last-rendered workspace
is admitted first, with inventory order breaking ties. Newly selected, expanding
and directly composited workspaces bypass the queue.

An unversioned view retains the existing byte-hash rendering path once its surface
has been initialized. Its changing content is not subject to this version-based
budget. This first policy targets terminal previews; it does not promise to bound
arbitrary unversioned custom drawing or the cost of one very large workspace.

Deferred subtrees are skipped during the draw traversal, after their UI lifetime
work has run. Neither their nested surfaces nor their draw callbacks submit work.
Their completed surface pixels remain valid, and pending work requests another
frame. A shared UI traversal helper computes the sibling/ancestor continuation;
normal parent clip/transform stacks still unwind. No terminal dimensions or
PTY processing cadence change, and no deferred draw refers to released image handles:
the retained object is completed pixels, not a postponed image draw command.

The overview uses retained small previews when the workspace's full wrapper
surface is absent. If no preview has ever been produced, the labelled tile is
empty until admitted. Crop coordinates travel with the completed wrapper pixels,
so a resized layout does not reinterpret an old surface with its new crop.

## What changed the result

Limiting only initialization removed the initial burst, but selecting another
workspace could still create a synchronized burst of changed terminal previews.
Temporary Metal timing instrumentation measured one such GPU submission at
79.2 ms. The instrumentation has been removed. Bounding pending content updates
as well as initialization eliminated that reproduced stall.

Same optimized binary, temporal and spatial policies enabled on both sides:

| Worst submitted frame, 48 workspaces | No render queue, runs 1 / 2 | Render queue, runs 1 / 2 |
| --- | --- | --- |
| Open overview | 61.75 / 73.77 ms | 15.41 / 15.06 ms |
| Select workspace | 56.84 / 54.86 ms | 12.59 / 13.16 ms |
| Reopen overview | 109.69 / 101.52 ms | 8.58 / 8.52 ms |
| Resize smaller | 257.26 / 215.28 ms | 6.06 / 6.11 ms |

Artifacts: `local/schedule-current-control` and `local/schedule-current-on`.
Screenshot readbacks synchronize GPU work outside the measured frame, so also
check no-screenshot runs. The earlier no-screenshot scheduled runs had worst
non-startup frames of 15.65 / 17.24 ms (`local/transition-scheduled`). Timing varies
with other machine load; these are local observations, not portable guarantees.

## Verification

Repeated screenshot replays at 1, 16 and 48 workspaces passed. Expanded and final
quiet-state screenshots match the same-binary control exactly; all checkpoints
and workload counters match between repetitions of each policy. A previous
comparison against an older control found differences confined to window chrome;
regenerating both variants with the same current binary removed that confound.

`--interrupt` changes selection and reverses zoom while the initial preview queue
is still filling. Both 48-workspace repetitions passed, with worst submitted
frames of 15.39 / 15.07 ms (`local/schedule-interrupt`). Additional captures at
frames 31, 33 and 37 record partial initialization. The frame-31 image was inspected:
uninitialized tiles retain their workspace labels and borders.

`--busy` scrolls every fixture terminal. Combined with interrupted transitions,
two no-screenshot runs passed a 50 ms individual-frame gate, with maxima of
16.03 / 15.34 ms (`local/schedule-busy`). Runtime assertions check selected surfaces
are current and no continuously pending preview exceeds a full admission round
plus two frames. Runner checks enforce the admission count and final queue drain.

The latest native sidebar, scroll-region, preview, tooltip, panel, managed-content
and terminal-glyph diagnostics are in `local/render-budget-diagnostics`.

```sh
python3 tools/benchmark-overview.py --output local/budget-check \
  --counts 48 --repeats 2 --preview-refresh-budget --preview-surface-budget \
  --preview-render-budget --interrupt --busy --no-screenshots --max-frame-ms 50
```

## Interactive acceptance remains open

`--overview_benchmark_interactive` keeps the isolated fixture window alive, allows
normal input and uses wall-clock producer ticks and refresh deadlines. It requires
the same fresh explicit user/project files as the scripted fixture. It does not
launch PTYs or connect to Flotilla. The synthetic source requests frames itself;
this mode cannot prove real producer wake behavior or idle parking.

An isolated app bundle and fresh fixture are in `local/Wheelhouse Preview.app` and
`local/schedule-interactive-app`. Computer-use recognized the app but repeatedly
returned `cgWindowNotFound`. Real mouse-driven transition acceptance is therefore
not yet established. The daily driver has not been replaced or restarted.

Provider-driven wakeups, quiet-state drain and live transitions are now covered
by the [live-provider check](overview-live-providers.md). Before enabling these
policies by default, verify real input and mixed glyph/image pixels. The four-surface
limit is a bounded first policy, not an adaptive GPU-time scheduler.
