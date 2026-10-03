# Terminal selection consistency (#72)

## Consumer and lifetime

Terminal View owns a coordinate selection over its displayed cell cache. Its
feed boundary (`uishell_terminal_apply_update`) holds the previous cache arena
only long enough to compare selected graphemes and cell widths, then publishes
the new cache. Highlight and Copy use the same column calculation. Copy uses
`uishell_terminal_copy_selection` and leaves the selection active. The terminal
continues updating while selected.

Preserve selection for cursor/blink changes, styling, unchanged full refreshes,
font/theme changes which leave grid geometry unchanged, and text updates outside
its range. Invalidate on selected grapheme/width changes, copies moving a selected
row (source or destination), grid resize/reflow, viewport kind/offset/top changes,
normal/alternate screen changes, and session-handle replacement. A leading glyph
outside a rectangular left edge is compared when a selected tail depends on it.
Hosting transfer/adoption conservatively clears because the pin supplies no
stable document identity across hosts.

Invalidate a drag on a changed content mapping or pixel geometry; keep its local
owner until release so cancellation cannot turn into application input. Completed
selection survives focus loss; incomplete drags are canceled. Native focus loss,
hiding the View, superseding presses and runtime teardown retire active gesture
ownership. A child press is paired with a release through the provider boundary;
a rejected press acquires no held child button.

`uishell_terminal_mouse_event` consumes ordered UI messages with their individual
coordinates and modifiers. The left press chooses local selection (Shift or no
capture), hyperlink activation (its platform chord), or application input. The
choice and rectangular mode persist through drag/release. Shift added to an
application gesture cannot invoke the provider's Shift override. Local gestures
send no left messages to the provider. Selection completion writes the existing
shared selection pasteboard; explicit Copy writes the standard clipboard.

The input wrapper uses `cleat_session_send_input_ex` and its emitted input count:
nonempty accepted Text/Paste, process key events (including Escape), application
mouse press and wheel input dismiss selection. Rejected input, successful no-ops,
empty Paste, modifier-only events, focus and Copy preserve it. Watcher, disconnected or
closed attachments reject process input locally and retain Copy; no path here
creates or restarts a session.

## Coordination and known provider limits

[Cleat #301](https://github.com/flotilla-org/cleat/issues/301) requests a coherent
reset/document/screen-transition epoch in render updates and packets. ABI 10 at
`87b9d853` has no such signal. Same-text reset, replacement behind an unchanged
handle, and switching screens away and back between pulls cannot be distinguished
from an unchanged full refresh. **Those transitions are not proven safe by this
change.** Neither dirty state nor render generation is used as a substitute.
Persistent tracked history selection remains Cleat #300.

Wheelhouse #70 should use the existing left-owner consumer and add wheel/middle
ownership without another left-selection matcher. Wheelhouse #152 should call
`uishell_terminal_copy_selection` for local Copy,
`uishell_terminal_clear_selection` for local-only Clear, and
`uishell_terminal_send_input` for process Paste. It owns semantic editing commands
and physical clipboard-chord release suppression. Wrap joining is #151.

Cleat may decline mouse reports after the application turns tracking off. The
traces prove the host/provider press/drag/release sequence; physical application
behavior across tracking-mode changes still needs the native checks below.
In-process stopped process identity/input capabilities remain provider work in
Cleat #277; this change does not infer exit from connection state or relaunch it.

## Deterministic evidence

The initial trace wrapped the existing real cell-cache consumer in the view's
feed boundary, selected `ab`, replaced its first cell, and read the displayed
selection. `./build/wheelhouse --terminal_selection_diagnostics` failed before
invalidation with:

```
stale selection trace: selected ab, Copy now returns Xb (expected cleared)
terminal selection diagnostics failed
```

After the fix the existing diagnostic runs extraction tests plus exhaustive
stream/rectangle traces over four drag directions and every cell of a 6x3 feed.
It covers selected/outside overwrites, wide-cell boundaries, cursor/style/full
refreshes, row-copy source/destination/self-copy, geometry/viewport/screen/session
transitions, canceled drags, Copy retention, accepted/rejected/no-op process
input, Escape, modifiers, application gesture ownership, same-frame gestures,
hyperlink ownership, outside release, focus cancellation and runtime teardown.
Fixtures replace only the provider feed/input boundary. Real cache, mouse,
selection and Copy consumers execute.

The existing scroll diagnostic also drives `ui_signal_from_box` with six ordered
messages (two gestures) in one frame, a deliberately different sampled pointer,
and an outside release, then feeds its claimed list to Terminal View's consumer.
The AppKit key-event runner now posts a local application-queue mouse
press/drag/outside release trace and verifies WM coordinates, modifiers and order.
It does not inject global OS input.

Linux uses repository-pinned Cleat `87b9d853` with features `none`, Andamento
`56bd2d05` and Jackstay `91156bfa`. The build and selection/link
headless checks passed. Scroll, panel, tooltip, shared UI, preview and sidebar
diagnostics passed under a temporary Xvfb
outside the checkout, with disposable user/project settings and software GL.
Native macOS/Windows compile and AppKit execution must be established by PR CI;
Linux's mock feeds do not establish a real VT or physical GUI acceptance.

## Mutation checks

All three targeted mutants were caught and reverted:

- Disable feed invalidation: selection diagnostic exits 1 on stale Copy and mapping
  transitions.
- Preserve/add Shift to an application-owned drag: selection diagnostic exits 1
  on application press/drag/release modifier ownership.
- Drop ordered motion claiming: the scroll diagnostic exits 1 specifically on the
  Terminal View's ordered UI-event trace.

Generated sources match committed copies; `git diff --check` passes.

## Operator native acceptance (pending)

Use a separate candidate build and disposable settings. Do not restart or replace
the daily driver. Record OS/build, Cleat/daemon revisions, route and exact results
for each scenario:

1. Normal shell: select in both directions, Shift-Option rectangle, Copy twice,
   modifier-only presses, type, nonempty/empty Paste, process Escape. Confirm Copy
   preserves bytes until dismissal and Escape reaches the process exactly once.
2. Overwrite inside/outside selection, cursor blink/style/full repaint, scroll,
   resize, alternate-screen switch. Confirm original text remains or selection
   deliberately clears; no replacement bytes are copied.
3. Fullscreen mouse-tracking TUI: Shift-select/Copy, change modifiers during both
   local/application drags, release outside, change tracking, supersede a press,
   hide the tab and lose focus. Confirm local gestures send no application clicks
   and application buttons do not stay held.
4. Repeat through the current nested Flotilla/Cleat route; record outer viewport
   identity/history limitations. Check retained disconnected/stopped content can
   Copy and typing/Paste never launches a replacement process.
5. Same-text reset and a coalesced normal/alternate round trip are pending Cleat
   #301, not accepted merely because an ordinary repaint preserves selection.

Physical GUI acceptance is required before crew settlement. Headless traces and
CI builds are reported separately and do not replace these results.
