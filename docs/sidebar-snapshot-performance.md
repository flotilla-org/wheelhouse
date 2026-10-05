# Native sidebar snapshot performance

Measured on Linux/Xvfb with a release (`-O2`) Wheelhouse build, Andamento
`9718ba1`, Cleat `e3465d7` (no Ghostty feature), and Jackstay `91156bf`.
The recovered starting point is Wheelhouse `e44e280`.

Every native run used `prlimit --as=8589934592` (8 GiB address-space cap)
and GNU `time -v`. Catalog sizes increased from 100 through 300 to 1,000
only after RSS plateaued. No uncapped reproduction of the original OOM was run.

## What grew

The recovered benchmark builds 960 UI frames inside one application update.
Normal updates call `fnt_frame()`; the recovered benchmark omitted that boundary.
Font runs containing tabs are deliberately not cached, so they accumulate in the
font frame arena without the boundary. At 100 issues, the arena grew from
4,989,769 bytes at frame 40 to 26,472,969 at frame 240 in idle mode;
drawing and persistent UI storage stayed constant. The bounded-storage assertion
failed for all four combinations of panel layout and input. Peak RSS for that
corrected-layout, unfixed-lifecycle run was 230,776 KiB. This demonstrates real
runaway benchmark growth; it does not establish the exact allocation history of
the prior vessel's reported 55 GB process.

Calling `fnt_frame()` before each simulated frame fixes that growth. The drawing
arena also now reserves 64 MiB initially, growing in blocks, rather than reserving
64 GiB upfront: the latter prevented an 8 GiB-capped process from starting.
On the 32-core measurement host, CPU-sized stripe arenas also nearly exhausted
the virtual-memory budget despite low RSS. The runner interposes `get_nprocs()`
to size application workers/stripes for one CPU, uses one async worker, and sets
`MALLOC_ARENA_MAX=2` and `LP_NUM_THREADS=1`. These controls apply equally to both
lookup modes and do not change sidebar UI work. They do not pin execution to a
physical CPU. The interposer affects dynamically resolved (PLT) calls; it cannot
replace statically bound or inlined CPU discovery. `SIDEBAR_CONFIG` logs the
effective worker/stripe CPU count; the runner stops if it differs from one. GPU presentation and pacing are excluded.

## Method

The production daily-driver configuration and `data/sidebar/fixture.jsonl` are
loaded through the real Andamento ABI, then ten projects and the stated number
of synthetic attention-bearing issues are added. The resulting snapshots have
229, 629, and 2,029 nodes. Four docked sections render through the production
control-surface/panel host. The merged arrangement pairs Projects/Sessions and
Attention/Git, with **Projects and Attention selected**, rather than measuring
only the cheap tabs. Both expensive sections must have nonzero viewports.

Each combination runs 240 frames: 40 warmup and 200 measured builds. Precise
scrolling sends 0.25-pixel events to Projects every measured frame. The final
Projects offset must be 50 pixels and Attention must remain at zero. Idle offsets
must stay zero. Viewport heights are 560/168 pixels (Projects/Attention) with four
panels and 536/144 pixels with merged panels. Existing docking diagnostics retain
coverage of manual sizing, reset, independent scrolling, and serialization.

The timer covers UI construction/layout, including the instrumented snapshot and
context analysis. It excludes metric output, font-frame retirement, GPU submission,
and pacing. Font storage may vary by up to one MiB after warmup; the runner requires
post-warmup RSS growth at most two MiB per combination. The tests operate on real
native code and fail when the omitted font boundary is restored.

## Measurements

Each value is the mean of 200 builds, in milliseconds. Linear mode disables only
the label lookup; both modes include the memory fix. These are single paired runs
on a shared host, so total-frame differences include scheduling noise. The directly
instrumented context timings give the clearest comparison.

| Issues | Panels | Input | Frame linear | Frame lookup | Analysis linear | Context linear | Context lookup |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 100 | four | idle | 1.053 | 0.928 | 0.013 | 0.010 | 0.005 |
| 100 | four | precise | 1.257 | 1.051 | 0.014 | 0.011 | 0.006 |
| 100 | merged | idle | 1.201 | 0.978 | 0.014 | 0.010 | 0.005 |
| 100 | merged | precise | 1.013 | 1.009 | 0.011 | 0.008 | 0.005 |
| 300 | four | idle | 2.780 | 2.928 | 0.025 | 0.094 | 0.021 |
| 300 | four | precise | 2.863 | 2.741 | 0.025 | 0.096 | 0.016 |
| 300 | merged | idle | 2.754 | 2.642 | 0.024 | 0.092 | 0.016 |
| 300 | merged | precise | 2.923 | 2.867 | 0.025 | 0.092 | 0.017 |
| 1000 | four | idle | 15.024 | 11.305 | 0.124 | 1.196 | 0.117 |
| 1000 | four | precise | 18.075 | 13.993 | 0.140 | 1.297 | 0.119 |
| 1000 | merged | idle | 15.033 | 12.649 | 0.126 | 1.182 | 0.117 |
| 1000 | merged | precise | 14.700 | 13.943 | 0.120 | 1.195 | 0.120 |

| Issues | Peak RSS linear (KiB) | Peak RSS lookup (KiB) | Largest post-warmup RSS change (KiB) |
| ---: | ---: | ---: | ---: |
| 100 | 124432 | 124132 | 488 |
| 300 | 151508 | 153044 | 488 |
| 1000 | 250528 | 249236 | 492 |

## Cache decision

Keep the snapshot-scoped identity-to-label table: at 1,000 issues it removes about
90% of context-resolution work (roughly 1.1 milliseconds per build). Identity alone
remains the lookup contract; the first non-section placement wins, including an
empty label. The table borrows snapshot-owned strings and invalidates before every
dispatch and before replacing a snapshot. Releasing state also clears the freed
label-arena pointer, allowing the existing native fixtures to reuse their state.
The C ABI exposes snapshot currency rather than a revision number, so snapshot
identity plus explicit invalidation provides the revision boundary. Font metrics
and viewport width cannot affect labels; they are deliberately absent from this key.

Do **not** add a shared geometry/animation analysis cache for #182: all analysis
across visible sections costs about 0.12–0.14 ms, under 1% of total frame time at
1,000 issues. Coalescing it could save only a fraction of that, while introducing
font/geometry/animation invalidation and section identity coupling. Each section
keeps its own scroll and sizing state. No hover-card relation cache is included.

## Reproduction and verification

Build release mode, start an Xvfb display, then run:

```sh
WHEELHOUSE_CLEAT_FEATURES=none ./build.sh wheelhouse release
xvfb-run -a python3 tools/benchmark-sidebar.py
```

The runner caps every process, saves raw logs under `build/sidebar-benchmark`,
checks bounded RSS at each size, and stops immediately on a failure. An alternative
GNU time path can be provided with `--time`; increase `--timeout` on slower hosts.
Timeouts terminate the entire process group and preserve partial logs; `--sizes 100 300` is a shorter run.
Automating the full memory benchmark in CI is tracked in
[Wheelhouse #199](https://github.com/flotilla-org/wheelhouse/issues/199); the existing
CI native sidebar diagnostics already cover label correctness.
Release mode is necessary: the debug arena inspection table reserves 256 GiB.

Native context diagnostics cover generated hash collisions, duplicate placements,
section exclusion, first empty labels, missing/empty identities, empty snapshots,
repopulation, invalidation with the same snapshot identity, and release/reuse.
The benchmark also exercises real-core failed dispatch, current-revision refresh,
label-changing refresh, and restoration of the original catalog.

The existing native sidebar diagnostics pass (precise scroll, Git, chip state,
project motion, reveal, focus, retry, restore and ended retention), as do the
27 native-ABI tests, fixture embedding test, and headless docking validity test.

Mutation check: allowing duplicate placements to overwrite the first label fails
`sidebar context labels`; omitting refresh invalidation fails the real-core
snapshot lifecycle check. Both mutations were reverted. The bounded font-storage
check also fails against the recovered implementation before the lifecycle fix.
