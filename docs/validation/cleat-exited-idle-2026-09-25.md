# Exited-terminal idle validation, 2026-09-25

Wheelhouse now pins Cleat `081c566725771ae6bd64fdfc4da82683b85079e8`
(PR #246). This also includes the wake re-arming and PNG development-build
changes from Cleat #239.

Built on macOS in a separate worktree with isolated Cleat and Andamento target
directories. Verified the executable links the isolated Cleat dylib. Sidebar,
panel, managed-content, preview, scroll-region, tooltip and terminal-glyph native
diagnostics all passed.

An isolated Wheelhouse window displayed 12 terminal views. Each command printed
a marker, wrote a completion file, and exited. All 12 completion files appeared.
After exit, three short CPU observations were 0.0%, 6.6% and 11.5%. A subsequent
three-second sample showed all 12 session actors parked on their command channels;
none was in the PTY kqueue wait loop. This is a controlled fixture, not a repeat
of the original daily-driver workload, which had measured 675–883% CPU with 11
spinning actors. The test window was closed afterward.

Local evidence for this session is under `/tmp/wh-exited-idle-f8w205_7`, including
`idle.sample.txt`, diagnostic logs and the fixture configuration. The build log
is `/tmp/wheelhouse-exited-idle-build.log`. These temporary files are not durable
repository artifacts.

The fix stops post-exit polling. Retained terminal views still own screen/history
and a parked actor; further resource retirement is separate work. It does not
include the synchronized-output cursor-flicker investigation (Cleat #242).
