# CI inputs

Lists and pins that the workflows in `.github/workflows` read, kept here so
that bumping a pin or adding a diagnostic needs no workflow edit.

| File | What it is |
| --- | --- |
| `pins.toml` | The Cleat and Jackstay commits CI builds against. |
| `pins.py` | Reads `pins.toml`; `python ci/pins.py github-env >> "$GITHUB_ENV"` exports each pin. |
| `ui-diagnostics.toml` | The UI diagnostics each OS runs, in order. |
| `run-ui-diagnostics.py` | Runs the Linux or macOS list; prints the Windows names. |
| `check-ui-diagnostics-migration.py` | One-off check that the list matches the workflow lines it replaced (#236). |
| `test_pins.py`, `test_run_ui_diagnostics.py` | Tests for the above (`python3 ci/test_pins.py`). |

The scripts use only the Python standard library and need Python 3.11 or
later, for `tomllib`.

## Pins

```toml
format = 1

[cleat]
env = "CLEAT_REV"            # variable the workflows read, as ${{ env.CLEAT_REV }}
repo = "flotilla-org/cleat"  # the repository the commit is in
rev = "c88692aa…"            # full 40-character lower-case commit
track = "manual"             # how the pin moves; "manual" is the only policy
```

`pins.py` fails if the file is missing, a `rev` is not 40 lower-case hex
characters, two pins share an `env` name, or a key is missing or unknown.

To bump a pin, change its `rev` and update the comment under it to say what the
new commit brings (ABI, protocol, rollout constraints). Locally, check out the
sibling repository at the same commit (`WHEELHOUSE_CLEAT_DIR`,
`WHEELHOUSE_JACKSTAY_DIR`) and build. No workflow changes.

Binnacle is not pinned here. Cargo pins it: `binnacle-core`, `binnacle-ffi`
and `binnacle-memory` are git dependencies by `rev` in `Cargo.toml`, locked in
`Cargo.lock`. Bump it there. Tool versions (Zig, Python, Node) are still in the
workflows.

## UI diagnostics

`ui-diagnostics.toml` lists every UI diagnostic, once, with the OSes it runs
on. CI runs them with:

```sh
python3 ci/run-ui-diagnostics.py --os linux    # "Linux UI diagnostics"
python3 ci/run-ui-diagnostics.py --os macos    # "macOS UI diagnostics"
```

and `native\run_tests.bat` takes its default list from
`python ci/run-ui-diagnostics.py --os windows --names`. The runner makes one
temporary directory, runs each entry for the OS in file order (on Linux under
`timeout 120s xvfb-run -a`), and stops at the first failure. Add `--dry-run` to
print the commands instead, with the directory as `$task_dir`. Run it from the
repository root after building `./build/wheelhouse`.

To add a diagnostic, add an entry where it should run in the order:

```toml
[[diagnostic]]
flag = "table"           # ./build/wheelhouse --table_diagnostics
prefix = "table"         # --user:"$task_dir/table-user" --project:"$task_dir/table-project"
os = ["linux", "macos", "windows"]
```

Optional keys: `args` (after the flag), `pre_args` (before it) and `env`,
variables for that run only; all three may use `$task_dir`. Windows ignores
them. An entry can run a
`command` instead of the app, such as a check that reads what an earlier entry
wrote. See the header of `ui-diagnostics.toml`.

Choosing OSes: run it everywhere unless it needs something a job lacks. The
Linux job builds Cleat without Ghostty's VT engine, so anything that runs real
terminal sessions is macOS and Windows only; the file says so beside those
entries. On Windows only the name is used: `tools/run-windows-diagnostics.ps1`
makes the directories and runs each diagnostic with its own timeout. The flag
must be one `native/src/uishell/uishell_main.c` accepts;
`ci/test_run_ui_diagnostics.py` checks that.

Diagnostics that run without `--user`/`--project` and outside the display
steps (Jackstay viewport geometry, terminal clipboard effects on Linux and
macOS) stay as their own steps in `build.yml`.
