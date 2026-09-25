# Jackstay CPU source on Windows, 2026-09-25

First Windows run of a Wheelhouse Jackstay view, on Beaufort (Windows 11), for
[Show a Jackstay CPU source on Windows](https://github.com/flotilla-org/wheelhouse/issues/67).
Wheelhouse was built by `build.bat` against Jackstay
`ad1e6c29b7fd2e73e7816d380fd185b781ac3e66`
([jackstay#42](https://github.com/flotilla-org/jackstay/pull/42), on C ABI 0.9),
with Cleat and Andamento at the revisions in `build.yml`. Cleat was built without
Ghostty; no terminal pane was used. Dependencies and target directories were
fresh worktrees used by nothing else.

## Setup

- The producer was Jackstay's `capture-input-source`, built with MSVC and run as
  a separate process on the user-scope Local Endpoint `whw1-9a07f65a57`
  (`\\.\pipe\jackstay.<SID>.whw1-9a07f65a57`) with `--log-input`.
- Wheelhouse ran with temporary `--user`/`--project` files. The user file held
  one Jackstay view with `source_endpoint: "whw1-9a07f65a57"`.
- A private Porthole `windows_foreground_server` ran with its own pipe and an
  in-memory policy. A temporary identity launched Wheelhouse and had observe,
  drive and manage grants for that one window only. Porthole took the
  screenshots (PrintWindow of that window) and typed the text and keys.
- Porthole's Windows adapter has no pointer or placement support. Clicks
  (Connect, the canvas) were posted as `WM_LBUTTONDOWN`/`UP` to our own window,
  and the pane resize used `SetWindowPos` on it. The global cursor was not moved.

Afterwards the window was closed through Porthole, and the producers were
stopped. The identity was revoked and the server stopped. No other window was
touched and nothing captured the whole screen.

## Observed

I read each screenshot. Frames are the reference's moving gradient with a white
pointer square at the top left; committed text grows a white bar at the bottom.

1. The form showed the endpoint name from the layout. After Connect, the status
   read `Live | Control ready` and the source's frames filled the canvas.
2. After a click on the canvas, Porthole typed `hello jackstay` and sent
   `ArrowLeft`, `Enter` and `KeyQ`. The producer logged each text commit and
   `input key ArrowLeft down/up`, `Enter down/up`, `KeyQ down`, `text "q"`,
   `KeyQ up`; the bottom bar grew.
3. The window was resized from 1100x760 to 820x900. The image re-fitted the
   narrower pane and stayed live. Text typed afterwards (`after resize`) was
   logged.
4. The producer was killed. The status became
   `Disconnected; reconnecting video | Control disconnected` over the dimmed
   last frame.
5. A new producer on the same name, with `--repeat --resize-every-ms 5000`, was
   started. Video resumed as observation (`Live`), without reacquiring control,
   as designed. A click on the canvas reconnected with input (`session 2:
   viewer admitted, input offered`) and the status returned to
   `Live | Control ready`.
6. `typed after producer restart` and `KeyW` reached the new producer's log.
7. The source alternated 320x180 and 480x360 frames. The first growth replaced
   the allocation, whose handles were duplicated into Wheelhouse. Screenshots
   showed the image switch between 16:9 and 4:3 while control stayed ready. Each
   size change sent a pointer-scope geometry reset (`input cleanup scope=2
   reason=2`). Text typed after the switch (`!`) was logged.
8. Closing the window through Porthole ended the view cleanly. The producer
   logged `input cleanup scope=1 reason=3` (all held input, disconnect) and
   `session 2 ended`. The Wheelhouse process exited.

An earlier producer without `--repeat` exited when the reconnect cancelled its
only session. That is the reference's one-viewer default, not a Wheelhouse
fault, and led to adding `--repeat` in jackstay#42.

`tools/test-jackstay.py` also passes on Beaufort over named pipes: input,
observation, refused input, source-death recovery and a missing endpoint.

## Not covered

Pointer input in the live window: button and motion events need the real cursor
over the window, which this run did not move. Button delivery with coordinates is
covered by the session acceptance instead. Also not covered: Session-scope
endpoints, a producer in another Windows session, D3D11 frames
([#68](https://github.com/flotilla-org/wheelhouse/issues/68)), and RDP/lock
continuity.

Screenshots and producer logs stay on Beaufort in the session scratchpad
(`w1\run`) and are not durable repository artifacts.
