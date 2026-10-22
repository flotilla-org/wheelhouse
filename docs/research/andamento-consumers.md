# Research: what builds and pins against Andamento today

Status: research notes, 22 October 2026. Nothing here is decided.

Question: [#422](https://github.com/flotilla-org/wheelhouse/issues/422), for the
map [#421](https://github.com/flotilla-org/wheelhouse/issues/421). What builds
from or pins Andamento's repo today, what does each consumer take from it, and
what would it pin to once Andamento's code lives in this repo?

**Sources.** Wheelhouse is cited at `origin/main` 99f1355. Andamento is cited at
upstream `main` 7fe869c (the `andamento-worktrees/daily-driver-main` checkout),
written `andamento:` below. That is exactly `ANDAMENTO_REV`, and Andamento has no
open PRs today. Flotilla is cited from the `~/dev/flotilla` checkout, written
`flotilla:`. The Zellij fork is `rjwittams/zellij`, read via the GitHub API and
the `~/dev/zellij.native-plugins` worktree (branch `spike/native-plugins`). A
sweep of every `~/dev/*` checkout for "andamento" in build, config and source
files found no other consumers beyond those below.

## Summary

Only Wheelhouse links Andamento's code into a shipped binary. It takes three
things: the `andamento-ffi` C library (with `andamento-core` inside it), the
generated header `andamento.h`, and the `andamento-git-watcher` binary. It never
touches the Zellij plugin crates. Everything else either path-depends on the
Zellij half of Andamento (the Zellij fork's native-plugin spike, Flotilla's
fixture generator) or talks to Andamento only over a wire format at runtime
(Flotilla's sinks).

Andamento's crates already split cleanly along that line, and Andamento checks
the split in CI (`andamento:scripts/check-independent-core.py:16`):

| Group | Crates | Needs a `../zellij` checkout |
| --- | --- | --- |
| Zellij-free | `andamento-core`, `andamento-ffi`, `andamento-git-watcher`, `andamento-terminal`, `andamento-html`, `andamento-tui` | no |
| Zellij plugins | `andamento` (root package), `andamento-shared`, `andamento-controller`, `andamento-rail`, `andamento-config` | yes (`zellij-tile`, `andamento:Cargo.toml:10-11,41-42`) |

`andamento-tui` has no Zellij dependency either, though the independence check
does not list it.

## Consumers

| Consumer | Takes | How it finds Andamento today | After the move |
| --- | --- | --- | --- |
| Wheelhouse CI (`build.yml`) | FFI library, header, git watcher, `wheelhouse_ingress` | `ANDAMENTO_REV` + `actions/checkout` into `../andamento` | Nothing to pin; same commit |
| Wheelhouse CI (`sidebar-memory.yml`) | same, release build | reads `ANDAMENTO_REV` out of `build.yml` | Nothing to pin |
| `build.sh` / `build.bat` | same | `WHEELHOUSE_ANDAMENTO_DIR`, default `../andamento`; generated consumer workspace | In-tree path |
| Headless harnesses (`tools/test-*.py`, `replay-ingress.py`) | FFI library via ctypes, header via `-I` | library path argument; header from `WHEELHOUSE_ANDAMENTO_DIR` | In-tree path |
| Daily driver (`tools/daily-driver.py`) | git watcher binary, KDL template format | `WHEELHOUSE_ANDAMENTO_DIR` / `ANDAMENTO_ROOT`, `ANDAMENTO_GIT_WATCHER_BIN` | In-tree build output |
| Zellij fork, `spike/native-plugins` | controller, rail, config crates | path `../../andamento/crates/...` | Path into the Wheelhouse checkout, or stays on the old repo |
| Flotilla fixture generator | `andamento-shared` serde types | path `../../../../../andamento/crates/andamento-shared`, run by hand | Path into Wheelhouse, or retired by flotilla#708 |
| Flotilla sinks (runtime) | patch wire format, pipe names | none: wire contract only | Unchanged |
| Andamento's own CI | everything | `ZELLIJ_REVISION` pin for the Zellij fork | Moves with whichever crates move |
| Project and lab records | repo identity | `project.yaml`, project-map manifests, Forgejo mirror | Update or retire |

### Wheelhouse CI

`build.yml` pins Andamento to a full SHA in the workflow env, with a comment
explaining the pin (`.github/workflows/build.yml:24-26`). The `native` job (all
three OSes) checks out `flotilla-org/andamento` at that SHA beside the
Wheelhouse checkout, without credentials so fork PRs work
(`build.yml:84-89`; `README.md:62`). The build itself is `build.sh wheelhouse`
or `build.bat wheelhouse` (`build.yml:119-130`), covered below. With `CI=true`,
`build.sh` also runs the Linux headless harnesses against the real Andamento
library and header (`build.sh:228-239`). The final "HTTP ingress and native
producer checks" step loads the built library straight out of Andamento's
target directory, `../andamento/target/$host/debug/libandamento_ffi.{so,dylib}`
(`build.yml:233-248`).

The `daily-driver` job needs no Andamento checkout: its tests fake the build
and the watcher (`tools/test-daily-driver.py:94-130`,
`tools/test-build-failures.py:66`).

`sidebar-memory.yml` does not hold its own pin. It reads `ANDAMENTO_REV` (and
the Cleat and Jackstay pins) out of `build.yml` with `sed` so the two cannot
drift (`.github/workflows/sidebar-memory.yml:38-44`), then checks out
Andamento the same way (`sidebar-memory.yml:55-60`) and builds release.

**After the move:** `ANDAMENTO_REV`, both checkout steps and the
`ANDAMENTO_REV` entry in sidebar-memory's pin loop retire. The test step's
`../andamento/target/...` paths (`build.yml:241-247`) become the in-repo target
directory. Cleat and Jackstay pins are untouched.

### `build.sh`, `build.bat` and the consumer workspace

Both scripts locate Andamento with `WHEELHOUSE_ANDAMENTO_DIR`, defaulting to
the sibling `../andamento` (`build.sh:67`, `build.bat:100`), and its Cargo
output with `WHEELHOUSE_ANDAMENTO_TARGET_DIR`, then `CARGO_TARGET_DIR`, then
`$andamento_dir/target` (`build.sh:69`, `build.bat:102`). Note that the output
lands inside the Andamento checkout by default.

They do not build Andamento's workspace. `tools/prepare-andamento-build.py`
writes a throwaway Cargo package, `build/andamento/Cargo.toml`, named
`wheelhouse-native-deps` (`tools/prepare-andamento-build.py:14-21`). Its library
is Wheelhouse's own ingress, `src/ingress/lib.rs`, built as
`wheelhouse_ingress`; it path-depends on `andamento-ffi` and
`andamento-git-watcher` in the configured checkout. The script copies in the
committed lockfile `tools/andamento-build/Cargo.lock`
(`prepare-andamento-build.py:22`), and the build runs `--locked`. The script
refuses a checkout without the git watcher (`prepare-andamento-build.py:10-11`).
This arrangement exists so that no Zellij checkout is needed
(`tools/andamento-build/README.md`; `README.md:132`).

The cargo build selects `-p andamento-ffi -p wheelhouse-native-deps -p
andamento-git-watcher` for the host target, overriding Andamento's
`wasm32-wasip1` default (`build.sh:72`, `build.bat:106`;
`andamento:.cargo/config.toml:2`). The C compile takes the header from
`$andamento_dir/crates/andamento-ffi/include` and links `andamento_ffi` and
`wheelhouse_ingress` with an rpath into Andamento's target dir
(`build.sh:73-75`). On Windows the two DLLs are copied next to `wheelhouse.exe`
(`build.bat:107-108,237-238`).

What Wheelhouse consumes from Andamento:

- **`andamento-ffi`**, the C library, built `cdylib`/`staticlib`/`rlib`
  (`andamento:crates/andamento-ffi/Cargo.toml`). It is the only Andamento
  crate the C code links, and it pulls in `andamento-core`.
- **`andamento.h`**, generated by cbindgen and committed; Andamento CI fails if
  it is stale (`andamento:scripts/generate-header.sh`). Wheelhouse includes it
  from `src/uishell/uishell_sidebar.c` and `src/uishell/uishell_subscriptions.h:4`,
  and demands exactly ABI 7 at runtime (`uishell_sidebar.c:1768`).
- **`andamento-git-watcher`**, a binary with no runtime dependency on any
  Andamento crate (only `serde_json`). It is built here only so the daily
  driver can run it (`build.sh:71`).
- **The KDL template language and record JSON.** `data/sidebar/*.kdl` are
  Andamento configs parsed by `andamento_create`, and
  `tools/workspace_seed.py:3-9` writes andamento-core `WorkspaceRecord` JSON.
  These are formats, not build inputs, so they need no pin.

**After the move:** `WHEELHOUSE_ANDAMENTO_DIR` and `prepare-andamento-build.py`
can retire in favour of a real Cargo workspace in the repo, holding the
Zellij-free crates plus the ingress crate. `tools/andamento-build/Cargo.lock`
becomes that workspace's lockfile. The header include path becomes a fixed
in-tree path. Whether `WHEELHOUSE_ANDAMENTO_TARGET_DIR` survives depends on the
layout question in `docs/design/platform-direction.md:59-61`. If the Zellij
plugin crates move too, they must stay out of the default workspace (Cargo
`exclude`, or a second workspace), or every Wheelhouse build will need a
`../zellij` checkout again, which is what the consumer workspace avoids today.

### Headless harnesses and replay

These load the built library via ctypes or compile C against the header:

- `tools/test-native-sidebar.py` and `tools/test-andamento-ingress.py` take a
  library path and assert ABI 7 (`test-native-sidebar.py:116`,
  `test-andamento-ingress.py:137`). Both hand-mirror ABI structs in ctypes
  (`test-native-sidebar.py:18-27`, `test-andamento-ingress.py:106-108`).
- `tools/test-docking-integration.py` and `tools/test-state-behaviour.py` take
  the library directory and default the header to
  `$WHEELHOUSE_ANDAMENTO_DIR/crates/andamento-ffi/include`
  (`test-docking-integration.py:26-27`, `test-state-behaviour.py:44-45`).
- `tools/replay-ingress.py` replays captures into a library given with
  `--library` (`replay-ingress.py:185`).

**After the move:** the header defaults become in-tree paths. The ctypes
mirrors stay hand-written either way; moving the code does not regenerate them.

### Daily driver

`tools/daily-driver.py` resolves Andamento from `WHEELHOUSE_ANDAMENTO_DIR`, then
`ANDAMENTO_ROOT`, then `../andamento` (`daily-driver.py:215`). Unless
`--no-build` or `WHEELHOUSE_BIN` is given it runs `build.sh wheelhouse` (or
`build.bat`) with that directory, which also builds the watcher
(`daily-driver.py:235-244`). It then looks for the watcher at
`ANDAMENTO_GIT_WATCHER_BIN`, or at
`<andamento target>/<host>/debug/andamento-git-watcher`, building just the
watcher through the consumer workspace if the main build was skipped
(`daily-driver.py:245-261`). It runs the watcher with
`--transport wheelhouse --socket <ingress> --roots <repo>`
(`daily-driver.py:173-178`). The KDL template defaults to Wheelhouse's own
`data/sidebar/daily-driver.kdl` (`daily-driver.py:216`). `README.md:244-251`
documents these variables.

The watcher's Wheelhouse transport shells out to `curl` against Wheelhouse's
`GET /v1/observed/workdirs` and `POST /v1/metadata/patch`
(`andamento:crates/andamento-git-watcher/src/transport.rs:37-39,87`;
`andamento:README.md:372-376`). So the watcher, which lives in Andamento,
depends on Wheelhouse's HTTP contract (`docs/protocol/pm-connect.md`) rather
than the other way round. It is also usable without Wheelhouse
(`--transport zellij`, stdout JSONL; `andamento:README.md:327-336`).

**After the move:** the watcher builds from the same tree at the same commit;
`ANDAMENTO_ROOT` and the Andamento half of `WHEELHOUSE_ANDAMENTO_DIR` retire, and
the default watcher path follows the in-repo target directory.
`ANDAMENTO_GIT_WATCHER_BIN` stays useful for release builds. Anyone who
installed it with `cargo install --path crates/andamento-git-watcher` from the
Andamento repo would install from the Wheelhouse path instead.

### The Zellij fork ("zellij-andamento")

Two directions:

1. **Andamento depends on the fork.** The Zellij plugin crates path-depend on
   `../zellij/zellij-tile` and `zellij-tile-utils` (`andamento:Cargo.toml:10-11,41-42`).
   Andamento's CI pins the fork as `ZELLIJ_REVISION: d262df58eb` from
   `rjwittams/zellij` and symlinks it as a sibling in every job
   (`andamento:.github/workflows/ci.yml:17-32`). That commit is ahead of the
   fork's `main` (GitHub compare).
2. **The fork's native-plugin spike depends on Andamento.** On branch
   `spike/native-plugins` (local worktree `~/dev/zellij.native-plugins`),
   `zellij-server` optionally path-depends on `andamento-controller`,
   `andamento-rail` and `andamento-config` at `../../andamento/crates/...`, with
   their `native-plugin-factory` feature (`zellij-server/Cargo.toml:50-53,93-100`,
   same on GitHub). The fork's `main` has no such dependency.

Neither side uses `andamento-ffi`, the header, or the watcher.

**After the move:** if the plugin crates move into Wheelhouse, Wheelhouse CI
inherits the `ZELLIJ_REVISION` pin and the sibling-symlink step for whatever
jobs build them, and the spike's paths would point into a Wheelhouse checkout.
If the plugin crates stay in the Andamento repo, the spike and the pin are
unaffected, but those crates then depend on `andamento-core` across repos,
reversing today's direction. Which crates move is the core-boundary question in
`docs/design/platform-direction.md:69-79`, not something this note settles.

### Flotilla

Flotilla has no build dependency on Andamento in its workspace or CI (no
"andamento" under `flotilla:.github/`). Two links exist:

- **Fixture generator.** `flotilla:crates/flotilla-manifest/fixtures/generator/Cargo.toml:7`
  path-depends on `../../../../../andamento/crates/andamento-shared` and is
  excluded from Flotilla's workspace (`flotilla:Cargo.toml:78`). It is run by
  hand to regenerate JSON fixtures that pin Flotilla's mirrored wire types
  (`flotilla:crates/flotilla-manifest/src/wire.rs:1-10`) to Andamento's real serde
  (`flotilla:crates/flotilla-manifest/fixtures/README.md`). That README calls
  the mirror a stopgap until a shared `flotilla-org/manifest` crate lands
  (flotilla#708). `andamento-shared` is a Zellij-side crate, so the generator
  also needs a `../zellij` sibling.
- **Runtime wire contract.** `ZellijPipeSink` sends patches to the Andamento
  Zellij plugin by pipe name (`flotilla:crates/flotilla-manifest/src/keys.rs:9,12`:
  `andamento-apply-metadata-patch`, `andamento-observed-identities`), and
  `WheelhouseHttpSink` posts the same payload to Wheelhouse's ingress
  (`flotilla:crates/flotilla-manifest/src/sink.rs:222-260`). No revision is
  pinned. Wheelhouse runs the Flotilla binary as a connector
  (`tools/daily-driver.py:162-165`), which is a Flotilla dependency, not an
  Andamento one.

**After the move:** the generator path would point into a Wheelhouse checkout
(wherever `andamento-shared` lands), or go away with flotilla#708. The pipe
names are a runtime contract; renaming the core does not touch them unless
someone renames the pipes.

### Andamento depending on others

Andamento's `tools/scenario` is a separate Cargo workspace that pins Flotilla by
git rev, `flotilla-manifest` and `flotilla-protocol` at 87f03e9
(`andamento:tools/scenario/Cargo.toml:8-9`). That pin moves with the tool.

`andamento:crates/andamento-core/tests/fixtures/daily-driver.kdl` is a copy of
Wheelhouse's `data/sidebar/daily-driver.kdl`, used by
`andamento:crates/andamento-core/src/scaling_tests.rs:338`. It has already
drifted: Wheelhouse's copy has the `workspace kind=...` subject declarations
and two `default-collapsed=true` flags the Andamento copy lacks. One repo would
let the test read the real file.

### Project and lab records

- Wheelhouse's `project.yaml:9-11` lists Andamento as a code member; Andamento
  has its own `project.yaml` and a governor workflow (`andamento:ops/governor.md`).
- The project-map's Flotilla manifest
  `~/dev/project-map/flotilla-manifests/repository-andamento.json` records the
  repo and a lab Forgejo mirror, `forgejo.lab.flotilla.work/lab/andamento`, last
  synced at 1dfd53b on 31 July 2026.

None of these build anything. After the move they need updating or retiring
along with whatever happens to the Andamento repo itself.

## Things noticed along the way

- The main Andamento checkout at `~/dev/andamento` is detached at 952e4ba and
  has no `crates/andamento-git-watcher`. A plain `bash build.sh wheelhouse`
  with the default `../andamento` therefore stops at
  `prepare-andamento-build.py:10-11`. Local builds work only with
  `WHEELHOUSE_ANDAMENTO_DIR` set to a current worktree such as
  `andamento-worktrees/daily-driver-main`. This is the kind of drift the move
  removes.
- `README.md:132` says builds need "C ABI 4"; the code requires ABI 7
  (`src/uishell/uishell_sidebar.c:1768`).
- Andamento's own CI (format, wasm clippy, host tests, wasm release build,
  header freshness, Zellij-free independence check;
  `andamento:.github/workflows/ci.yml:20-152`) has to land somewhere. The
  header check and the independence check matter to Wheelhouse regardless of
  which crates move.
