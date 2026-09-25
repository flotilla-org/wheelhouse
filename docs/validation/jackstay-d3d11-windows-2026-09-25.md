# Jackstay D3D11 frames on Windows, 2026-09-25

Live run on Beaufort (Windows 11 Pro 10.0.26200, RDP session, unlocked
throughout) for
[Import Jackstay D3D11 frames on Windows](https://github.com/flotilla-org/wheelhouse/issues/68).
Wheelhouse was built by `build.bat` against Jackstay `5f38b72`
([jackstay#48](https://github.com/flotilla-org/jackstay/pull/48), C ABI 0.11 on
top of main's 0.10 D3D11 calls) with `backend-windows`, and Porthole main
`7c46e02` (#188). Cleat and Andamento were at the revisions in `build.yml`;
Cleat was built without Ghostty and no terminal pane was used. All dependency
checkouts and target directories were fresh worktrees used by nothing else.

DXGI adapters on Beaufort (from Jackstay's evidence):
`00000000:00009fe5` AMD Radeon(TM) Graphics (the default),
`00000000:7d738999` Microsoft Remote Display Adapter, and
`00000000:0000ba31` Microsoft Basic Render Driver (WARP).

Screenshots are `PrintWindow` captures of the Wheelhouse window this run
launched, read by hand. They stay in the session scratchpad and are not
repository artifacts. Nothing else was captured, no other window was touched,
and no whole-screen capture was used.

## Porthole native capture session

An isolated daemon (`windows_capture_server` on `\\.\pipe\porthole-whw2-<random>`,
in-memory policy store), as in Porthole's
`scripts/windows-native-capture-smoke.ps1`. A temporary identity launched
Porthole's `capture_fixture` (a window cycling red, green and blue each
second, resizing itself from 320x200 to 600x200 after 30 s); only that
identity's requests were approved (launch, then `observe` + `record` on the
one surface). `capture-session surface <id> --native` started the session:
`ready`, `publication=d3d11 on adapter 00000000:00009fe5 (AMD Radeon(TM)
Graphics)`, `border=hidden`. Wheelhouse ran with a layout holding
`porthole_endpoint`, `porthole_session`, `attach_token` and `connect: 1`.

| Step | Observed |
| --- | --- |
| Wrong token (`ptas_wrong`) | `Porthole refused: attach request is not authorized for this capture session`; no frames. |
| Right token | `Live | Observation only | 320x200 D3D11 import on 00000000:00009fe5 AMD Radeon(TM) Graphics`. The pane showed the fixture's green, then blue a second later. |
| Fixture resize | Porthole reported `ready 600x200`, same epoch. The pane showed a wide red 3:1 image and `600x200 D3D11 import ...`. |
| Pane resize (window 1500x760 to 900x900) | The image re-fitted the narrower pane and stayed live. |
| Producer kill (the isolated daemon, which owns the capture, device, pool and fences) | `Disconnected; reconnecting video`, the last frame dimmed; still so 6 s later, with reconnect attempts failing quietly on the vanished endpoint. |
| Close | Wheelhouse exited on `WM_CLOSE`. The saved layout kept `porthole_endpoint` and `porthole_session` but neither `attach_token` nor `connect`. |

Afterwards the fixture process was stopped. The identity and grants existed
only in the killed daemon's memory.

## Jackstay `d3d11_source` directly

Jackstay's example on a user-scope endpoint, Wheelhouse with
`d3d11_endpoint` and `connect: 1`.

| Step | Observed |
| --- | --- |
| `--source window --resize-every-ms 5000` (WGC of the example's own window) | `D3D11 import on 00000000:00009fe5 AMD Radeon(TM) Graphics`; the pane followed recolouring and switched between 480x270 and 720x270. |
| Pane resize to 1500x700 | Re-fitted, live. |
| Producer killed | `Disconnected; reconnecting video`, last frame dimmed. |
| New producer on WARP (`--adapter warp`, synthetic) on the same name | Reconnected by itself: `D3D11 read back on 00000000:0000ba31 Microsoft Basic Render Driver: the renderer is on another adapter (renderer 00000000:00009fe5)`, showing the gradient, bar and frame bits. |
| Wheelhouse restarted with `--render_adapter:00000000:0000ba31` | The whole UI ran on WARP: `D3D11 import on 00000000:0000ba31 Microsoft Basic Render Driver`. |

The source logged `setup ended: Ok(())` for both Wheelhouse clients.

## Acceptance suite

`python tools\test-jackstay.py` passes on Beaufort: the CPU cases, then
`d3d11_source` resizing every 300 ms, imported on the default adapter
(12 frames of two sizes, a kill and restart, 7 more), with the renderer claiming
another adapter (read back), and with no renderer device (read back).

## Not covered

- The abandoned-readiness-fence branch was not seen live: in these kills setup
  closure was observed first, as in Jackstay's own viewer run. The branch
  discards the frame and keeps the last good one.
- A Porthole session publishing CPU frames (a device without shared fences):
  every adapter here shares fences. That path runs the existing CPU setup on
  the same connection.
- Device-loss epochs, lock and RDP disconnect (Porthole and Jackstay test these
  with injected conditions only).
- The Remote Display Adapter as the renderer's adapter.
