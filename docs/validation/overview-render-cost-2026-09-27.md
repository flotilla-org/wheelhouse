# Overview terminal rendering cost, 27 September 2026

## Live observation

Robert reported jank with many agents in the workspace overview. Sampled PID
24166, built from a97e919, without restarting or changing the running instance.
The local build helper selects the default debug build: C at -O0, debug Rust
libraries. Cleat supplies VT state; Wheelhouse's glyph renderer supplies drawing.

A 10-second sample put 6408/7431 main-thread samples (86%) under the terminal
cell-feed drawing function. A subsequent 45-second capture, while Robert made
transitions, put at least 26710/36609 (73%) there. CPU was usually 94–95% in the
longer capture, briefly 40–42% near its end. Andamento snapshot acquisition was
337/36609 samples, under 1%.

Process footprint was about 1.8 GB; top showed about 1.9 GB, falling to 1.0 GB and
returning to 1.85 GB during the transitions. The process had a 9.8 GB lifetime
peak, not a measured peak attributable to this capture. No precise transition
markers or allocation trace were collected. Sampling is not frame-time tracing.

Raw captures and benchmark logs are preserved under the investigation worktree's
`local/evidence/`. Original captures are `/tmp/wheelhouse-overview-20260927.sample.txt`,
`/tmp/wheelhouse-transitions-20260927.sample.txt`, and the latter's `.top.txt` companion.

## CPU draw-build experiment

`tools/experiments/overview-draw-benchmark.patch` is a throwaway macOS diagnostic
hook, not a production change. It calls the real cell-feed drawing function on a
200×60 printable-ASCII grid, with warm font caches and 1, 16 or 48 successive
builds. Two warmups precede ten measured repetitions. No GPU submission, Cleat
parsing, provider update copying, or overview layout is included. All grids are
explicitly rebuilt: these numbers are a workload measurement, not measured live
frame times or a claim that every live terminal changes each frame.

| Rebuilt terminals | C -O0 | C -O2 | -O0, experimental single glyph-key hash |
| --- | ---: | ---: | ---: |
| 1 | 4.742 ms | 0.522 ms | 2.962 ms |
| 16 | 74.775 ms | 9.272 ms | 46.025 ms |
| 48 | 223.860 ms | 27.925 ms | 138.644 ms |

Only the C optimization flag changed for the -O2 comparison; BUILD_DEBUG=1 and
all dependency libraries stayed unchanged. This isolates compiler optimization,
not the full release profile. A single four-U64 key hash replaced five chained
field hashes in the separate hash experiment; production source was restored.
That experiment did not receive semantic regression validation and is not ready
to merge. The baseline -O2 binary passed the existing terminal glyph diagnostics.

At raster scale 0.25, the 16-terminal measurements were 74.715 ms (-O0) and
9.012 ms (-O2); 48 terminals were 223.586 and 28.117 ms. Lower raster scale alone
did not remove the CPU grid walk. Scratch allocation was 6.05 MiB per batch of
16 draws and 18.15 MiB for 48; this is allocation volume, not retained footprint.

To reproduce in an isolated checkout with the pinned dependencies configured:

```sh
git apply tools/experiments/overview-draw-benchmark.patch
bash build.sh wheelhouse
mkdir -p local/bench
WHEELHOUSE_DRAW_BENCH=1 build/wheelhouse --user:local/bench/user --project:local/bench/project --terminal_glyph_diagnostics
```

For the isolated C comparison, use a CC wrapper replacing `-O0` with `-O2`, keeping
other arguments untouched. `bash build.sh wheelhouse release` also changes Rust
profiles and BUILD_DEBUG and therefore is a different experiment. Remove the
hook after measurement by reverse-applying the patch. Do not use this hook for
full glyph validation: the environment flag deliberately exits diagnostics early.

## Source findings and limits

- There already is a retained terminal draw bucket. Unchanged keys reuse it;
  this is not unconditional redraw of every unchanged terminal. Its key includes
  render generation, geometry, font, raster scale and selection.
- Any changed generation rebuilds the entire terminal draw bucket. Even an
  operation-free update advances the cached render generation, so cursor-only
  publication can invalidate all text drawing. We have not yet counted the live
  mix of cursor, row, full, geometry and selection invalidations.
- Partial provider updates allocate a new full cell grid and copy old cells
  before applying changes. Cleat already exposes row/scroll operations; the
  consumer does not preserve their granularity into draw-command caching.
- Overview builds all materialized workspace children. Nonselected surfaces
  use half the window backing scale, not dimensions derived from the displayed
  tile. The small retained preview is downsampled afterward.
- Surface dimensions changing releases and reallocates textures; unretained
  surfaces unused in a frame are evicted. This is a plausible contributor to the
  memory swing, not proof of its cause or of a leak.

## Proposed order

1. Validate an optimized, symbol-bearing daily-driver build against the live
   workload. Do not mask algorithmic scaling with the build-mode improvement.
2. Instrument invalidation reasons and per-workspace build time. Compare static,
   cursor-only, one-row and busy output at representative window sizes/counts.
3. Give preview demand a displayed-size and refresh budget. Tiny unreadable
   tiles can reuse their last image and coalesce refreshes; readable, hovered or
   expanding tiles get priority. Keep logical terminal dimensions stable so
   preview policy does not resize/reflow running applications. Hidden tiles
   should not demand full drawing. Bound staleness and avoid starving updates.
4. Retain row/run drawing and patch cell storage at the supplied damage
   granularity. Cursor, selection and images need explicit invalidation rules.
5. Budget preview textures against displayed pixel size, with quantized sizes
   or hysteresis during animation. Measure allocations and transition latency
   before changing lifetime policy. Keep active-view and input latency separate
   from thumbnail refresh cadence.

No Ghostty fork update is justified by these captures. Revisit that boundary if
provider parsing/update production becomes a measured bottleneck. Shared terminal
state belongs in Cleat; scheduling, preview surfaces and draw caching belong in
Wheelhouse. Coordinate any base renderer changes with RAD/rdg-dag.

[Slug](https://github.com/EricLengyel/Slug) supplies reference font-rendering
shaders. It is a later renderer experiment; replacing glyph rasterization would
not itself remove whole-grid CPU reconstruction, lookup or preview scheduling.
