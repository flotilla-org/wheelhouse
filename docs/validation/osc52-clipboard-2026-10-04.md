# Live terminal application clipboard writes

Wheelhouse consumes Cleat's owned live clipboard events once, on the UI thread,
after native events/commands and before rendering windows. Every initialized
Terminal View is registered, including hidden workspaces. A wake with no dirty
cells drains the same queue as any other wake. Acquisition and release are
independent of snapshots, rendering credit, preview budgets and selection caches.
There is no escape parser, recording consumer or clipboard-read callback here.

Default eligibility requires the currently focused native window, its selected
workspace and Active Panel's Selected View, a recently built input-owning Terminal
View, current controller role and provider clipboard capability. Query/menu/hover
focus, overview, diagnostic replay, retained fixtures and replaced runtimes deny
effects. The host setting `deny_application_clipboard_writes:1` in the **user**
bucket denies both writes and clears; workspace configuration cannot override it.
Operator selection Copy and structured Paste retain their separate input paths.
Focused local or remote applications may replace the clipboard with bounded UTF-8
text, including control characters and newlines; the host does not sanitize text.
The default-on policy is the initial #71 contract.

Cleat delivers to only one controlling attachment. References to the same provider
session handle share one destructive queue and identity watermark. Separate handles
rely on Cleat's sole-recipient policy. Suppressed events are released and identities
consumed immediately, never deferred until later focus. Session destruction removes
its registered runtime before destroying the provider. Managed replacement clears
old focus and consumption state. Transfer/reconnect freshness also relies on the
shared Cleat contract clearing pending receipts and changing activation/actor IDs;
Wheelhouse rejects duplicate sequences and older activations within an actor.

The host validates UTF-8 (including NUL, overlong, surrogate and range rejection),
kind/destination, clear shape, 64 KiB text, 16 events and 256 KiB text per drain.
Unsupported representations are rejected by Cleat before acquisition. The first
four rejection observations per runtime log a reason and loss delta without payloads; rejection and upstream
loss counters remain bounded. Native write failure consumes the event too.

## Version boundary

Use Cleat `00c072b207dc943f6c93fe3b6b09abaa257695a6`, provider ABI **11** and packet
protocol **12**. Wheelhouse's compilation rejects older provider headers and its
provider requests ABI 11. It cannot attach to protocol-11 daemons. Coordinate the
fleet Cleat roll with the operator's daily-driver rollout, as with #97; the PR
and these checks do not restart a daily driver. The pin brings Ghostty
`rjwittams/ghostty@c361de9691f006f65c400be73896d1e48a8ec56c`; the workflow's existing
preparation scripts read that revision from the pinned Cleat toolchain file.

## Platform boundaries

- macOS: `NSPasteboardTypeString` on the general pasteboard or Ghostty's named
  `com.mitchellh.ghostty.selection` pasteboard. Clear uses `clearContents` without
  installing an empty text representation. UTF-8 conversion precedes mutation.
- Windows: `CF_UNICODETEXT` with allocation/conversion before opening or emptying
  the clipboard, and the active native owner HWND. Selection uses the existing process-local
  buffer (Windows has no native PRIMARY). LF is preserved, matching the existing
  selection Copy adapter; no implicit CRLF normalization is added. Verify multiline
  text with the external reader during acceptance. Clear calls `EmptyClipboard`, or empties
  the selection buffer.
- Linux: the existing X11 CLIPBOARD owner serves small UTF8_STRING requests;
  clear relinquishes the standard selection. Selection is process-local. This does
  **not** claim external reads, X11 PRIMARY or large INCR support. See
  [#150](https://github.com/flotilla-org/wheelhouse/issues/150). A headless stub
  returns failure and never claims platform delivery.

## Automated checks

Use pinned sibling dependencies and disposable settings. Linux uses
`WHEELHOUSE_CLEAT_FEATURES=none` and the same production consumer with fake provider
queue/desktop sinks, so no VT or physical GUI is required for clipboard checks:

```sh
WHEELHOUSE_CLEAT_FEATURES=none bash build.sh wheelhouse
./build/wheelhouse --terminal_clipboard_diagnostics
./build/wheelhouse --terminal_selection_diagnostics
python3 tools/check-generated.py
```

The clipboard diagnostic exercises the production dispatch and real configuration
resolver, Unicode/destinations/clear, malformed/unsupported/oversized inputs,
queue bounds, host deny, all eligibility bits, effect-only dispatch, modal focus,
workspace/tab changes before drain, inactive window aliases, watcher demotion,
reconnect activation, hosting actor change, replacement, hydration, replay,
provider loss and native refusal. Every acquisition is released; no test touches
a desktop clipboard. Existing scroll diagnostics retain ordered selection Copy,
rectangular selection and structured bracketed Paste checks.

Linux vessel results: native build, clipboard and selection diagnostics,
scroll-region UI/input diagnostics, launch-environment regression and Windows
input-readiness source checks pass. Generated sources match. macOS/Windows native
compilation is a CI gate after the Governor applies the required pin; it is not
physical desktop acceptance.

## Operator desktop acceptance (required before settlement)

Run a PR build in a separate instance with disposable `--user`/`--project` settings
and isolated Cleat runtime. Never replace or restart the daily driver. Writes and
clears intentionally change the test desktop clipboard, so do this explicitly
outside ordinary automated tests. Record platform/build and external reader result.

1. Focus a direct child Terminal View and execute:
   `printf '\033]52;c;aGVsbG8=\007'`. Paste into another application (macOS can also
   use `pbpaste`) and verify **hello**. Repeat with ST termination and Unicode:
   `printf '\033]52;c;w6kg5LitIPCfmIA=\033\\'` gives **é 中 😀**.
2. Clear with `printf '\033]52;c;\007'`; verify the external application/reader
   sees an empty clipboard. On macOS also exercise destination `s` with the named
   selection pasteboard and verify it stays separate from the general pasteboard.
   Windows selection acceptance is an in-app middle-paste buffer check.
3. Repeat the payload/clear tests with local `cleat attach`, then with the actual
   current SSH/Flotilla attach route. Record all Cleat endpoints at protocol 12.
4. Perform a real Claude Code or Codex fullscreen Copy action in that route and
   verify the copied text in a separate application. This cannot be replaced by
   a simulated SSH transport or a successful clipboard API return.
5. In two windows/views, repeat with inactive input owners, watchers and overview
   previews; none may overwrite the sentinel clipboard. Change focus/role before
   draining; reconnect, resize and full refresh must not repeat an old write.
   Set user `deny_application_clipboard_writes:1` and verify operator selection
   Copy, rectangular Copy, selection middle-paste and bracketed Paste still work.
6. Send `printf '\033]52;c;?\007'` and verify no clipboard content is returned to
   the child (the host registers no read operation).

For a failure identify the first boundary: VT capability/OSC callback, Cleat event
queue and controlling recipient, attach relay, enclosing Wheelhouse provider
acquisition, host eligibility rejection log, native adapter refusal, or external
reader. Captures/render output are not clipboard delivery evidence.

Physical macOS/Windows, live SSH and fullscreen TUI acceptance remain **pending**
until the operator supplies results. Linux fixture sinks are not desktop evidence.
