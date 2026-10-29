#!/usr/bin/env python3
"""Read the cross-repo pins in ci/pins.toml.

    python ci/pins.py github-env >> "$GITHUB_ENV"

prints one NAME=rev line per pin, in file order. Standard library only, so it
runs as `python` on Windows and `python3` elsewhere (3.11+, for tomllib).
"""

import argparse
import pathlib
import re
import sys
import tomllib

DEFAULT_PATH = pathlib.Path(__file__).resolve().parent / "pins.toml"
FORMAT = 1
REV = re.compile(r"[0-9a-f]{40}")
ENV_NAME = re.compile(r"[A-Z][A-Z0-9_]*")
REPO = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
TRACK_POLICIES = ("manual",)
KEYS = {"env", "repo", "rev", "track"}


class PinError(Exception):
    pass


def load(path):
    """Return the pins in `path` as (name, entry) pairs, in file order."""
    try:
        with open(path, "rb") as handle:
            data = tomllib.load(handle)
    except FileNotFoundError:
        raise PinError(f"{path}: pins file not found") from None
    except tomllib.TOMLDecodeError as error:
        raise PinError(f"{path}: not valid TOML: {error}") from None

    if data.get("format") != FORMAT:
        raise PinError(f"{path}: expected format = {FORMAT}, found {data.get('format')!r}")
    pins = [(name, entry) for name, entry in data.items() if name != "format"]
    if not pins:
        raise PinError(f"{path}: no pins")

    seen = {}
    for name, entry in pins:
        where = f"{path}: [{name}]"
        if not isinstance(entry, dict):
            raise PinError(f"{where}: expected a table")
        missing = sorted(KEYS - entry.keys())
        if missing:
            raise PinError(f"{where}: missing {', '.join(missing)}")
        unknown = sorted(entry.keys() - KEYS)
        if unknown:
            raise PinError(f"{where}: unknown key {', '.join(unknown)}")
        env, repo, rev, track = entry["env"], entry["repo"], entry["rev"], entry["track"]
        if not isinstance(env, str) or not ENV_NAME.fullmatch(env):
            raise PinError(f"{where}: env {env!r} is not an upper-case variable name")
        if not isinstance(repo, str) or not REPO.fullmatch(repo):
            raise PinError(f"{where}: repo {repo!r} is not owner/name")
        if not isinstance(rev, str) or not REV.fullmatch(rev):
            raise PinError(f"{where}: rev {rev!r} is not 40 lower-case hex characters")
        if track not in TRACK_POLICIES:
            raise PinError(f"{where}: track {track!r} is not one of {', '.join(TRACK_POLICIES)}")
        if env in seen:
            raise PinError(f"{where}: env {env} is already used by [{seen[env]}]")
        seen[env] = name
    return pins


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["github-env"])
    parser.add_argument("--file", type=pathlib.Path, default=DEFAULT_PATH, help="pins file (default: ci/pins.toml)")
    args = parser.parse_args(argv)
    try:
        pins = load(args.file)
    except PinError as error:
        print(f"pins.py: {error}", file=sys.stderr)
        return 1
    # $GITHUB_ENV is read line by line; a Windows \r would end up in the value.
    sys.stdout.reconfigure(newline="\n")
    for _, entry in pins:
        print(f"{entry['env']}={entry['rev']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
