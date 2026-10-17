# Jackstay CPU frame ownership and Metal upload

CPU frames now retain their BGRA/RGBA format, publication cursor and source host
timestamp. The worker still copies out of the shared lease, so the producer can
reuse it independently. The GUI takes ownership of that private mailbox image
instead of allocating and copying another image under the session mutex.

The View selects the matching texture format. On macOS it uses the existing
shared-storage texture allocation path, which copies directly into Metal texture
storage without a staging blit or GUI-thread GPU completion wait. Every image
still gets a fresh texture; its bytes stay immutable and the renderer's existing
completion-based retirement handles its lifetime. Other backends retain their
existing allocation path. D3D11 readback/import retains the new metadata too.

## Checks on Apple M4, macOS 26.6

- Optimized application bundle build passed.
- `tools/test-jackstay.py` passed separate-process media, input, observation,
  refusal, restart, missing source, affordances, reset and cleanup checks, using
  both named endpoints and absolute socket paths.
- The mailbox regression runs at the actual worker/GUI seam: BGRA/RGBA bytes,
  padded row stride, pointer ownership transfer, version filtering, timestamps,
  and an older image remaining valid after a newer publication. It passed under
  AddressSanitizer too.
- `--jackstay_input_diagnostics` passed, including a new renderer readback check
  through the View's actual texture upload helper. Both RGBA and BGRA produce red
  and blue in the expected positions. The older texture remains correct after
  overwriting the CPU source and uploading another texture.
- `--jackstay_geometry_diagnostics` passed logical/backing scale checks.

A synthetic copy benchmark on this shared machine, 2948 × 2040 BGRA, measured
median worker posting time of 6.132 ms before and 2.162 ms after. Median GUI
snapshot time changed from 2.248 ms to 0.001 ms. This isolates CPU mailbox work;
it does not measure WebKit, host texture upload or actual display presentation.
The before/after JSONL and harness live in the local web-performance artifacts.

Native Windows D3D11 acceptance was not run on this Mac. The format and ownership
checks are portable and execute with the session harness on Windows as well.
No Jackstay ABI change is required.

This does not implement macOS IOSurface acquisition or expose a browser's GPU
backbuffer. Those remain separate from the CPU snapshot improvements here.
