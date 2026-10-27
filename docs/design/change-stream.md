# The change stream and intents

Status: design, agreed 26 October 2026, for step 0 of [#441](https://github.com/flotilla-org/wheelhouse/issues/441). The boundary it implements was decided on [#425](https://github.com/flotilla-org/wheelhouse/issues/425); the costs it removes were measured by the browser sidebar prototype ([#428](https://github.com/flotilla-org/wheelhouse/issues/428), `prototype/web-sidebar`).

Step 0 gives the core one way to tell a frontend what changed and one way to be told what to do. A frontend **subscribes** to a view, gets a **snapshot**, then follows **deltas**, each tagged with the generation it brings the view to. It changes logical state only by sending an **intent**. Intent results and fact-driven changes arrive through the same deltas. The sidebar is the first view; the arrangement, Slots, overlay and local entities follow in later #441 steps on the same machinery.

This is the in-process boundary: Rust for the TUI, the C ABI for native, wasm for the browser. The core and its frontend ship in one build there, so there is nothing to negotiate. How the relay carries a stream between builds is decided separately; the stream's types serialize so that a negotiated, versioned codec can carry them later (CBOR is one candidate, not a decision).

## What it replaces

Today any change bumps the sidebar's revision and drops its retained snapshot. Native polls `wheelhouse_snapshot_is_current`, and on a change acquires a new snapshot, which clones the whole surface and flattens it into C arrays (`acquire_snapshot`), then marks all managed content dirty. The other stores (both arrangements, Slots, the dashboard, records, local entities) each have a generation and a get/commit pair of their own. Sidebar clicks go through `wheelhouse_dispatch`, which returns host effects.

The prototype put numbers on the snapshot path in the browser: at 734 rows a whole snapshot is 7.3 MB and 33 ms per update, and diffing projected rows afterwards costs 11.6 ms. Keyed deltas into SolidJS cost 1.4 ms at the same size. Its findings 3 to 6 are this design's requirements: deltas from what changed, positional order operations, typed roles instead of `chip-…:` strings, and intent results through the stream.

## Producing deltas

Evaluation already keeps what it can between revisions (`Kept` in `state.rs`): a node whose placement, variables, state, collapse, staleness and children are unchanged comes back as the same `Rc<PlacementNode>` (`Resolver::node`). So the core can produce a delta by walking the subscriber's last surface against the current one and skipping every pair that is `Rc::ptr_eq`. The walk costs in proportion to the changed nodes and the child lists along their paths, not to the surface.

The snapshot is the first delta: a subscription's first poll compares nothing with the current surface. Each subscription keeps the surface it last delivered (an `Rc`, so sharing is free). A subscriber that falls several revisions behind gets one delta from its surface to now. There is no history log to keep or trim, and a slow consumer never sees an intermediate state.

A frontend never sees a half-applied change: a delta is applied whole, and its generation is the view's generation once applied.

## The sidebar view's deltas

The view is a keyed tree: sections, then placement nodes by `PlacementKey`. A delta is an ordered list of operations:

- `upsert(node)`: a node's own content (label, form, state, typed roles, fields, controls, collapsed, stale), without its children;
- `remove(key)`;
- `children(parent, ops)`: positional `insert(at, key)`, `remove(at)` and `move(from, to)` on a node's or section's child list, so a visibility change sends only what moved;
- `section(…)` and `sections(ops)` for sections and their order;
- `display_variables(…)` and `diagnostics(…)` when they change.

Detail cards stay on demand, as now: they are large and read rarely.

**Typed roles**, in step 0 so the payload is defined once: chip meaning moves out of prefixed field strings into the node: status, attention, icon, and the short and medium labels. A template declares them with `presents="…"` on a field, and they arrive in the node's content as `presented`. Native's C parser (`uishell_sidebar.c`) goes, and native and the web read the same typed fields.

**Pull.** A frontend calls `poll(subscription)` when it is ready to apply a delta: once a frame for native, from the event loop in the browser. A cheap "changed since" check, as `snapshot_is_current` is now, tells it whether to. A push nudge is added only if a frontend turns out to need one.

## Intents

One entry point: `apply(intent) -> IntentReceipt`. The receipt is accepted, with a request id, or refused, with a reason. What the intent changes arrives as deltas, tagged so the frontend that sent it can match them to its request id. Host effects (focus a workspace, open a URL, copy) are platform commands for the frontend that sent the intent, so they come back with the receipt, as `dispatch`'s do today.

Step 0's vocabulary is the sidebar's existing `Action`s, renamed as intents. Later steps add arrangement, lifecycle and store intents. The vocabulary is the core's logical-state commands, shared with the Automation Endpoint (ADR 0014): each intent has a stable name a named command maps onto.

## Bindings

Rust types are the source.

- **Rust (TUI):** the types directly.
- **C (native):** cbindgen. A delta is a handle with accessors, as the snapshot is now: operation count, operation *i*, and text that borrows the handle's buffers until release. The header check in CI covers it.
- **wasm (browser):** a `wheelhouse-wasm` crate, with the SolidJS app beside it in `web/`, with TypeScript types generated from the Rust (tsify). Deltas cross as JS objects, not JSON text. The wasm check of `wheelhouse-core` in CI already keeps the core building.

## Tests

- **Property (Hegel):** for random patch, tick and intent sequences, a fresh snapshot equals the first snapshot with every delta applied, for subscribers that poll at every step and for ones that poll rarely. Generations only increase. An evaluation that changes nothing yields no delta.
- **Cost:** `scaling_tests` asserts that one small patch at fleet scale yields a delta of a few operations, and times it beside `stress_shapes`.
- **C:** a C harness follows the stream through the ABI and checks it against snapshots, like the existing smoke test.
- **wasm:** a headless browser test replays a recorded facts stream and checks the page's model against snapshots.

## Pull requests

Stacked:

1. The stream in Rust: subscriptions, the delta model, the `Rc` walk, typed roles, and the property and cost tests.
2. The intent entry point, with `dispatch` kept as a shim.
3. The C ABI and its harness.
4. `wheelhouse-wasm`, the TypeScript types and the browser test.
5. Native's sidebar follows the stream instead of re-acquiring, and its chip parser goes. This is the largest C change, so it comes last.

## Decided

With Robert, 26 October 2026: typed roles are in step 0; native's switch is step 0's last PR; delivery is pull; the web app lives in `web/` in this repo.
