# Size-aware terminal preview refresh experiment

Current status: the three preview policies are enabled by default following
hands-on acceptance. Binary opt-outs are `--no_preview_render_budget`,
`--no_preview_surface_budget` and `--no_preview_refresh_budget`. The experiments
below record the earlier opt-in stages. The synthetic runner still selects each
policy explicitly and disables unselected policies for reproducible controls.

This is an opt-in experiment for [#103](https://github.com/flotilla-org/wheelhouse/issues/103),
measured with the [overview replay](overview-replay.md). Normal startup behavior
is unchanged. Enable `--preview_refresh_budget` on Wheelhouse, or
`--preview-refresh-budget` on the replay runner.

## Policy

Unselected offscreen terminal previews use the existing quantized displayed-width
demand to set a refresh interval:

| Preview demand | Interval |
| --- | --- |
| Up to 128 points | 200 ms |
| Up to 256 points | 100 ms |
| Wider, active/composited, or selected overview workspace | No added delay |

The selected overview workspace remains full-rate because it can expand into
the active presentation. Geometry, fonts, selection and raster changes refresh
immediately. Previews containing image placements, or whose previous draw held
image placements, bypass the budget; this avoids retaining draws referencing
replaced/deleted image textures. Image-specific refresh policy is deferred.

Terminal updates still reach the cell/image caches normally. Only rebuilding
terminal draw commands waits. Surface content identity follows the retained draw
commands actually displayed, not the latest producer generation. Dirty previews
request further frames until their refresh deadline, including when the last
producer update is followed by silence. Deadlines have a stable per-workspace
phase to spread work rather than synchronize all thumbnails on one burst.
Intervals are targets with frame scheduling granularity, not hard wall-clock
latency guarantees when the UI thread or system is blocked.

The image-cache render generation advances on every provider update, even when
there are no images. The refresh decision therefore normalizes that field for
text-only previews and separately excludes image-bearing draws. The replay now
applies the image-cache update path too; omitting it would produce a misleading
benchmark success while live terminals never deferred.

This changes neither terminal dimensions nor producer output, and does not skip
workspace UI lifetimes. It also does not reduce provider cell-grid copies, surface
allocation sizes, or cold creation of many workspaces. It is a temporal level of
detail experiment; spatial texture budgeting and damage-granular drawing remain
separate steps.

## Optimized-build control

With only C compilation changed from -O0 to -O2 (same debug libraries and
BUILD_DEBUG), the original replay's busy 48-workspace steady phase fell from
41.9–42.6 ms build p95 to 8.7–9.5 ms. Initial overview entry remained noisy and
expensive: optimized runs reached 121 and 183 ms p95 in that phase. These data do
not establish a cold-entry improvement. Local artifacts: `local/replay-O2`.

## Matched refresh comparison

Both sides used the same optimized binary, with the flag off/on. The final phase
now stops producer updates at virtual frame 210, so deferred content must catch
up without further source changes. The image-cache consumer correction is also
present on both sides. Do not directly compare whole-run totals against the
older continuously-updating final phase.

| Workspaces | Rebuilds, control → budget | Rebuilt cells, control → budget |
| --- | --- | --- |
| 1 | 4 → 4 | 13,103 → 13,103 |
| 16 | 542 → 324 | 1,838,540 → 1,128,869 |
| 48 | 1,606 → 699 | 5,441,220 → 2,388,817 |

Counts matched across two repetitions of each size. Producer updates were
unchanged (483 for 16 workspaces; 1,427 for 48). Surface allocations also stayed
unchanged (114 and 338). In the 48-workspace selection phase, build p95 was
7.16/7.12 ms without the policy and 4.77/4.86 ms with it. The 16-workspace timing
comparison was mixed: 3.79/3.96 ms versus 4.90/3.73 ms. Reduced deterministic work
is established; an across-the-board latency improvement is not.

Expanded-workspace and final-drained screenshots matched the control exactly for
all sizes and repetitions. Intermediate tiny previews intentionally differ while
updates are active. Every checkpoint and work count matched between repetitions
of the same variant. Artifacts: `local/refresh-control` and `local/refresh-budget`.

All seven existing native diagnostics passed with the flag enabled. A further
16-workspace run exercised the runner's explicit checkpoint comparison against
the control. This replay is frame-driven; it does not independently prove the
real application's idle wake scheduling or physical input latency. Live acceptance
should cover those before enabling the policy by default.

```sh
python3 tools/benchmark-overview.py --output local/control
python3 tools/benchmark-overview.py --output local/budget \
  --preview-refresh-budget --baseline local/control/summary.json \
  --match-checkpoints expand drain
```

The runner's default 15% timing regression gate can flag noisy cold-entry results;
inspect phase-level measurements rather than discarding those failures. Checkpoint
matching is explicit so a temporal-detail experiment can require convergence and
active-view fidelity without requiring every intermediate thumbnail to be current.
