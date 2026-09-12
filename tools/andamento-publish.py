#!/usr/bin/env python3
"""Publish live git facts to Wheelhouse without Flotilla or Zellij."""
import argparse
import http.client
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import time


class UnixHTTPConnection(http.client.HTTPConnection):
    def __init__(self, path):
        super().__init__("localhost", timeout=6)
        self.path = path

    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect(self.path)


def publish(path, patch):
    body = json.dumps(patch, separators=(",", ":")).encode()
    for attempt in range(2):
        connection = UnixHTTPConnection(path)
        try:
            connection.request("POST", "/v1/metadata/patch", body, {"Content-Type": "application/json"})
            response = connection.getresponse()
            response.read()
            if response.status == 204:
                return
            if response.status < 500:
                raise ValueError(f"patch rejected: HTTP {response.status}")
            raise OSError(f"HTTP {response.status}")
        except (OSError, http.client.HTTPException):
            if attempt:
                raise
            time.sleep(0.5)
        finally:
            connection.close()


def patch(kind, identity, facts):
    facts = {"entity.kind": kind, "entity.id": identity, **facts}
    return {"type": "metadata-patch", "target": {"kind": "entity", "value": {"kind": kind, "id": identity}},
            "source_id": "wheelhouse.git", "set": {
                key: {"value": {"type": "bool" if isinstance(value, bool) else "text", "value": value}, "ttl_ms": 6000}
                for key, value in facts.items()}, "unset": []}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=os.environ.get("WHEELHOUSE_SOCKET"))
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    if not args.socket:
        parser.error("--socket or WHEELHOUSE_SOCKET is required")
    repo = args.repo.resolve()
    while True:
        branch = subprocess.check_output(["git", "-C", str(repo), "branch", "--show-current"], text=True).strip() or "detached"
        dirty = bool(subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain"], text=True))
        facts = [patch("project", str(repo), {"display.label": repo.name, "flotilla.project": str(repo), "flotilla.project.name": repo.name}),
                 patch("checkout", str(repo), {"display.label": branch, "flotilla.project": str(repo), "vcs.repo": str(repo),
                       "flotilla.checkout": str(repo), "status.state": "active" if dirty else "idle",
                       "status.attention": dirty, "summary.text": "Uncommitted changes" if dirty else "Clean working tree",
                       "action.primary.recipe": f"cd {shlex.quote(str(repo))} && exec /bin/sh", "action.primary.key": "open"})]
        try:
            for fact in facts:
                publish(args.socket, fact)
        except (OSError, http.client.HTTPException) as error:
            if args.once:
                raise
            print(f"Waiting for Wheelhouse: {error}", flush=True)
        if args.once:
            break
        time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
