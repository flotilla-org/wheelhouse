# Terminal selection and clipboard remediation

Status updated 4 October 2026 against Wheelhouse main through PR #156. The
original source review on 3 October used Wheelhouse `00be551`, Cleat `e233d6d`,
Ghostty app source `64daa599`, and Cleat's Ghostty VT fork `c3dbb925`. The
current Wheelhouse dependency pin is still Cleat `87b9d853`; merged Cleat
clipboard transport is a later ABI and deployment boundary.

## Selection lifetime and Shift ownership

Wheelhouse supports native Shift-drag, Cmd-C, and Shift-Option rectangular
selection. Selection release writes the selection pasteboard; explicit Copy
writes the standard clipboard. Paste uses Cleat's structured paste input,
including bracketed-paste encoding.

[Selection lifetime #72](https://github.com/flotilla-org/wheelhouse/issues/72)
landed in [PR #154](https://github.com/flotilla-org/wheelhouse/pull/154).
Selection endpoints remain viewport row/column coordinates, but the feed
consumer now validates selected text, widths and available mapping identity
before publishing an update. It preserves Copy, cursor/style changes,
unchanged refreshes and edits outside the selection. Selected-content or
mapping changes clear the selection, as does accepted process input.
Process-directed Escape dismisses selection and still reaches the child once;
Clear Selection remains local-only.

Local selection, hyperlink and application ownership is latched at press
through release/cancellation, using each event's coordinates and order.
Rectangular mode is latched too. Hidden views, focus loss, superseding presses
and teardown retire accepted gestures. Retained stopped/disconnected content
can still be copied locally, while process input follows provider availability.
The pinned in-process provider does not expose stopped-document lifecycle;
[Cleat #277](https://github.com/flotilla-org/cleat/issues/277) owns that gap.

Ghostty keeps selection after Copy by default. Its default
`selection-clear-on-typing` clears it when a non-modifier key sends input and
when composition begins; Escape and application-owned mouse interactions
clear it. `Screen.select` converts endpoints to tracked terminal references
which follow storage through scroll/reflow. References: Ghostty `Surface.zig`
(`keyCallback`, `mouseButtonCallback`, `setSelection`) and `terminal/Screen.zig`.
Wheelhouse's coordinate fallback clears selection on history scrolling; this
prevents stale copying but does not provide Ghostty's retention behavior.

[Wrap-aware copying #151](https://github.com/flotilla-org/wheelhouse/issues/151)
landed in [PR #153](https://github.com/flotilla-org/wheelhouse/pull/153).
The cell cache retains wrap flags and available screen identity. Linear Copy
joins soft-wrapped physical rows into logical lines; rectangles retain physical
rows and padding. Highlight and extraction use the same selected bounds.

[Shift override #70](https://github.com/flotilla-org/wheelhouse/issues/70)
landed in [PR #156](https://github.com/flotilla-org/wheelhouse/pull/156).
Shift-wheel and Shift-page navigation request the outer viewport without child
input; precise deltas are accumulated per event. Shift-middle forces structured
selection-buffer Paste even under mouse capture, with ownership retained
through release. An unavailable outer history is a consumed local no-op.
It does not imply access to inner Cleat history or XTSHIFTESCAPE negotiation.
Physical mouse/trackpad and nested-route acceptance remains pending in
[the Shift override checklist](../shift-override-acceptance.md).

Full selection behavior needs attachment-owned tracked references and shared
copy formatting through Cleat, tracked in
[Cleat #300](https://github.com/flotilla-org/cleat/issues/300). Each attachment
needs its own selection, including screen identity; Wheelhouse retains native
pointer policy and draws returned ranges. Separately,
[Cleat #301](https://github.com/flotilla-org/cleat/issues/301) owns coherent
reset/document/screen-transition identity. Without that signal, identical-text
resets and transitions coalesced between render pulls remain undetectable.
Render generation or repaint alone must not be treated as content identity.

## OSC 52 transport and desktop consumption

[Cleat #240](https://github.com/flotilla-org/cleat/issues/240) landed in
[Cleat PR #304](https://github.com/flotilla-org/cleat/pull/304), commit
`00c072b`. It registers Ghostty's write effect, copies borrowed callback data
into bounded owned storage, and relays live text/clear events through providers
and functional `cleat attach`. The attach CLI reconstructs output through
`PacketTerminalRenderer`, so it emits OSC 52 from those effects instead of
forwarding the child's original byte stream.

This changes provider ABI 10 to 11 and packet protocol 11 to 12. Cleat also
pins Ghostty `c361de9`, whose completion metadata distinguishes fire-and-forget
OSC 52 from writes requiring completed clipboard I/O. Reads remain disabled;
acknowledged Kitty writes are rejected rather than accepted into an async queue.
Queue bounds, controlling-recipient selection and disconnect/takeover semantics
are defined in [Cleat's clipboard contract](https://github.com/flotilla-org/cleat/blob/00c072b207dc943f6c93fe3b6b09abaa257695a6/docs/clipboard-effects.md).

[Wheelhouse #71](https://github.com/flotilla-org/wheelhouse/issues/71) is now
unblocked for implementation. It must adopt the merged pin/ABI, drain owned
provider effects independently of rendering, and deliver eligible writes to the
platform clipboard. The intended route is:

```text
TUI -> inner Cleat -> live clipboard effect -> cleat attach CLI
    -> OSC 52 on the SSH/PTY stream -> outer embedded Cleat
    -> provider effect -> Wheelhouse -> desktop clipboard
```

Every Cleat boundary on that route needs a compatible deployed version; merged
source alone does not upgrade a running crew or daemon. The consumer pin also
brings connect-only attachment, controller-only session end and image backing
APIs. Only the clipboard adoption belongs in #71 unless another export is
required. Sequence protocol-12 daemon/fleet deployment with Wheelhouse adoption;
old daemons are rejected. Direct remote attachment removes the re-encoding hop
later, but is not a prerequisite for this route.

The first host policy allows supported writes only from the live controller of
the active input-owning Terminal View in the active application window, with a
setting to deny application writes. Watchers, inactive views/windows, previews,
hydration and retained/replayed documents must drop effects, not save them for
later focus. Repaint, resize, reconnect and hosting transfer must not repeat an
old write. Clipboard reads stay disabled. Supporting OSC 52 writes must not
implicitly enable queries or asynchronous acknowledged protocols.

Keep effect identity and consumption separate from render feeds, whose updates
can be coalesced, deferred for previews or replayed. Validate destination,
representation and host bounds before clipboard mutation. Unsupported requests
leave the clipboard intact; explicit clear targets the requested destination.
OSC 52 has no acknowledgement or retry guarantee across disconnects.

## Edit menu and command routing

[Edit menu #152](https://github.com/flotilla-org/wheelhouse/issues/152) remains
open. The shared shell already defines Copy, Cut, Paste, Select All, Undo and
Redo, but its menu specs expose Window, Panel, Tab and Help. Use shared specs
and command eligibility for native, full and compact Edit menus.

Eligibility and delivery must agree with the active input owner. Text fields
edit their contents; terminal Copy uses a valid local selection; terminal Paste
uses structured input. Initial terminal Select All selects the rendered
viewport, with that scope documented, rather than translating generic
whole-range navigation into Home/End. Cut, Undo and Redo are unavailable for
terminal output. TUI-owned copying remains an application action delivered via
OSC 52.

Replace duplicate inline C/V matching with the semantic commands and configurable
bindings used by menus. Preserve plain Ctrl-C/Ctrl-V for applications and
Ctrl-Shift-C/V defaults on other platforms. Host-owned chords must consume both
press and release so Kitty keyboard reporting does not receive unmatched releases.
The native menu model needs explicit enabled state shared with custom menus and
dispatch. Disable Paste for stopped/disconnected daemon sessions where the
provider reports availability. At the current pin, in-process sessions always
report streaming; their Paste eligibility cannot detect exit until Cleat #277.

## Remaining delivery and evidence

1. Edit commands and menus (#152): test real dispatch, text-field versus terminal
   ownership, remapped shortcuts, bracketed Paste, disabled actions and releases.
2. Wheelhouse clipboard consumption (#71): adopt Cleat #304 and test direct
   child, local attach, SSH/Flotilla attach and the actual TUI Copy action.
   Cover invalid/oversized input, clear requests, policy rejection, watchers,
   previews, reconnect and replay without repeated writes. Ordinary automated
   tests use fake sinks; isolated native checks verify platform clipboards.
3. Shared tracked selection and coherent mapping identity (Cleat #300/#301).
   These remain separate from the bounded stale-selection fix.

PR #154's lifetime, extraction and ordered event diagnostics passed, including
mutants which detect stale Copy and ownership regressions. The operator checked
the mouse-tracking fixture and confirmed Shift-drag/Cmd-C through the nested
Flotilla/SSH/Cleat route. That is Wheelhouse-local Copy of rendered text; it does
not establish OSC 52 from Codex or Claude. The full physical stress matrix was
not manually repeated. PR #156's build, real AppKit and PTY-paste checks pass;
its physical acceptance checklist remains pending.

The original disposable Ghostty C probe passed nine decoder checks without
mutating the desktop clipboard. Cleat #304 subsequently validated real VT,
provider, packet, attach and enclosing-VT boundaries, including nested local
attachment. Its desktop paste, SSH/Flotilla and actual fullscreen TUI Copy
checks remain a call for human acceptance. Wheelhouse end-to-end clipboard
acceptance has not yet been established.
