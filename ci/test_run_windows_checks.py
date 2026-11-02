#!/usr/bin/env python3
"""Tests for ci/run-windows-checks.py, ci/windows-checks.toml and the
build.yml steps that run them.

Standard library only; runs on any OS. The runner is pointed at lists of
`python -c` checks, so nothing is built.
    python ci/test_run_windows_checks.py
"""

import importlib.util
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

CI = pathlib.Path(__file__).resolve().parent
ROOT = CI.parent
RUNNER = CI / "run-windows-checks.py"
CHECKED_IN = CI / "windows-checks.toml"
BUILD_YML = ROOT / ".github" / "workflows" / "build.yml"


def load_runner():
    spec = importlib.util.spec_from_file_location("run_windows_checks", RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check(name, code="pass", group="unit", **extra):
    """A [[check]] entry that runs `python -c <code>`."""
    lines = [
        "[[check]]",
        f'name = "{name}"',
        f'group = "{group}"',
        f"run = ['python', '-c', {code!r}]",
    ]
    lines += [f"{key} = {value}" for key, value in extra.items()]
    return "\n".join(lines) + "\n"


FAIL = "import sys; sys.exit(4)"


class Runner(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = pathlib.Path(self.dir.name)
        self.list = self.root / "checks.toml"
        self.log = self.root / "log"

    def logs(self, name):
        """Code that appends `name` to the log, then passes."""
        return f"open({str(self.log)!r}, 'a').write({name + chr(10)!r})"

    def ran(self):
        return self.log.read_text().split() if self.log.exists() else []

    def run_list(self, *entries, args=(), env=None):
        self.list.write_text("format = 1\n" + "".join(entries))
        environment = dict(os.environ)
        environment.pop("GITHUB_ACTIONS", None)
        environment.update(env or {})
        return subprocess.run(
            [sys.executable, str(RUNNER), "--checks", str(self.list), *args],
            capture_output=True, text=True, env=environment,
        )

    def summary(self, result):
        lines = result.stdout.split("\nWindows checks:\n", 1)[1].splitlines()
        return [line.split()[:2] for line in lines[:-1]], lines[-1]

    def test_keeps_going_after_a_failure_and_exits_non_zero(self):
        result = self.run_list(
            check("one", self.logs("one")),
            check("two", FAIL),
            check("three", self.logs("three")),
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["one", "three"])
        rows, totals = self.summary(result)
        self.assertEqual(rows, [["PASS", "one"], ["FAIL", "two"], ["PASS", "three"]])
        self.assertEqual(totals, "2 passed, 1 failed, 0 skipped")
        self.assertIn("exit 4", result.stdout)

    def test_all_passing_exits_zero(self):
        result = self.run_list(check("one"), check("two"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.summary(result)[1], "2 passed, 0 failed, 0 skipped")

    def test_a_check_whose_needs_failed_is_skipped(self):
        result = self.run_list(
            check("build", FAIL),
            check("uses-build", self.logs("uses-build"), needs='["build"]'),
            check("independent", self.logs("independent")),
            check("uses-uses-build", self.logs("uses-uses-build"), needs='["uses-build"]'),
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.ran(), ["independent"])
        rows, _ = self.summary(result)
        self.assertEqual(rows, [["FAIL", "build"], ["SKIP", "uses-build"], ["PASS", "independent"], ["SKIP", "uses-uses-build"]])
        self.assertIn("build did not pass", result.stdout)

    def test_skipped_needs_count_as_passed(self):
        entries = (check("build", FAIL), check("uses-build", self.logs("uses-build"), needs='["build"]'))
        result = self.run_list(*entries, args=["--skip", "build"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["uses-build"])
        self.log.unlink()
        result = self.run_list(*entries, args=["--only", "uses-build"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["uses-build"])

    def test_group_selects_its_checks_in_file_order(self):
        entries = (
            check("a", self.logs("a"), group="native"),
            check("b", self.logs("b"), group="unit"),
            check("c", self.logs("c"), group="native"),
        )
        result = self.run_list(*entries, args=["--group", "native"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["a", "c"])

    def test_env_applies_to_its_check_only(self):
        probe = f"import os; open({str(self.log)!r}, 'a').write(os.environ.get('WINDOWS_CHECK_PROBE', '-') + chr(10))"
        result = self.run_list(
            check("one", probe, env='{ WINDOWS_CHECK_PROBE = "set" }'),
            check("two", probe),
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["set", "-"])

    def test_list_prints_without_running(self):
        result = self.run_list(check("one", self.logs("one")), args=["--list"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith("one [unit]: python -c "))
        self.assertEqual(self.ran(), [])

    def test_github_actions_groups_output_and_annotates_failures(self):
        result = self.run_list(check("one"), check("two", FAIL), env={"GITHUB_ACTIONS": "true"})
        self.assertEqual(result.stdout.count("::group::"), 2)
        self.assertEqual(result.stdout.count("::endgroup::"), 2)
        self.assertIn("::error title=Windows check two failed::exit 4", result.stdout)

    def test_unknown_check_name_is_rejected(self):
        result = self.run_list(check("one"), args=["--skip", "bulid"])
        self.assertEqual(result.returncode, 1)
        self.assertIn("no check named 'bulid'", result.stderr)

    def assert_rejected(self, message, *entries):
        result = self.run_list(*entries)
        self.assertEqual(result.returncode, 1)
        self.assertIn(message, result.stderr)

    def test_bad_lists_are_rejected(self):
        self.assert_rejected("another check has this name", check("one"), check("one"))
        self.assert_rejected("not an earlier check", check("one", needs='["two"]'), check("two"))
        self.assert_rejected("group must be one of", check("one", group="lint"))
        self.assert_rejected("unknown key", check("one", timeout="5"))
        self.assert_rejected("unknown requirement 'admin'", check("one", requires='["admin"]'))
        self.assert_rejected("no [[check]] entries")


class Requirements(unittest.TestCase):
    def test_a_check_needing_symlinks_is_skipped_with_the_reason_when_they_are_unavailable(self):
        runner = load_runner()
        entry = {"name": "links", "requires": ["symlinks"]}
        with mock.patch.object(runner, "can_symlink", return_value=False):
            self.assertIn("Developer Mode", runner.missing_requirement(entry))
        with mock.patch.object(runner, "can_symlink", return_value=True):
            self.assertIsNone(runner.missing_requirement(entry))
        self.assertIsNone(runner.missing_requirement({"name": "plain"}))


class CheckedInList(unittest.TestCase):
    def setUp(self):
        self.checks = load_runner().load(CHECKED_IN)

    def test_every_script_it_names_exists(self):
        for entry in self.checks:
            for word in entry["run"]:
                if re.search(r"\.(py|ps1|bat)$", word):
                    with self.subTest(check=entry["name"]):
                        self.assertTrue((ROOT / word).is_file(), f"{entry['name']}: {word} not found")

    def test_it_lists_this_test(self):
        self.assertIn(["python", "ci/test_run_windows_checks.py"], [entry["run"] for entry in self.checks])

    def test_build_yml_runs_each_group_on_windows(self):
        # CI runs the list itself, so the list and the jobs cannot drift apart.
        workflow = BUILD_YML.read_text(encoding="utf-8")
        steps = re.split(r"\n      - ", workflow)
        for group, command in (
            ("unit", "python ci/run-windows-checks.py --group unit"),
            ("native", r"call native\check_windows.bat --group native"),
        ):
            with self.subTest(group=group):
                matching = [step for step in steps if command in step]
                self.assertEqual(len(matching), 1, f"build.yml has no single step running `{command}`")
                self.assertIn("if: runner.os == 'Windows'", matching[0])

    def test_build_yml_runs_no_listed_script_on_windows_besides(self):
        # A listed script's other build.yml steps must not run on Windows;
        # there it would run twice, and the two could diverge.
        workflow = BUILD_YML.read_text(encoding="utf-8")
        scripts = {word for entry in self.checks for word in entry["run"] if re.search(r"\.(py|ps1|bat)$", word)}
        not_windows = ("if: runner.os != 'Windows'", "if: runner.os == 'Linux'", "if: runner.os == 'macOS'")
        for step in re.split(r"\n      - ", workflow):
            if "run-windows-checks.py" in step or "check_windows.bat" in step:
                continue
            for script in scripts:
                if script in step or script.replace("/", "\\") in step:
                    with self.subTest(script=script):
                        self.assertTrue(any(condition in step for condition in not_windows),
                                        f"this build.yml step runs {script} on Windows too:\n{step}")


if __name__ == "__main__":
    unittest.main()
