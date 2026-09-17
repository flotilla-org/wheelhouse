"""Interrupt an isolated probe connection and require automatic recovery."""

import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time

if len(sys.argv) != 4:
    raise SystemExit("usage: probe-cleat-reconnect.py PROBE RUNTIME_ROOT SESSION_ID")
upstream = str(pathlib.Path(sys.argv[2]) / "wh-image-baseline/socket")
with tempfile.TemporaryDirectory(prefix="wh-reconnect-", dir="/tmp") as runtime_root:
    path = pathlib.Path(runtime_root) / "wh-image-baseline"
    path.mkdir()
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(path / "socket"))
    listener.listen()
    active = []
    stopping = threading.Event()
    blocked = threading.Event()

    def relay(source, destination):
        try:
            while data := source.recv(65536):
                destination.sendall(data)
        except OSError:
            pass
        for connection in (source, destination):
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()

    def serve():
        while not stopping.is_set():
            try:
                source, _ = listener.accept()
            except OSError:
                return
            if blocked.is_set():
                source.close()
                continue
            destination = socket.socket(socket.AF_UNIX)
            destination.connect(upstream)
            active.extend([source, destination])
            threading.Thread(
                target=relay, args=(source, destination), daemon=True
            ).start()
            threading.Thread(
                target=relay, args=(destination, source), daemon=True
            ).start()

    threading.Thread(target=serve, daemon=True).start()
    env = dict(os.environ, CLEAT_RUNTIME_DIR=runtime_root, WH_EXPECT_RECONNECT="1")
    probe = subprocess.Popen([sys.argv[1], "daemon", "unused", sys.argv[3]], env=env)
    time.sleep(1)
    blocked.set()
    for connection in list(active):
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
    time.sleep(1)
    blocked.clear()
    code = probe.wait(timeout=20)
    stopping.set()
    listener.close()
    raise SystemExit(code)
