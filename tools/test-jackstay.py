#!/usr/bin/env python3
"""Exercise Wheelhouse's consumer against separate Jackstay reference processes.

The consumer and source find each other by Local Endpoint name on every
platform. On macOS/Linux the input and recovery checks also run against an
absolute socket path, the form existing path-bound publications use.
On Windows run this from a Visual Studio developer prompt (cl on PATH).
"""

import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
JACKSTAY = Path(
    os.environ.get("WHEELHOUSE_JACKSTAY_DIR", ROOT.parent / "jackstay")
).resolve()
LIB = (
    Path(os.environ.get("WHEELHOUSE_JACKSTAY_TARGET_DIR", JACKSTAY / "target"))
    / "debug"
)
WINDOWS = sys.platform == "win32"
INCLUDE = JACKSTAY / "crates/jackstay/include"
SOURCE_C = JACKSTAY / "tools/capture-viewer-sdl/src/input_source.c"
SESSION_C = ROOT / "tools/test-jackstay-session.c"


def build(work):
    consumer = work / ("consumer.exe" if WINDOWS else "consumer")
    source = work / ("source.exe" if WINDOWS else "source")
    if WINDOWS:
        # Same compiler and flags as build.bat: the consumer is RAD C, not C11.
        subprocess.run(
            ["cl", "/nologo", "/Z7", "/Od", "/DBUILD_DEBUG=1", "/Zc:preprocessor",
             "/I" + str(ROOT / "src"), "/I" + str(INCLUDE), str(SESSION_C),
             "/Fo" + str(work) + "\\", "/Fe" + str(consumer),
             "/link", str(LIB / "jackstay.dll.lib")],
            check=True, cwd=work,
        )
        subprocess.run(
            ["cl", "/nologo", "/std:c11", "/W4", "/WX", "/I" + str(INCLUDE),
             str(SOURCE_C), "/Fo" + str(work) + "\\", "/Fe" + str(source),
             "/link", str(LIB / "jackstay.dll.lib")],
            check=True, cwd=work,
        )
        shutil.copy2(LIB / "jackstay.dll", work)
        return consumer, source
    link = ["-L" + str(LIB), "-ljackstay", "-Wl,-rpath," + str(LIB), "-lpthread", "-lm"]
    # The consumer includes RAD's base layer, built as Wheelhouse builds it.
    base = ["clang", "-g", "-DBUILD_DEBUG=1", "-D_GNU_SOURCE", "-Wall",
            "-Wno-unused-function", "-Wno-missing-braces", "-Wno-unused-variable",
            "-Wno-unused-but-set-variable", "-Wno-initializer-overrides",
            "-Wno-incompatible-pointer-types-discards-qualifiers",
            "-Wno-writable-strings", "-Wno-unknown-warning-option",
            "-I" + str(ROOT / "src"), "-I" + str(INCLUDE)]
    if sys.platform == "darwin":
        base += ["-x", "objective-c"]
    else:
        link += ["-lrt", "-ldl"]
    subprocess.run(base + [str(SESSION_C), "-x", "none"] + link + ["-o", str(consumer)],
                   check=True)
    subprocess.run(
        ["clang", "-Wall", "-Wextra", "-Werror", "-D_DARWIN_C_SOURCE",
         "-I" + str(INCLUDE), str(SOURCE_C)] + link + ["-o", str(source)],
        check=True,
    )
    return consumer, source


class Lines:
    """Reads a child's stdout on a thread; pipes cannot be select()ed on Windows."""

    def __init__(self, process):
        self.queue = queue.Queue()
        threading.Thread(target=self.pump, args=(process.stdout,), daemon=True).start()

    def pump(self, stream):
        for line in iter(stream.readline, b""):
            self.queue.put(line.decode())
        self.queue.put(None)

    def next(self, timeout=10):
        try:
            line = self.queue.get(timeout=max(timeout, 0))
        except queue.Empty:
            raise TimeoutError("Jackstay source did not report expected input")
        if line is None:
            raise AssertionError("Jackstay source exited before its state report")
        return line

    def rest(self):
        lines = []
        while True:
            line = self.queue.get(timeout=5)
            if line is None:
                return "".join(lines)
            lines.append(line)


def launch(source, address, observe_only=False):
    target = [str(address)] if isinstance(address, Path) else ["--endpoint", address]
    process = subprocess.Popen(
        [str(source)] + target + ["--report-state"]
        + (["--observe-only"] if observe_only else []),
        stdout=subprocess.PIPE,
    )
    process.lines = Lines(process)
    try:
        assert process.lines.next().strip() == "ready"
    except BaseException:
        process.kill()
        process.wait()
        raise
    return process


def run(consumer, source, mode, address):
    producer = None if mode == "missing" else launch(source, address, mode == "refused")
    client = None
    try:
        client = subprocess.Popen(
            [str(consumer), str(address), mode], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True
        )
        observed = ""
        if mode == "input":
            for marker, expected in (
                ("await-button-down", "state downs=1 repeats=1 releases=1 text_bytes=8 held=0 buttons=1"),
                ("await-key-down", "state downs=2 repeats=1 releases=1 text_bytes=8 held=1 buttons=0"),
            ):
                deadline = time.monotonic() + 10
                while True:
                    line = producer.lines.next(deadline - time.monotonic())
                    observed += line
                    if line.strip() == expected:
                        break
                assert client.stdout.readline().strip() == marker
                client.stdin.write("\n")
                client.stdin.flush()
        if mode == "recovery":
            assert client.stdout.readline().strip() == "restart-source"
            producer.kill()
            producer.wait()
            # The reference releases its endpoint after admission; the restarted
            # source binds the same address afresh.
            producer = launch(source, address)
        output, _ = client.communicate(timeout=25)
        print(output, end="")
        assert client.returncode == 0, (mode, address, client.returncode)
        if producer:
            producer.wait(timeout=5)
            report = observed + producer.lines.rest()
            assert producer.returncode == 0, report
            if mode == "input":
                assert (
                    "downs=2 repeats=1 releases=1 text_bytes=8 cleanup=2 held=0 buttons=0"
                    in report
                ), report
    finally:
        for process in (client, producer):
            if process and process.poll() is None:
                process.kill()
                process.wait()


with tempfile.TemporaryDirectory(prefix="wh-js-", dir=None if WINDOWS else "/tmp") as temporary:
    work = Path(temporary)
    consumer, source = build(work)
    for mode in ("input", "observe", "refused", "recovery", "missing"):
        run(consumer, source, mode, f"wh-js-{os.getpid()}-{mode}")
    if not WINDOWS:
        for mode in ("input", "recovery"):
            run(consumer, source, mode, work / mode)
print("All Jackstay session acceptance checks passed")
