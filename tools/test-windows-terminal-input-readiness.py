#!/usr/bin/env python3
"""Check fixture readiness with fragmented terminal replies, on any platform."""
import ctypes as C
from ctypes import wintypes as W
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "terminal_input", Path(__file__).with_name("test-windows-terminal-input.py"))
fixture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixture)


class ReadinessTest(unittest.TestCase):
    def capture(self, chunks, ready_before_read):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            ready = directory / "ready"
            index = 0

            def read(handle, buf, size, count, overlapped):
                nonlocal index
                self.assertEqual(ready.exists(), ready_before_read[index],
                                 f"readiness before input chunk {index}")
                if index == len(chunks):
                    return False
                chunk = chunks[index]
                C.memmove(buf, chunk, len(chunk))
                C.cast(count, C.POINTER(W.DWORD))[0] = len(chunk)
                index += 1
                return True

            class Kernel:
                pass

            kernel = Kernel()
            kernel.GetStdHandle = lambda _: 1
            kernel.SetConsoleMode = lambda *args: True
            kernel.ReadFile = read
            output = io.StringIO()
            with patch.object(C, "WinDLL", return_value=kernel, create=True), \
                    patch.object(fixture.sys, "stdout", output):
                fixture.capture_input(directory)
            self.assertEqual((directory / "input.bin").read_bytes(), b"".join(chunks))
            if ready.exists():
                self.assertEqual(ready.read_text(), str(os.getpid()))
            return output.getvalue()

    def test_ready_waits_for_all_mode_replies_even_when_fragmented(self):
        chunks = [b"\x1b[?1003;1$y\x1b[?10", b"06;1$y\x1b[?1016;1$y",
                  b"\x1b[?", b"3u"]
        output = self.capture(chunks, [False, False, False, False, True])
        for mode in (1003, 1006, 1016):
            self.assertIn(f"\x1b[?{mode}$p", output)
        self.assertIn("\x1b[?u", output)

    def test_a_reset_mouse_mode_is_not_ready(self):
        self.capture([b"\x1b[?1003;1$y\x1b[?1006;2$y\x1b[?1016;1$y\x1b[?3u"],
                     [False, False])

    def test_a_missing_key_event_flag_is_not_ready(self):
        self.capture([b"\x1b[?1003;1$y\x1b[?1006;1$y\x1b[?1016;1$y\x1b[?1u"],
                     [False, False])

    def test_view_waits_for_marker_in_render_trace(self):
        marker = fixture.READY_MARKER.encode()
        self.assertFalse(fixture.view_ready(b'child stdout text="' + marker + b'"'))
        self.assertFalse(fixture.view_ready(b'terminal glyph trace: text="Starting"'))
        self.assertTrue(fixture.view_ready(b'terminal glyph trace: gen=2 text="' +
                                           marker + b'   " cps=[...]'))

    def test_timeout_reports_the_stage_and_diagnostic_snapshot(self):
        with self.assertRaises(AssertionError) as failure:
            fixture.wait_for(lambda: False, "first mouse press", timeout=0,
                             diagnostics=lambda: "foreground_hwnd=123 received=b'partial'")
        self.assertIn("first mouse press", str(failure.exception))
        self.assertIn("foreground_hwnd=123", str(failure.exception))
        self.assertIn("received=b'partial'", str(failure.exception))


if __name__ == "__main__":
    unittest.main()
