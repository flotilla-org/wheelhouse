#!/usr/bin/env python3
"""Exercise Wheelhouse's consumer against separate Jackstay reference processes."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
JACKSTAY = Path(
    os.environ.get("WHEELHOUSE_JACKSTAY_DIR", ROOT.parent / "jackstay")
).resolve()
LIB = (
    Path(os.environ.get("WHEELHOUSE_JACKSTAY_TARGET_DIR", JACKSTAY / "target"))
    / "debug"
)

with tempfile.TemporaryDirectory(prefix="wh-js-", dir="/tmp") as temporary:
    work = Path(temporary)
    common = [
        "clang",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-I" + str(ROOT / "src"),
        "-I" + str(JACKSTAY / "crates/jackstay/include"),
        "-L" + str(LIB),
        "-ljackstay",
        "-Wl,-rpath," + str(LIB),
    ]
    consumer = work / "consumer"
    source = work / "source"
    subprocess.run(
        common
        + [
            str(ROOT / "tools/test-jackstay-session.c"),
            str(ROOT / "src/jackstay/wheelhouse_jackstay.c"),
            "-o",
            str(consumer),
        ],
        check=True,
    )
    subprocess.run(
        common
        + [
            str(JACKSTAY / "tools/capture-viewer-sdl/src/input_source.c"),
            "-o",
            str(source),
        ],
        check=True,
    )

    def launch(path, observe_only=False):
        process = subprocess.Popen(
            [str(source), str(path), "--report-state"]
            + (["--observe-only"] if observe_only else []),
            stdout=subprocess.PIPE,
            text=True,
        )
        assert process.stdout.readline().strip() == "ready"
        return process

    for mode in ("input", "observe", "refused", "recovery", "missing"):
        path = work / mode
        producer = None if mode == "missing" else launch(path, mode == "refused")
        client = None
        try:
            client = subprocess.Popen(
                [str(consumer), str(path), mode], stdout=subprocess.PIPE, text=True
            )
            if mode == "recovery":
                assert client.stdout.readline().strip() == "restart-source"
                producer.kill()
                producer.wait()
                # The reference unlinks after admission; the restarted source owns a fresh endpoint.
                producer = launch(path)
            output, _ = client.communicate(timeout=25)
            print(output, end="")
            assert client.returncode == 0, (mode, client.returncode)
            if producer:
                report, _ = producer.communicate(timeout=5)
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
print("All Jackstay session acceptance checks passed")
