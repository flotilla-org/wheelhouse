# Retained subject workspaces

An open workspace of an ended subject keeps its original subject row visible,
dimmed and marked ×, with Show finished off. Focusing uses the original workspace
ID and preserves panels and tabs. Closing the workspace releases its retained
path; the row then follows the normal Show finished rule. This records the
2026-10-05 owner ruling on [#188](https://github.com/flotilla-org/wheelhouse/issues/188),
which supersedes the historical acceptance below. Current validation and captures
are in [workspace highlight acceptance](workspace-highlight-acceptance.md).

An open workspace whose subject is unobserved, or whose kind has no placement,
appears in Other workspaces. Coverage uses workspace IDs and includes collapsed
descendants. A restart without subject facts also falls back there; it does not
infer project or role placement (the latter remains andamento#105).

Published convoy phases `landed`, `abandoned`, and `cancelled`, superseded
generations, and producer identity retractions are authoritative ends. Fact
expiry, empty drains during disconnects, and subsequent reassertions are
unobserved periods, which preserve the existing path without marking it ended.
A standing role outlives the terminal phase of its current attempt.

Retained paths are held while their workspace remains open. No timeout runs ([policy follow-up #159](https://github.com/flotilla-org/wheelhouse/issues/159)):
`retained_workspace_expired` in Andamento is the policy seam for #159's later
expiry decision. Historical search and log navigation remain outside this slice.

![Ended workspace under its convoy with Show finished enabled](screenshots/retained-workspaces/show-finished.png)

This older capture has Show finished enabled. It is historical evidence only;
use the current acceptance link above for the open-ended behavior with the
filter disabled.

Native entry templates supply fields in label/kind/status order. Custom KDL
must put status at index 2 for native status glyphs and ended hover text; the
renderer no longer guesses status from the first non-label/non-kind field.

## Historical validation before #188

The following results describe the earlier retained-path implementation, not
the current open-ended visibility rule or companion pin.

- Native Linux build with clang, Cleat's `none` feature set, and the existing
  workflow-pinned Jackstay succeeds.
- All 26 scenarios in `tools/test-native-sidebar.py` pass against Andamento
  `b72a103` (the main merge of verified companion `5a84451`, andamento#121). They use the real daily-driver
  templates and C ABI, including source-heartbeat removal, alias identity,
  Show finished, focus and user close.
- Andamento's 114 unit tests and 43 sidebar scenarios pass. Disconnect tests
  cross the lease boundary, stay on the original project path, reconnect,
  and remain unended. Tests also cover ancestor removal, repeated authoritative
  ends, no timer, standing roles, unrelated sources and subjectless workspaces.
- Targeted core mutations removing retained catalog augmentation, terminal-phase
  detection, and explicit fact withdrawal each fail the corresponding scenario.
  Native mutations dropping the ended glyph or letting opening override ended
  each fail the shared-renderer diagnostics. The new lifecycle test also
  fails against the unpatched core.

Reproduce the headless ABI acceptance after building:

```sh
python3 tools/test-native-sidebar.py ../andamento/target/x86_64-unknown-linux-gnu/debug/libandamento_ffi.so
```

For a live presentation, use isolated user/project settings with
`--andamento_socket:<private-directory>/facts.sock` and
`--andamento_config:data/sidebar/daily-driver.kdl`. Publish a project, convoy
and vessel with an attach recipe, open the vessel, then publish its terminal
phase or retract its identity facts. Toggle Show finished and focus its row.
The implementation and screenshot do not claim macOS or Windows visual
validation; their existing native CI builds exercise the shared code.
