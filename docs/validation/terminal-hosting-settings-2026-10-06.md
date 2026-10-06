# Terminal hosting in tab settings

Issue #109 moves terminal hosting metadata into Selected Tab Settings
(`tab_settings`, Cmd/Ctrl+Alt+T). The panel shows the live hosting identity and
Hand to daemon / Adopt. Existing palette commands still work.

`show_hosting_overlay` is a per-view Boolean, default `0`. Toggleable canvas
metadata follows `show_<name>_overlay`, leaving room for later overlays.
The daemon connection/role pill remains an independent on-canvas status cue.

The shared schema-driven settings renderer now accepts
`@runtime_value(provider_name)` and `@runtime_action(provider_name)` on string
fields. The runtime provider supplies a value, action label, and command; a
missing command disables the action. These rows bypass text editing and read
live state each frame. Their types are marked noneditable. They do not write
configuration. To add another runtime provider, extend
`uishell_runtime_setting`; no bespoke settings panel is necessary.

## Container validation

Built the native Linux executable with the CI-pinned Jackstay checkout and
`WHEELHOUSE_CLEAT_FEATURES=none` (passthrough, matching Linux CI). Ran
`--hosting_diagnostics` under Xvfb with software OpenGL. The diagnostics use
production schema evaluation, the settings lister, the terminal canvas, a real
in-process Cleat session, and synthetic pointer events. They cover unavailable
hosting (including an untouched, never-built hidden terminal), in-process and daemon action selection, exact daemon identity retention,
noneditable settings rows, per-view default/override behavior, off/on/off canvas
visibility, and action routing to the owning terminal.

Both targeted mutations were detected and reverted: removing the overlay guard
fails the off-state canvas checks; swapping transfer/adopt commands fails the
action mapping and pointer-routing checks. The final hosting, panel, and
clipboard diagnostics pass, as do generated-source and whitespace checks.

Not exercised: operator desktop visual/interactive acceptance, Ghostty rendering,
or a live daemon transfer/adopt round trip. The operator should open Selected Tab
Settings on a live terminal, transfer it, inspect its daemon identity, adopt it,
and toggle Show Hosting Overlay. Confirm the connection/role status pill still
appears when its status requires it. No desktop visual acceptance is claimed.

CI wiring follow-up: [#233](https://github.com/flotilla-org/wheelhouse/issues/233).
The injected GitHub App token cannot update workflows; this PR provides the
native diagnostic command and a runner with an explicit `xvfb-run` example.
