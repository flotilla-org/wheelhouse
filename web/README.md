# Web sidebar

The sidebar in a browser: `crates/wheelhouse-wasm` runs the core as wasm,
and this page follows its change stream into a SolidJS store and changes it
by intent (`docs/design/change-stream.md`). It replays
`data/sidebar/fixture.jsonl` through the daily driver's templates; no relay
feeds it yet, and it opens no workspaces. The facts are applied at time 0
and the core is never ticked, so the page is static: what changes with time
(staleness, ages) stands still until a relay feeds it live facts and a clock.

Needs Rust with the `wasm32-unknown-unknown` target, `wasm-pack`, Node and pnpm.

```sh
pnpm install
pnpm dev     # builds the core into src/core, serves on :5178
pnpm test    # builds, serves on :5179, checks the page's model in Chromium
```

The test compares the model the page built from deltas with a fresh view of
the core after loading, after clicks and after a later fact.
