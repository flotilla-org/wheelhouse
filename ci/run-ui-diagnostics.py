#!/usr/bin/env python3
"""Run the UI diagnostics in ci/ui-diagnostics.toml for one OS.

    python3 ci/run-ui-diagnostics.py --os linux            # run them
    python3 ci/run-ui-diagnostics.py --os macos --dry-run  # print the commands
    python ci/run-ui-diagnostics.py --os windows --names   # print the flag names (commands have none)

Run from the repository root. Makes one temporary directory for the whole run,
runs each entry for the OS in file order, stops at the first failure with its
exit status, and removes the directory at the end. --dry-run writes the
directory as the literal $task_dir. Windows runs its list through
tools/run-windows-diagnostics.ps1, which takes the names from --names.
Standard library only (3.11+, for tomllib).
"""

import argparse
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import tomllib

DEFAULT_LIST = pathlib.Path(__file__).resolve().parent / "ui-diagnostics.toml"
FORMAT = 1
OSES = ("linux", "macos", "windows")
BINARY = "./build/wheelhouse"
LINUX_WRAPPER = ["timeout", "120s", "xvfb-run", "-a"]
TASK_DIR = "$task_dir"
FLAG = re.compile(r"[a-z][a-z0-9_]*")
ENV_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
FLAG_KEYS = {"flag", "prefix", "pre_args", "args", "env", "os"}
COMMAND_KEYS = {"command", "env", "os"}


class ListError(Exception):
    pass


def _strings(value, where, key):
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ListError(f"{where}: {key} must be a list of strings")
    return value


def load(path):
    """Return the validated entries in `path`, in file order."""
    try:
        with open(path, "rb") as handle:
            data = tomllib.load(handle)
    except FileNotFoundError:
        raise ListError(f"{path}: diagnostics list not found") from None
    except tomllib.TOMLDecodeError as error:
        raise ListError(f"{path}: not valid TOML: {error}") from None
    if data.get("format") != FORMAT:
        raise ListError(f"{path}: expected format = {FORMAT}, found {data.get('format')!r}")
    unknown = sorted(data.keys() - {"format", "diagnostic"})
    if unknown:
        raise ListError(f"{path}: unknown key {', '.join(unknown)}")
    entries = data.get("diagnostic", [])
    if not entries:
        raise ListError(f"{path}: no [[diagnostic]] entries")

    for number, entry in enumerate(entries, 1):
        name = entry.get("flag") or entry.get("command")
        where = f"{path}: diagnostic {number}" + (f" ({name})" if isinstance(name, str) else "")
        oses = _strings(entry.get("os"), where, "os")
        if not oses:
            raise ListError(f"{where}: os lists no OS")
        for os_name in oses:
            if os_name not in OSES:
                raise ListError(f"{where}: unknown OS {os_name!r}; expected one of {', '.join(OSES)}")
        env = entry.get("env", {})
        if not isinstance(env, dict) or not all(
            ENV_NAME.fullmatch(key) and isinstance(value, str) for key, value in env.items()
        ):
            raise ListError(f"{where}: env must map variable names to strings")

        if "flag" in entry and "command" in entry:
            raise ListError(f"{where}: has both a flag and a command")
        if "flag" in entry:
            keys = FLAG_KEYS
            if not isinstance(entry["flag"], str) or not FLAG.fullmatch(entry["flag"]):
                raise ListError(f"{where}: flag must be a lower-case name, as in --<flag>_diagnostics")
            if entry["flag"].endswith("_diagnostics"):
                raise ListError(f"{where}: write the flag without its _diagnostics suffix")
            if set(oses) - {"windows"} and not isinstance(entry.get("prefix"), str):
                raise ListError(f"{where}: needs a prefix for its --user/--project directories")
            _strings(entry.get("pre_args", []), where, "pre_args")
            _strings(entry.get("args", []), where, "args")
        elif "command" in entry:
            keys = COMMAND_KEYS
            if not isinstance(entry["command"], str) or not entry["command"].strip():
                raise ListError(f"{where}: command must be a non-empty string")
            if "windows" in oses:
                raise ListError(f"{where}: Windows runs flags only, not commands")
        else:
            raise ListError(f"{where}: has no flag and no command")
        unknown = sorted(entry.keys() - keys)
        if unknown:
            raise ListError(f"{where}: unknown key {', '.join(unknown)}")
    return entries


def for_os(entries, os_name):
    return [entry for entry in entries if os_name in entry["os"]]


def command_line(entry, os_name):
    """The entry's command line as shell text, with $task_dir unexpanded."""
    if "command" in entry:
        return entry["command"]
    prefix = entry["prefix"]
    stem = f"{prefix}-" if prefix else ""
    words = [
        BINARY,
        f'--user:"{TASK_DIR}/{stem}user"',
        f'--project:"{TASK_DIR}/{stem}project"',
        *entry.get("pre_args", []),
        f"--{entry['flag']}_diagnostics",
        *entry.get("args", []),
    ]
    if os_name == "linux":
        words = LINUX_WRAPPER + words
    return " ".join(words)


def display(entry, os_name):
    env = [f"{key}={value}" for key, value in entry.get("env", {}).items()]
    return " ".join(env + [command_line(entry, os_name)])


def run(entries, os_name):
    task_dir = tempfile.mkdtemp(prefix="wheelhouse-ui-diagnostics-")
    try:
        for entry in entries:
            line = display(entry, os_name)
            print(f"+ {line}", flush=True)
            # mkdtemp paths have no quotes or spaces, so plain substitution keeps the words.
            argv = shlex.split(command_line(entry, os_name).replace(TASK_DIR, task_dir))
            env = dict(os.environ)
            env.update((key, value.replace(TASK_DIR, task_dir)) for key, value in entry.get("env", {}).items())
            status = subprocess.run(argv, env=env).returncode
            if status != 0:
                print(f"run-ui-diagnostics: failed (exit {status}): {line}", file=sys.stderr, flush=True)
                return status if status > 0 else 1
    finally:
        shutil.rmtree(task_dir, ignore_errors=True)
    print(f"run-ui-diagnostics: {len(entries)} {os_name} diagnostics passed", flush=True)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--os", required=True, choices=OSES, dest="os_name")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="print the commands instead of running them")
    mode.add_argument("--names", action="store_true", help="print the flag names, one per line")
    parser.add_argument("--list", type=pathlib.Path, default=DEFAULT_LIST, help="list file (default: ci/ui-diagnostics.toml)")
    args = parser.parse_args(argv)
    try:
        entries = for_os(load(args.list), args.os_name)
    except ListError as error:
        print(f"run-ui-diagnostics: {error}", file=sys.stderr)
        return 1

    if args.names:
        for entry in entries:
            if "flag" in entry:
                print(entry["flag"])
        return 0
    if args.os_name == "windows":
        print("run-ui-diagnostics: Windows runs its list with native\\run_tests.bat "
              "(tools/run-windows-diagnostics.ps1); use --names", file=sys.stderr)
        return 1
    if args.dry_run:
        for entry in entries:
            print(display(entry, args.os_name))
        return 0
    return run(entries, args.os_name)


if __name__ == "__main__":
    sys.exit(main())
