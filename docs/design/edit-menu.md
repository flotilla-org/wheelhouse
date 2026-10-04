# Edit actions

Edit menu items, command palette activation and configured shortcuts use the same
commands and focused input owner. Opening a menu preserves the last editor in its
invoking window. Queued commands cannot redirect to a newly focused editor.

Text fields support selection Copy, Cut, Paste and whole-input Select All. A
Terminal View supports local selection Copy, structured Paste, Select All and
Clear Selection. Terminal Select All covers the **current rendered viewport**;
shared history selection is deferred to [cleat#300](https://github.com/flotilla-org/cleat/issues/300).
Clear Selection has no default shortcut and does not reserve Escape. Terminal
output cannot be cut. Undo/Redo remain disabled until a real editor supports them.
Unavailable commands are rejected at dispatch as well as shown disabled in menus.

On macOS, terminal host shortcuts are Cmd-C/V/A. On Linux and Windows they are
Ctrl-Shift-C/V/A. Text fields retain their existing shortcuts. Configured bindings
and native equivalents use the existing effective binding ownership rules. Plain
Ctrl-C/V/A/X/Z/Y remain available to terminal applications. A consumed host chord
owns its repeats, associated text and matching release, even after modifiers change.

Paste snapshots the standard clipboard once. Terminal Paste sends a Cleat Paste
event; Cleat owns bracketed-paste encoding. No host navigation keys are generated
for terminal selection. Copy uses the shared wrap-aware local selection extractor.

Daemon sessions disable process input when their reported connection is not
streaming or their role is not controller. Retained final output can still be
selected and copied. The pinned provider reports in-process sessions as streaming,
so **Paste remains enabled for in-process sessions even after process exit**. This
limitation requires the stopped-document lifecycle API in
[cleat#277](https://github.com/flotilla-org/cleat/issues/277). This change does not
modify Cleat or advance its pin.

Linux standard clipboard writes work, but the current Linux clipboard reader
returns empty text. External clipboard reads and PRIMARY are not implemented by
this change; see [wheelhouse#150](https://github.com/flotilla-org/wheelhouse/issues/150).
Shared tests use a clipboard boundary fixture; native macOS acceptance must use
the real clipboard and a functional VT provider.

`--terminal_selection_diagnostics` includes command/focus/clipboard traces with
two windows and two panels. `--shared_ui_diagnostics` also drives full and compact
menu buttons, and on macOS drives AppKit equivalents and menu activation through
the same shell consumers. The standalone `--edit_command_diagnostics` entry point
runs the shared traces without initializing a graphical display.

The Linux UI runner drives actual X11 plain and shifted key events through the
text producer and editor, asserting press/text/release order and metadata. Dead
keys and IME composition need a native input-method check; the disposable Xvfb
fixture does not provide an IME. That acceptance work is tracked in
[wheelhouse#158](https://github.com/flotilla-org/wheelhouse/issues/158).
Uncorrelated composition/synthetic events remain
independent of consumed physical chords in shared routing tests.
