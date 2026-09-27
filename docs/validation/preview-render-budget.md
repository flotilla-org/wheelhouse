# Bounded workspace preview rendering

Current status: the three preview policies are enabled by default following
hands-on acceptance. Binary opt-outs are `--no_preview_render_budget`,
`--no_preview_surface_budget` and `--no_preview_refresh_budget`. The experiments
below record the earlier opt-in stages. The synthetic runner still selects each
policy explicitly and disables unselected policies for reproducible controls.

Follow-up to [transition measurements](overview-transition-cost.md). The first
scheduler experiment is opt-in with `--preview_render_budget` (runner spelling:
`--preview-render-budget`). It is independent of the temporal terminal bucket
policy and demand-sized surface policy.

## Scheduling boundary

All demanded materialized workspaces still build their ordinary UI. The subsequent
[live-provider work](overview-live-providers.md) also bounds background provider
starts and snapshot requests; selected terminals remain immediate. After layout, the shell admits at most four pending offscreen
workspace surfaces per frame. Pending means a missing/invalid surface, a changed
size, or a changed declared content version. Workspace layout and animation
state are folded into that version after layout, so chrome movement uses the same
admission queue as terminal content. The oldest last-rendered workspace
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

## Interactive acceptance

`--overview_benchmark_interactive` keeps the isolated fixture window alive, allows
normal input and uses wall-clock producer ticks and refresh deadlines. It requires
the same fresh explicit user/project files as the scripted fixture. It does not
launch PTYs or connect to Flotilla. The synthetic source requests frames itself;
this mode cannot prove real producer wake behavior or idle parking.

An isolated app bundle and fresh fixture are in `local/Wheelhouse Preview.app` and
`local/schedule-interactive-app`. Computer-use recognized the app but repeatedly
returned `cgWindowNotFound`. Robert confirmed the isolated 48-terminal window was
visible and responsive while trying overview transitions and resizing. The daily
driver has not been replaced or restarted.

Provider-driven wakeups, quiet-state drain and live transitions are now covered
by the [live-provider check](overview-live-providers.md). Mixed glyph/image pixels
and image deletion are covered by its readback checks. The four-surface
limit is a bounded first policy, not an adaptive GPU-time scheduler.


## Default-on verification and remaining startup hitch

The policies now default on, with explicit opt-outs for comparison. Further live
image testing exposed two costs that the admission counters alone did not catch:

- Terminal image uploads used Metal's synchronous static upload path. Dynamic
  textures avoid waiting behind previously submitted GPU work when a new image
  generation arrives.
- Workspace chrome geometry changed while producer versions stayed unchanged.
  The renderer redrew those surfaces outside the admission queue. Folding layout
  and animation into admission fixes that mismatch. Both replay runners now
  assert actual background redraws, in addition to admissions, never exceed four.

`local/live-redraw-gate-red` demonstrates the failing redraw assertion before the
layout fix (181 ms maximum active frame). `local/live-layout-admission` and
`local/live-layout-confirm` pass the redraw, image lifecycle, final content and
idle checks. Their selection/expansion/return/resize maxima are below 35 ms.
They still **fail** the 50 ms post-first-paint timing gate: the second frame takes
64.24 and 60.62 ms respectively. First paint is 204.82 and 195.49 ms.

A targeted startup probe (`local/live-init-events`) measured 47.74 ms inside
`wm_get_events` on the second frame, versus 0.13 ms on the third. A separate sample
(`/tmp/wh-init-profile.sample`) shows AppKit activation/menu-bar handling and Core
Animation transaction work in event processing. This startup cost remains open;
the timing gate has not been relaxed or made to exclude it. The temporary probe
was removed after measurement.

The 1/16/48 synthetic replays in `local/layout-synthetic` pass a 50 ms transition
gate and the actual-redraw bound. All nine native diagnostics and the explicit
preview-policy opt-out control pass (`local/layout-diagnostics`). These results
establish bounded preview work, not a guarantee of 60 Hz or bounded OS event cost.

The expanded and final quiet screenshots match the render-budget-disabled control
exactly at 1/16/48 terminals (`local/layout-control`). Timing comparisons from
that control run are not used as a performance gate.
