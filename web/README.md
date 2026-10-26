# Web sidebar

The sidebar in a browser: `crates/wheelhouse-wasm` runs the core as wasm,
and this page follows its change stream into a SolidJS store and changes it
by intent (`docs/design/change-stream.md`). It replays
`data/sidebar/fixture.jsonl` through the daily driver's templates; no relay
feeds it yet, and it opens no workspaces.

Needs Rust with the `wasm32-unknown-unknown` target, `wasm-pack`, Node and pnpm.

```sh
pnpm install
pnpm dev     # builds the core into src/core, serves on :5178
pnpm test    # builds, serves on :5179, checks the page's model in Chromium
```

The test compares the model the page built from deltas with a fresh view of
the core after loading, after clicks and after a later fact.
