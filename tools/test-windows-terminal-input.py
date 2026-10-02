#!/usr/bin/env python3
"""Exercise native window messages through a real Cleat terminal and VT encoder.

Uses only a window and child owned by this test; never moves the desktop pointer.
Run on Windows after build.bat wheelhouse, optionally passing an executable.
"""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def publish(directory, name, text):
    pending = directory / (name + ".tmp")
    pending.write_text(text)
    pending.replace(directory / name)


def mouse_modes(data):
    return {int(mode): int(state) for mode, state in
            re.findall(rb"\x1b\[\?(\d+);(\d+)\$y", data)}


def terminal_ready(data):
    modes = mouse_modes(data)
    flags = re.findall(rb"\x1b\[\?(\d+)u", data)
    return (all(modes.get(mode) in (1, 3) for mode in (1003, 1006, 1016)) and
            bool(flags) and int(flags[-1]) & 3 == 3)


def capture_input(directory):
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.GetStdHandle.restype = W.HANDLE
    kernel.SetConsoleMode.argtypes = [W.HANDLE, W.DWORD]
    kernel.ReadFile.argtypes = [W.HANDLE, C.c_void_p, W.DWORD,
                               C.POINTER(W.DWORD), C.c_void_p]
    handle = kernel.GetStdHandle(-10)
    if not kernel.SetConsoleMode(handle, 0x200):  # raw VT input
        raise C.WinError(C.get_last_error())
    publish(directory, "child-pid", str(os.getpid()))
    # Kitty key event types and SGR pixel mouse tracking.
    # A flush only queues output. Queries make readiness a round trip through
    # the real VT parser and ConPTY input path, after all modes are enabled.
    sys.stdout.write("\x1b[>3u\x1b[?1003h\x1b[?1006h\x1b[?1016h"
                     "\x1b[?1003$p\x1b[?1006$p\x1b[?1016$p\x1b[?u")
    sys.stdout.flush()
    replies = bytearray()
    ready = False
    with (directory / "input.bin").open("wb", buffering=0) as output:
        while True:
            buf, count = C.create_string_buffer(4096), W.DWORD()
            if not kernel.ReadFile(handle, buf, len(buf), C.byref(count), None):
                break
            if not count.value:
                break
            chunk = buf.raw[:count.value]
            output.write(chunk)
            if not ready:
                replies.extend(chunk)
                if terminal_ready(replies):
                    publish(directory, "ready", str(os.getpid()))
                    ready = True
                    sys.stdout.write("Ready\r\n")
                    sys.stdout.flush()


def wait_for(predicate, description, timeout=15, diagnostics=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    details = "\n" + diagnostics() if diagnostics else ""
    raise AssertionError(f"timed out after {timeout:g}s waiting for {description}{details}")


def check(executable):
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenProcess.restype = W.HANDLE
    kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
    kernel.CloseHandle.argtypes = [W.HANDLE]
    user = C.WinDLL("user32", use_last_error=True)
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.IsWindowVisible.argtypes = [W.HWND]
    user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    user.GetForegroundWindow.restype = W.HWND
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    failures = []
    with tempfile.TemporaryDirectory(prefix="wheelhouse-input-") as tmp:
        directory = Path(tmp)
        expression = subprocess.list2cmdline([
            sys.executable, str(Path(__file__).resolve()), "--child", tmp])
        expression = expression.replace("\\", "\\\\").replace('"', '\\"')
        (directory / "user").write_text(
            '// uishell 0.1.0 user file\nwindow:\n{\n  size: 900 600\n'
            '  panels:\n  {\n    1.0:\n    {\n      terminal:\n      {\n'
            f'        expression: "{expression}"\n        selected\n'
            '      }\n      selected\n    }\n  }\n}\n')
        (directory / "project").write_text("// isolated input regression\n")
        started = time.monotonic()
        process = subprocess.Popen([str(executable), f"--user:{tmp}/user",
                                    f"--project:{tmp}/project"], cwd=executable.parent)
        child_handle = None
        child_pid = None
        hwnd = None
        try:
            def window():
                windows = []

                @callback_type
                def visit(hwnd, _):
                    pid = W.DWORD()
                    user.GetWindowThreadProcessId(hwnd, C.byref(pid))
                    if pid.value == process.pid and user.IsWindowVisible(hwnd):
                        rect = W.RECT()
                        if user.GetWindowRect(hwnd, C.byref(rect)):
                            area = (rect.right - rect.left) * (rect.bottom - rect.top)
                            windows.append((area, hwnd))
                    return True

                user.EnumWindows(visit, 0)
                return max(windows)[1] if windows else None

            def data():
                path = directory / "input.bin"
                return path.read_bytes() if path.exists() else b""

            def diagnostics():
                rect = W.RECT()
                size = None
                if hwnd and user.GetClientRect(hwnd, C.byref(rect)):
                    size = (rect.right - rect.left, rect.bottom - rect.top)
                received = data()
                return (f"elapsed={time.monotonic() - started:.3f}s "
                        f"wheelhouse_pid={process.pid} exit={process.poll()} "
                        f"hwnd={hwnd} visible={bool(hwnd and user.IsWindowVisible(hwnd))} "
                        f"foreground_hwnd={user.GetForegroundWindow()} client_size={size}\n"
                        f"child_pid={child_pid} child_wait="
                        f"{kernel.WaitForSingleObject(child_handle, 0) if child_handle else None} "
                        f"ready={(directory / 'ready').exists()} "
                        f"mouse_modes={mouse_modes(received)}\n"
                        f"received={received[-1024:]!r} ({len(received)} bytes total)")

            def wait(predicate, description):
                return wait_for(predicate, description, diagnostics=diagnostics)

            hwnd = wait(window, "test window")
            pid_file = directory / "child-pid"
            child_pid = int(wait(lambda: pid_file.read_text() if pid_file.exists() else "",
                                 "VT input fixture process"))
            child_handle = kernel.OpenProcess(0x100000, False, child_pid)
            if not child_handle:
                raise C.WinError(C.get_last_error())
            ready = directory / "ready"
            wait(lambda: ready.exists(), "VT input fixture mode acknowledgements")
            print(f"READY: terminal modes acknowledged after {time.monotonic() - started:.3f}s")
            def post(message, wparam, lparam):
                if not user.PostMessageW(hwnd, message, wparam, lparam):
                    raise C.WinError(C.get_last_error())

            def click(x, y):
                post(0x201, 1, x | (y << 16))
                post(0x202, 0, x | (y << 16))

            pressed = time.monotonic()
            click(450, 200)  # clear of the sidebar; also focus the terminal view
            # Win32 client coordinates and Cleat's SGR-pixel coordinates have the
            # same physical pixel unit, even when the display uses DPI scaling.
            wait(lambda: b"\x1b[<0;" in data(), "first mouse press")
            print(f"READY: first mouse input after {time.monotonic() - pressed:.3f}s")
            click(570, 240)
            def left_presses():
                return [(int(x), int(y)) for x, y in
                        re.findall(rb"\x1b\[<0;(\d+);(\d+)M", data())]
            mouse = wait(lambda: left_presses() if len(left_presses()) >= 2 else None,
                             "two mouse presses")
            if len(mouse) < 2 or (mouse[-1][0] - mouse[-2][0],
                                  mouse[-1][1] - mouse[-2][1]) != (120, 40):
                failures.append(f"mouse presses lost message coordinates: {mouse!r}")
            else:
                print("PASS: mouse presses retain message coordinates")

            # Consecutive messages can share one UI frame. Every button event
            # must survive, with the release's own position and original order.
            start = len(data())
            for down, up, button, flag in [(0x201, 0x202, 0, 1),
                                          (0x207, 0x208, 1, 0x10),
                                          (0x204, 0x205, 2, 2)]:
                post(down, flag, 450 | (260 << 16))
                post(up, 0, 490 | (280 << 16))
            def button_events():
                return [(int(b), int(x), int(y), action) for b, x, y, action in
                        re.findall(rb"\x1b\[<([012]);(\d+);(\d+)([Mm])", data()[start:])]
            buttons = wait(lambda: button_events() if len(button_events()) >= 6 else None,
                               "six ordered button events")
            if ([(b, a) for b, _, _, a in buttons] !=
                    [(b, a) for b in range(3) for a in (b"M", b"m")] or
                    any((buttons[i+1][1] - buttons[i][1],
                         buttons[i+1][2] - buttons[i][2]) != (40, 20)
                        for i in range(0, len(buttons) - 1, 2))):
                failures.append(f"batched mouse presses/releases changed: {buttons!r}")
            else:
                print("PASS: all three mouse buttons retain event order and release coordinates")

            for vk, suffix in [(0x25, b"D"), (0x26, b"A"),
                               (0x27, b"C"), (0x28, b"B")]:
                start = len(data())
                post(0x100, vk, 1 | (1 << 24))
                post(0x101, vk, 1 | (1 << 24) | (3 << 30))
                press = b"\x1b[1;1:1" + suffix
                release = b"\x1b[1;1:3" + suffix
                received = wait(lambda: data()[start:] if release in data()[start:] else None,
                                    "arrow release")
                if press not in received or release not in received:
                    failures.append(f"arrow {suffix!r} missing press/release: {received!r}")
                else:
                    print(f"PASS: arrow {suffix.decode()} press and release")
        finally:
            # Only the process tree created by this invocation is terminated.
            if process.poll() is None:
                killed = subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if killed.returncode and process.poll() is None:
                    raise AssertionError("test-owned Wheelhouse process survived cleanup")
            process.wait(timeout=10)
            if child_handle:
                result = kernel.WaitForSingleObject(child_handle, 5000)
                kernel.CloseHandle(child_handle)
                if result != 0:
                    raise AssertionError("input fixture did not exit during cleanup")
    for failure in failures:
        print("FAIL:", failure, file=sys.stderr)
    return bool(failures)


if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("Windows-only native input test")
    if sys.argv[1:2] == ["--child"]:
        capture_input(Path(sys.argv[2]))
    else:
        executable = (Path(sys.argv[1]) if len(sys.argv) > 1 else
                      Path(__file__).resolve().parents[1] / "build/wheelhouse.exe")
        raise SystemExit(check(executable.resolve()))
