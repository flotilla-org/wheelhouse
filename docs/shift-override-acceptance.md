# Native Shift override acceptance (#70)

Status: **operator acceptance pending**. Linux builds and fixture/UI traces do
not establish physical mouse/trackpad or current nested-attach behavior.

Use a disposable user/project configuration and a separate test window; do not
restart or replace the daily driver. Record platform, Wheelhouse/Cleat versions,
input device, direct versus current Flotilla attachment, and the observed outer
history availability for each run.

1. Run a numbered normal-screen transcript (for example `seq 1 500`) directly
   and through the current nested attachment. Compare plain wheel and
   Shift-wheel, both directions, small trackpad deltas, and both bounds.
   Shift-wheel must affect only the outer document, including mouse wheels
   which AppKit reports on its horizontal axis while Shift is held. If its history is absent,
   record “outer history unavailable; consumed local no-op,” rather than
   claiming inner history moved.
2. Repeat using a fullscreen mouse-capturing TUI, including Claude Code. Verify
   Shift-wheel changes only available outer history and never invokes child
   mouse/cursor-key actions. Compare a mouse wheel with a trackpad.
3. Verify Shift-PageUp/Down and their release after releasing Shift first. Plain
   page keys must still reach the application. On keyboards lacking page keys,
   use the Scroll Terminal View Up/Down palette commands or configure Cmd
   bindings, and verify one page uses the current visible row count.
4. Compare plain versus Shift-middle-click under capture and without capture.
   Paste a selection different from the standard clipboard; repeat with an
   empty selection. Release outside the canvas, change Shift/capture before
   release, and lose focus while held. No local mouse press/release may reach
   the child. In bracketed-paste mode, verify one paste operation.
5. Shift-drag then Cmd-C (Ctrl-Shift-C elsewhere), and Shift-Option/Alt rectangle
   selection must survive. Change modifiers during the drag; no local left
   gesture may reach the child. Application wheel/mouse interaction invalidates
   selection according to #154, while an unmoved local viewport preserves it.

| Environment/device | Transcript | Capturing TUI | Outer history available | Result/evidence |
| --- | --- | --- | --- | --- |
| Direct / mouse | pending | pending | pending | pending |
| Direct / trackpad | pending | pending | pending | pending |
| Current attach / mouse | pending | pending | pending | pending |
| Current attach / trackpad | pending | pending | pending | pending |

Operator: record acceptance or concrete failures in the PR before settlement.
