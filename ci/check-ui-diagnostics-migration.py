#!/usr/bin/env python3
"""Compare ci/ui-diagnostics.toml with the lists it replaced.

    python3 ci/check-ui-diagnostics-migration.py <base-commit>

Takes the "Linux UI diagnostics" and "macOS UI diagnostics" run: blocks from
.github/workflows/build.yml at <base-commit>, drops the task_dir= and trap
lines, and checks they equal `ci/run-ui-diagnostics.py --dry-run` as a
multiset of lines, byte for byte. Checks the PPM validator still follows the
glyph diagnostic, and that `--os windows --names` equals the default
$Diagnostics list in tools/run-windows-diagnostics.ps1 at <base-commit>.
Prints both orders. Only meaningful against a commit from before the workflow
moved to the runner; a one-off migration check, not a standing test.
"""

import collections
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RUNNER = ROOT / "ci" / "run-ui-diagnostics.py"
STEPS = {"linux": "Linux UI diagnostics", "macos": "macOS UI diagnostics"}


def git_show(base, path):
    return subprocess.run(
        ["git", "show", f"{base}:{path}"], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout


def run_block(workflow, step):
    """The lines of `step`'s `run: |` block, dedented, as written."""
    lines = workflow.splitlines()
    start = next(i for i, line in enumerate(lines) if line.strip() == f"- name: {step}")
    step_indent = len(lines[start]) - len(lines[start].lstrip())
    for i in range(start + 1, len(lines)):
        line = lines[i]
        indent = len(line) - len(line.lstrip())
        if line.strip() and indent <= step_indent:
            raise SystemExit(f"{step}: no run: | block")
        if line.strip() == "run: |":
            body = []
            for line in lines[i + 1:]:
                if line.strip() and len(line) - len(line.lstrip()) <= indent:
                    break
                body.append(line)
            body_indent = min(len(line) - len(line.lstrip()) for line in body if line.strip())
            return [line[body_indent:] for line in body if line.strip()]
    raise SystemExit(f"{step}: no run: | block")


def runner(*args):
    return subprocess.run(
        [sys.executable, str(RUNNER), *args], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.splitlines()


def show_order(label, before, after):
    print(f"  {label} order (before -> after):")
    width = max(len(str(len(before))), 2)
    for line in after:
        old = before.index(line) + 1 if line in before else None
        new = after.index(line) + 1
        mark = "" if old == new else f"   <- was {old}"
        print(f"    {new:>{width}}  {line}{mark}")


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    base = argv[1]
    workflow = git_show(base, ".github/workflows/build.yml")
    ok = True

    for os_name, step in STEPS.items():
        before = [
            line for line in run_block(workflow, step)
            if not line.startswith("task_dir=") and not line.startswith("trap ")
        ]
        after = runner("--os", os_name, "--dry-run")
        same = collections.Counter(before) == collections.Counter(after)
        ok &= same
        print(f"{os_name}: {len(before)} lines in build.yml at {base}, {len(after)} from the runner: "
              + ("identical as a multiset" if same else "DIFFERENT"))
        for line in sorted((collections.Counter(before) - collections.Counter(after)).elements()):
            print(f"  only in build.yml: {line}")
        for line in sorted((collections.Counter(after) - collections.Counter(before)).elements()):
            print(f"  only from runner:  {line}")
        if before == after:
            print("  same order")
        else:
            show_order(os_name, before, after)

    after = runner("--os", "macos", "--dry-run")
    glyph = [i for i, line in enumerate(after) if "--terminal_glyph_diagnostics" in line]
    check = [i for i, line in enumerate(after) if "validate-terminal-fixture-ppm.py" in line]
    ppm_ok = bool(glyph) and bool(check) and min(check) > max(glyph)
    ok &= ppm_ok
    print("macos: PPM validator " + ("runs after terminal_glyph" if ppm_ok else "does NOT run after terminal_glyph"))

    ps1 = git_show(base, "tools/run-windows-diagnostics.ps1")
    match = re.search(r"\[string\[\]\]\$Diagnostics\s*=\s*@\(([^)]*)\)", ps1)
    if not match:
        print(f"windows: no default $Diagnostics list in the ps1 at {base}")
        return 1
    before = re.findall(r'"([a-z_]+)"', match.group(1))
    after = runner("--os", "windows", "--names")
    same = sorted(before) == sorted(after) and len(set(after)) == len(after)
    ok &= same
    print(f"windows: {len(before)} names in the ps1 at {base}, {len(after)} from the runner: "
          + ("identical as a set" if same else "DIFFERENT"))
    if not same:
        print(f"  only in ps1:    {sorted(set(before) - set(after))}")
        print(f"  only in runner: {sorted(set(after) - set(before))}")
    if before == after:
        print("  same order")
    else:
        show_order("windows", before, after)

    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
