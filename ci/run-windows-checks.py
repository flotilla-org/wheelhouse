#!/usr/bin/env python3
"""Run the Windows CI checks in ci/windows-checks.toml.

    native\\check_windows                          # every check (sets up MSVC first)
    python ci/run-windows-checks.py --group unit  # the daily-driver job's checks
    python ci/run-windows-checks.py --skip build  # the rest, against an existing build
    python ci/run-windows-checks.py --list        # print the checks without running them

Runs each selected check in file order from the repository root and keeps
going after a failure. A check whose `needs` did not pass is skipped with the
reason. Prints a summary and
exits 1 if any check failed. build.yml's Windows jobs run the same list.
Standard library only (3.11+, for tomllib).
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_LIST = ROOT / "ci" / "windows-checks.toml"
FORMAT = 1
GROUPS = ("unit", "native")
NAME = re.compile(r"[a-z][a-z0-9-]*")
ENV_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
KEYS = {"name", "group", "run", "env", "needs"}
PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"


class ListError(Exception):
    pass


def _strings(value, where, key):
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ListError(f"{where}: {key} must be a list of strings")
    return value


def load(path):
    """Return the validated checks in `path`, in file order."""
    try:
        with open(path, "rb") as handle:
            data = tomllib.load(handle)
    except FileNotFoundError:
        raise ListError(f"{path}: checks list not found") from None
    except tomllib.TOMLDecodeError as error:
        raise ListError(f"{path}: not valid TOML: {error}") from None
    if data.get("format") != FORMAT:
        raise ListError(f"{path}: expected format = {FORMAT}, found {data.get('format')!r}")
    unknown = sorted(data.keys() - {"format", "check"})
    if unknown:
        raise ListError(f"{path}: unknown key {', '.join(unknown)}")
    checks = data.get("check", [])
    if not checks:
        raise ListError(f"{path}: no [[check]] entries")

    seen = set()
    for number, check in enumerate(checks, 1):
        name = check.get("name")
        where = f"{path}: check {number}" + (f" ({name})" if isinstance(name, str) else "")
        if not isinstance(name, str) or not NAME.fullmatch(name):
            raise ListError(f"{where}: name must be lower-case letters, digits and dashes")
        if name in seen:
            raise ListError(f"{where}: another check has this name")
        unknown = sorted(check.keys() - KEYS)
        if unknown:
            raise ListError(f"{where}: unknown key {', '.join(unknown)}")
        if check.get("group") not in GROUPS:
            raise ListError(f"{where}: group must be one of {', '.join(GROUPS)}")
        if not _strings(check.get("run"), where, "run"):
            raise ListError(f"{where}: run is empty")
        env = check.get("env", {})
        if not isinstance(env, dict) or not all(
            ENV_NAME.fullmatch(key) and isinstance(value, str) for key, value in env.items()
        ):
            raise ListError(f"{where}: env must map variable names to strings")
        for need in _strings(check.get("needs", []), where, "needs"):
            if need not in seen:
                raise ListError(f"{where}: needs {need!r}, which is not an earlier check")
        seen.add(name)
    return checks


def display(check):
    env = [f"{key}={value}" for key, value in check.get("env", {}).items()]
    return " ".join(env + check["run"])


_host = None


def rust_host():
    global _host
    if _host is None:
        output = subprocess.run(["rustc", "-vV"], capture_output=True, text=True, check=True).stdout
        _host = next(line.split(":", 1)[1].strip() for line in output.splitlines() if line.startswith("host:"))
    return _host


def argv(check):
    """The check's command line, ready for subprocess, with {host} expanded."""
    words = [word.replace("{host}", rust_host()) if "{host}" in word else word for word in check["run"]]
    first, rest = words[0], words[1:]
    if first == "python":
        return [sys.executable, *rest]
    if "/" in first:
        first = str(ROOT.joinpath(*first.split("/")))
    if first.endswith(".ps1"):
        shell = shutil.which("pwsh") or shutil.which("powershell") or "powershell"
        return [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", first, *rest]
    return [first, *rest]


class Reporter:
    """Groups each check's output in GitHub Actions logs; plain headings elsewhere."""

    def __init__(self):
        self.actions = os.environ.get("GITHUB_ACTIONS") == "true"

    def start(self, check):
        heading = f"{check['name']}: {display(check)}"
        print(f"::group::{heading}" if self.actions else f"\n=== {heading}", flush=True)

    def end(self, check, status, detail):
        if self.actions:
            print("::endgroup::", flush=True)
            if status == FAIL:
                print(f"::error title=Windows check {check['name']} failed::{detail}", flush=True)
        else:
            print(f"=== {check['name']}: {status} ({detail})", flush=True)


def run(checks, skipped):
    """Run `checks` in order; `skipped` are names left out, which count as passed."""
    reporter = Reporter()
    results = []
    status_of = {name: PASS for name in skipped}
    for check in checks:
        name = check["name"]
        unmet = [need for need in check.get("needs", []) if status_of.get(need, PASS) != PASS]
        if unmet:
            reason = f"{', '.join(unmet)} did not pass"
            print(f"\n=== {name}: SKIP ({reason})", flush=True)
            status_of[name] = SKIP
            results.append((name, SKIP, reason))
            continue
        reporter.start(check)
        env = dict(os.environ)
        env.update(check.get("env", {}))
        began = time.monotonic()
        try:
            code = subprocess.run(argv(check), cwd=ROOT, env=env).returncode
            detail = f"exit {code}"
        except OSError as error:
            code, detail = 1, str(error)
        except subprocess.CalledProcessError as error:
            code, detail = 1, f"could not read the rustc host: {error}"
        status = PASS if code == 0 else FAIL
        detail = f"{time.monotonic() - began:.0f}s" if status == PASS else f"{detail}, {time.monotonic() - began:.0f}s"
        reporter.end(check, status, detail)
        status_of[name] = status
        results.append((name, status, detail))

    width = max((len(name) for name, _, _ in results), default=0)
    print("\nWindows checks:", flush=True)
    for name, status, detail in results:
        print(f"  {status}  {name.ljust(width)}  {detail}", flush=True)
    counts = {status: sum(1 for _, s, _ in results if s == status) for status in (PASS, FAIL, SKIP)}
    print(f"{counts[PASS]} passed, {counts[FAIL]} failed, {counts[SKIP]} skipped", flush=True)
    return 1 if counts[FAIL] else 0


def main(argv_=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--group", action="append", choices=GROUPS, help="run only this group (repeatable)")
    parser.add_argument("--only", action="append", default=[], metavar="NAME", help="run only these checks (repeatable, or comma-separated)")
    parser.add_argument("--skip", action="append", default=[], metavar="NAME", help="leave these checks out (repeatable, or comma-separated)")
    parser.add_argument("--list", action="store_true", help="print the selected checks instead of running them")
    parser.add_argument("--checks", type=pathlib.Path, default=DEFAULT_LIST, help="list file (default: ci/windows-checks.toml)")
    args = parser.parse_args(argv_)
    try:
        checks = load(args.checks)
    except ListError as error:
        print(f"run-windows-checks: {error}", file=sys.stderr)
        return 1

    names = {check["name"] for check in checks}
    only = {name for value in args.only for name in value.split(",") if name}
    skip = {name for value in args.skip for name in value.split(",") if name}
    for name in sorted((only | skip) - names):
        print(f"run-windows-checks: no check named {name!r}", file=sys.stderr)
        return 1
    groups = set(args.group or GROUPS)
    selected = [
        check for check in checks
        if check["group"] in groups and (not only or check["name"] in only) and check["name"] not in skip
    ]
    if not selected:
        print("run-windows-checks: no checks selected", file=sys.stderr)
        return 1
    if args.list:
        for check in selected:
            print(f"{check['name']} [{check['group']}]: {display(check)}")
        return 0
    return run(selected, names - {check["name"] for check in selected})


if __name__ == "__main__":
    sys.exit(main())
