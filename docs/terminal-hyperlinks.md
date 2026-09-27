# Terminal hyperlinks

Hover an explicit OSC 8 terminal hyperlink to see its destination, which may
be different from its visible label. Cmd-click on macOS or Ctrl-click on Linux
and Windows opens an HTTP or HTTPS destination in the system handler. Activation
happens on the modified left-button press. That gesture's drag and release are
consumed as well, even if the modifier is released first.

Plain clicks retain the terminal application's mouse behavior. Shift-drag always
selects text locally, including in mouse-tracking applications. Shift takes
priority over link activation. Plain-text URLs and project paths are not detected.

Only HTTP and HTTPS with a nonempty authority and printable ASCII URI bytes are
eligible for opening; use percent encoding and punycode for international URLs.
Whitespace, controls, backslashes and all other schemes are rejected. In
particular, neither local nor remote `file:` destinations open a local path.
Blocked destinations remain inspectable on hover; control, non-ASCII and backslash
bytes are shown as `\xNN` escapes so they cannot hide or reorder the destination. Terminal output and hover
alone never launch a handler.

Destinations are copied from Cleat's render feed and move with cells, including
scrollback, row updates, scroll copies and resize. Hover is recalculated from the
frame being drawn. Activation uses the last displayed frame; it is suppressed
while the grid dimensions differ after resize. This requires Cleat provider ABI
10 / packet protocol 11, including an upgraded `cleat attach` on remote hosts to
relay OSC 8 through packet-to-terminal attachment.

## Validation

`./build/wheelhouse --terminal_link_diagnostics` runs headless model checks for
scheme policy, modifier priority, press/drag/release ownership, adjacent and wide
cells, copied URI lifetime, scroll copying, replaced content and resize. CI runs
these checks on Linux, macOS and Windows. Cleat has VT and nested-renderer tests.

Interactive acceptance must be exercised on the daily driver before merge:

1. Print `printf '\033]8;;https://example.com/destination\033\\different label\033]8;;\033\\\n'`.
2. Inspect the hover destination and Cmd-click (Ctrl-click elsewhere). Confirm
   hover alone does nothing, and try adjacent links and wrapped/wide labels.
3. Repeat through Cleat attach and remote flotilla attach, in scrollback, after
   output and after resize. Replace a link under a stationary pointer.
4. In a mouse-tracking application verify plain clicks arrive, link activation
   sends neither press nor release, releasing the modifier first is safe, and
   Shift-drag still selects. Try a remote `file://host/path` link: it must not open.

Build and model checks do not constitute interactive gesture or remote-attach
acceptance. The Linux crew environment cannot exercise the macOS gesture.
