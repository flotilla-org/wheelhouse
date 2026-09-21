# Sidebar update cost, 2026-09-21

The daily-driver profile spent almost all main-thread samples rebuilding
Andamento snapshots after individual incoming patches and periodic ticks.
Andamento now builds one indexed catalog evaluation per changed snapshot and
retains the snapshot across unchanged facts, heartbeat renewals and idle ticks.

Wheelhouse asks `andamento_snapshot_is_current` before acquiring a replacement.
Ingress callbacks apply facts without acquiring a snapshot. After draining the
queue and applying the expiry tick, the host refreshes each sidebar once. Change
tracking, expiry bookkeeping and action validity remain in the shared core.
The existing frame scheduling policy and producer protocol are unchanged.

The Andamento pin is c11bbe715da365027961cc06ce07dd69cd3a5866 on
`perf/catalog-evaluation`; publish that ref before running remote Wheelhouse CI.

Validation on macOS:

- Full Wheelhouse debug build against the new dependency passed.
- Eight native-sidebar C ABI tests passed, including unchanged-tick and
  duplicate-patch validity and changed-recipe invalidation.
- HTTP/UDS ingress tests passed. The native executable test was also run
  explicitly and accepted updates after becoming idle.
- Andamento's reusable-core validation passed its unit/integration tests,
  terminal/HTML/FFI tests, no-JSON FFI build and linked C fixture.

The shared-core synthetic 200-entity snapshot benchmark fell from about 923 ms
to 30 ms with catalog reuse in the first measurement. Unchanged tick plus Rust
borrowed snapshot queries now take about 0.05 to 0.08 microseconds; that figure
does not include C snapshot flattening or rendering. Full changed-snapshot
measurements vary under concurrent build load. See Andamento's
`docs/sidebar-design/catalog-evaluation.md` for scope and reproduction.

The daily-driver process has not been replaced, so an end-to-end profile of
real Flotilla traffic is still needed. Producer batch envelopes, incremental
catalog maintenance and frame scheduling changes are outside this patch.
