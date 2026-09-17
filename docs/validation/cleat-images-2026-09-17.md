# Cleat compatibility and image baseline, 17 September 2026

## Revisions and artifacts

Worktree: `wheelhouse-worktrees/cleat-image-readiness`, based on Wheelhouse
`1860fd3380b0ef028cdfcefe92d82e97b7d4316c` (PR 28). The source changes in this
branch add cache diagnostics and refresh CI's pin; the daily-driver edits in the
original checkout are untouched.

The final native executable and bundle were rebuilt from commit `454d41a812ed48045c395d591dfc7ad8f8c0abc0`.
The subsequent probe-only change rejects closed sessions explicitly.

- Cleat: `c5eaa36c2d1dfbe8d18f5fdf946bd279b384a8cc`, detached clean worktree
  `cleat-worktrees/wheelhouse-image-baseline`.
- Ghostty: `c3dbb925e6cbcfceafba5749f81a486dd2275099`, built afresh with the
  pinned helper and Zig 0.16.0, ReleaseSafe.
- Andamento: `f8c63862e997f1e95fa80b264755e9ad270ab9ee`.
- Image suite: `c33f07651bf09a6ed4efa70aad9dc2c4ebcf551f`, detached clean worktree
  `kitty-image-tests-worktrees/wheelhouse-baseline`. Initial exploratory runs used
  the existing dirty checkout; provider baselines and RGBA/crop visuals were
  repeated against the clean checkout. The daemon GUI screenshot used the initial
  RGB session; its explicit stage and image helper files were unchanged.
- Host: arm64 macOS 26.6, build 25G72; native Metal renderer.

Both the standalone binary and `build/Wheelhouse.app/Contents/MacOS/wheelhouse`
passed glyph/cache diagnostics. The bundle passed `codesign --verify --deep
--strict`. It is a development bundle: it links external dylibs rather than
embedding self-contained dependencies.

`otool -L` identifies the exact baseline worktree's `target/debug/deps/libcleat.dylib`
for both binaries. Runtime `DYLD_PRINT_LIBRARIES=1` confirms cleat and Ghostty
loaded from the baseline worktree. Artifact SHA-256 values:

- cleat dylib: `ed3c7ae8403f7bf00eb7db77ade10e71424d18ce9868ded381168b8910198e47`
- Ghostty dylib: `8f1674bdc148f1e8c160e0673319e48b3f29a02d8e4795c38ded83173c9d995c`

These identify this local build, not portable expected hashes. No claim is made
about the library loaded by the user's pre-existing app.

## Results

| Path/check | Observed result | Evidence type |
| --- | --- | --- |
| In-process `explicit-rgb` | Resource lookup returns 4,704 bytes per image lookup | Real C ABI probe |
| In-process `explicit-rgba` | Gradient control and alpha checker visible | Native screenshot |
| In-process `multi-crop` | Five distinct crops from one PNG test-card asset visible | Native screenshot |
| Daemon create + live RGB | Descriptors and placement arrive; zero successful byte lookups | Real C ABI probe |
| Daemon native GUI attach | Text and box visible, image absent | Native screenshot |
| Two simultaneous daemon attachments | Both reach Streaming, both granted controller role; no live bytes | Real C ABI probe |
| Second viewer destroyed and reattached | Returns to Streaming with descriptors; first stays usable | Real C ABI probe |
| Forced transport disconnect/reconnect | Disconnected observed, then Streaming recovered | Private UDS proxy; process/session not restarted |
| Daemon history after 100 newlines | Viewport kind 3, offset 63, one successful 4,704-byte image lookup | Real C ABI probe; pixels not visually checked |
| Glyph fixture / image cache diagnostics | Pass in standalone and bundle | Native diagnostic exit status |
| Fixture semantic validator | Pass, 1040x624; emoji chroma 321 | Pixel validator |
| Existing integration checks | 7 sidebar ABI, 6 ingress, 3 launcher tests pass | Automated |

The probe's update/resource totals count returned updates, including repeated
in-process snapshots. They are **not** unique assets, wire packets, GPU uploads,
or throughput measurements. A TOP command on the unscrolled RGB screen remained
viewport kind 1 and is not counted as history coverage.

The live failure matches cleat #206. No in-process image regression was observed
in these stages. Linux/Windows builds, CLI outer-terminal graphics, retained
file/shm lifetime, large-frame streaming, history pixels, and full image protocol
coverage were not run here. They are not implied by a successful upload reply.

## Cache diagnostics

The existing glyph diagnostic now exercises the production image cache through
injected byte lookup, texture upload/release and quota collaborators. The normal
path retains its 320 MiB unreferenced-resource eviction policy. No decoder formats
or retained-asset semantics changed.

Covered: unchanged-generation texture reuse with moved placement; same-ID new
pixels/generation replacement and old-texture release; missing-byte retry at the
same generation; allocation-failure retry; placement removal without immediate
asset destruction; eviction and refetch; visible-resource quota exemption; and
proportional source cropping on both axes at canvas boundaries. Synthetic textures
avoid allocating hundreds of MiB. Current references may exceed the quota; this
is not a strict total-memory ceiling.

## Reproduction

This is a dated validation record, not a continuously updated support matrix.
For reproduction, set these paths to local checkouts at the revisions above, then build:

```sh
export WHEELHOUSE_CLEAT_DIR=/path/to/cleat
export WHEELHOUSE_ANDAMENTO_DIR=/path/to/andamento
export IMAGE_SUITE=/path/to/kitty-image-tests
(cd "$WHEELHOUSE_CLEAT_DIR" && bash tools/prepare-ghostty-vt.sh)
bash build.sh wheelhouse bundle
bash tools/run-macos-terminal-glyph-diagnostics.sh
python3 tools/validate-terminal-fixture-ppm.py local/screenshots/macos-metal-terminal-fixture.ppm
codesign --verify --deep --strict build/Wheelhouse.app
```

For isolated profiles, pass `--user:/tmp/PROFILE/user --project:/tmp/PROFILE/project`
to diagnostic runs. The baseline used temporary profiles for both executable forms.
Use `DYLD_PRINT_LIBRARIES=1` on each invocation to record loaded library paths.

Compile and run the C ABI probe:

```sh
clang -I"$WHEELHOUSE_CLEAT_DIR/crates/cleat/include" tools/cleat-image-baseline.c \
  -L"$WHEELHOUSE_CLEAT_DIR/target/debug" -lcleat -o build/cleat-image-baseline
build/cleat-image-baseline inprocess "cd '$IMAGE_SUITE' && uv run python -m smoke --stage explicit-rgb"
export CLEAT_RUNTIME_DIR=$(mktemp -d /tmp/wh-image-runtime.XXXXXX)
"$WHEELHOUSE_CLEAT_DIR/target/debug/cleat" --server wh-image-baseline launch baseline-rgb \
  --tag project=wheelhouse --tag purpose=test --size 100x40 --vt ghostty \
  --cwd "$IMAGE_SUITE" --cmd 'uv run python -m smoke --stage explicit-rgb'
build/cleat-image-baseline daemon "cd '$IMAGE_SUITE' && uv run python -m smoke --stage explicit-rgb"
python3 tools/probe-cleat-reconnect.py build/cleat-image-baseline "$CLEAT_RUNTIME_DIR" baseline-rgb
```

The probe creates two attachments, closes/recreates the second, and requests TOP.
For actual history, reuse the suite's RGB producer, then scroll it offscreen:

```sh
build/cleat-image-baseline daemon "cd '$IMAGE_SUITE' && uv run python -c 'from smoke.util.placement import explicit_rgb; import time; explicit_rgb(x=2,y=2,cols=14,rows=7); time.sleep(2); print(\"\\n\"*100,flush=True); time.sleep(30)'"
```

For native visuals, put this in an isolated user config, replacing the suite path:

```text
window:
{
  size: 1200 800
  panels: selected terminal:
  {
    expression: "cd /path/to/image-suite && uv run python -m smoke --stage explicit-rgba"
    selected
  }
}
```

Use `multi-crop` for crops. For daemon attach, add `session: "baseline-rgb"` and
`daemon_name: "wh-image-baseline"` to the terminal block and launch Wheelhouse
with the same `CLEAT_RUNTIME_DIR`. Quote hyphenated config values. Close only these
test windows. List and kill sessions only inside the private runtime; daemon
sessions survive closing a viewer.

Local logs, screenshots and the glyph fixture are retained under
`local/image-baseline/` and `build/`; they are not committed test expectations.

## Consumer requirements for cleat #206

- Reliable lookup of an exact `(session incarnation, image ID, generation)` while
  consuming a render update, and after the GPU cache evicts an unplaced texture.
- Sending bytes once cannot rely solely on Wheelhouse's GPU residency. The C
  daemon provider currently replaces its byte vector on later updates. Retain
  provider bytes or provide a refetch contract.
- Notify/wake consumers when previously unavailable bytes become available,
  even if placement and resource generation did not change. Wheelhouse retries
  invalid resources when processing an update; it cannot infer asset arrival.
- Keep placement removal distinct from asset deletion and define generation
  lifetime across history/live, reconnect and simultaneous attachments.
- Preserve format/compression metadata. Wheelhouse currently accepts uncompressed
  RGB/RGBA/PNG, not arbitrary new encoded formats or deflate payloads.

Do not expand decoder/cache ownership policy until the delivery contract is agreed.
After retained delivery lands, rerun these same stimuli plus the suite's source
lifetime, late-attach and large-frame cases.

Consumer findings were reported on [cleat #206](https://github.com/flotilla-org/cleat/issues/206#issuecomment-5720280520).
