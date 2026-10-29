#!/usr/bin/env python3
"""Tests for ci/run-ui-diagnostics.py and ci/ui-diagnostics.toml.

Standard library only, and no display or built app: the runner is pointed at a
fake ./build/wheelhouse, and fake timeout and xvfb-run, that log their argv.
    python3 ci/test_run_ui_diagnostics.py
"""

import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import textwrap
import tomllib
import unittest

CI = pathlib.Path(__file__).resolve().parent
RUNNER = CI / "run-ui-diagnostics.py"
CHECKED_IN = CI / "ui-diagnostics.toml"
# The Windows job's set; tools/run-windows-diagnostics.ps1 takes it from --names.
WINDOWS = [
    "shared_ui", "sidebar", "scroll_region", "preview", "tooltip", "panel", "jackstay_input",
    "terminal_link", "terminal_clipboard", "terminal_selection", "terminal_glyph",
]
UISHELL_MAIN = CI.parent / "native" / "src" / "uishell" / "uishell_main.c"

# Logs one JSON line per call, then fails if FAKE_FAIL names its flag.
FAKE_APP = """\
#!{python}
import json, os, sys
args = sys.argv[1:]
task_dirs = [a.split(":", 1)[1].rsplit("/", 1)[0] for a in args if a.startswith("--user:")]
with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(json.dumps({{
        "tool": "wheelhouse",
        "argv": args,
        "generic": os.environ.get("WHEELHOUSE_INPUT_TEST_GENERIC"),
        "task_dir_exists": [os.path.isdir(d) for d in task_dirs],
    }}) + "\\n")
fail = os.environ.get("FAKE_FAIL")
sys.exit(int(os.environ.get("FAKE_STATUS", "3")) if fail and f"--{{fail}}_diagnostics" in args else 0)
"""

# timeout <duration> cmd... and xvfb-run -a cmd...: log, then run cmd.
FAKE_WRAPPER = """\
#!{python}
import json, os, subprocess, sys
with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(json.dumps({{"tool": "{name}", "argv": sys.argv[1:]}}) + "\\n")
sys.exit(subprocess.run(sys.argv[1 + {skip}:]).returncode)
"""


def write_executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


@unittest.skipIf(os.name == "nt", "the fake app and wrappers are POSIX scripts")
class Runner(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = pathlib.Path(self.dir.name)
        self.log = self.root / "log.jsonl"
        self.list = self.root / "list.toml"
        python = sys.executable
        write_executable(self.root / "build" / "wheelhouse", FAKE_APP.format(python=python))
        write_executable(self.root / "bin" / "timeout", FAKE_WRAPPER.format(python=python, name="timeout", skip=1))
        write_executable(self.root / "bin" / "xvfb-run", FAKE_WRAPPER.format(python=python, name="xvfb-run", skip=1))

    def run_list(self, toml, *args, fail=None, status=None):
        self.list.write_text("format = 1\n" + textwrap.dedent(toml))
        env = dict(os.environ, FAKE_LOG=str(self.log), PATH=f"{self.root / 'bin'}{os.pathsep}{os.environ['PATH']}")
        env.pop("WHEELHOUSE_INPUT_TEST_GENERIC", None)
        # Keeps the runner's temporary directory inside this test's own.
        (self.root / "tmp").mkdir(exist_ok=True)
        env["TMPDIR"] = str(self.root / "tmp")
        if fail:
            env["FAKE_FAIL"] = fail
        if status is not None:
            env["FAKE_STATUS"] = str(status)
        return subprocess.run(
            [sys.executable, str(RUNNER), "--list", str(self.list), *args],
            cwd=self.root, env=env, capture_output=True, text=True,
        )

    def calls(self, tool=None):
        if not self.log.exists():
            return []
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        return [call for call in calls if tool is None or call["tool"] == tool]

    def flags(self):
        return [next(a for a in call["argv"] if a.endswith("_diagnostics")) for call in self.calls("wheelhouse")]

    def test_runs_the_os_entries_in_file_order(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            os = ["linux", "macos"]
            [[diagnostic]]
            flag = "two"
            prefix = "two"
            os = ["macos"]
            [[diagnostic]]
            flag = "three"
            prefix = ""
            os = ["linux"]
            [[diagnostic]]
            flag = "four"
            os = ["windows"]
            [[diagnostic]]
            flag = "five"
            prefix = "five"
            os = ["macos", "linux"]
        """
        result = self.run_list(toml, "--os", "linux")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.flags(), ["--one_diagnostics", "--three_diagnostics", "--five_diagnostics"])
        self.log.unlink()
        result = self.run_list(toml, "--os", "macos")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.flags(), ["--one_diagnostics", "--two_diagnostics", "--five_diagnostics"])
        result = self.run_list(toml, "--os", "windows", "--names")
        self.assertEqual(result.stdout, "four\n")

    def test_stops_at_the_first_failure(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            os = ["macos"]
            [[diagnostic]]
            flag = "two"
            prefix = "two"
            os = ["macos"]
            [[diagnostic]]
            flag = "three"
            prefix = "three"
            os = ["macos"]
        """
        result = self.run_list(toml, "--os", "macos", fail="two", status=5)
        self.assertEqual(result.returncode, 5)
        self.assertIn("failed (exit 5)", result.stderr)
        self.assertIn("--two_diagnostics", result.stderr)
        self.assertEqual(self.flags(), ["--one_diagnostics", "--two_diagnostics"])

    def test_a_failing_command_stops_the_run(self):
        toml = """
            [[diagnostic]]
            command = "false"
            os = ["macos"]
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            os = ["macos"]
        """
        result = self.run_list(toml, "--os", "macos")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_linux_wraps_the_app_and_macos_does_not(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            os = ["linux", "macos"]
            [[diagnostic]]
            command = 'build/wheelhouse --command_diagnostics'
            os = ["linux"]
        """
        result = self.run_list(toml, "--os", "linux")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call["tool"] for call in self.calls()], ["timeout", "xvfb-run", "wheelhouse", "wheelhouse"])
        self.assertEqual(self.calls("timeout")[0]["argv"][:4], ["120s", "xvfb-run", "-a", "./build/wheelhouse"])
        self.assertEqual(self.calls("xvfb-run")[0]["argv"][:2], ["-a", "./build/wheelhouse"])
        self.log.unlink()
        result = self.run_list(toml, "--os", "macos")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call["tool"] for call in self.calls()], ["wheelhouse"])

    def test_env_applies_to_its_entry_only(self):
        toml = """
            [[diagnostic]]
            flag = "plain"
            prefix = "plain"
            os = ["linux"]
            [[diagnostic]]
            flag = "generic"
            prefix = "generic"
            env = { WHEELHOUSE_INPUT_TEST_GENERIC = "1" }
            os = ["linux"]
        """
        result = self.run_list(toml, "--os", "linux")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call["generic"] for call in self.calls("wheelhouse")], [None, "1"])
        result = self.run_list(toml, "--os", "linux", "--dry-run")
        self.assertTrue(result.stdout.splitlines()[1].startswith("WHEELHOUSE_INPUT_TEST_GENERIC=1 timeout 120s "))

    def check_task_dir(self, fail):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            pre_args = ['--dashboard:"$task_dir/dash"']
            args = ['--fixture:"$task_dir"']
            os = ["macos"]
            [[diagnostic]]
            flag = "two"
            prefix = ""
            os = ["macos"]
        """
        result = self.run_list(toml, "--os", "macos", fail=fail)
        self.assertEqual(result.returncode != 0, fail is not None)
        calls = self.calls("wheelhouse")
        self.assertEqual(len(calls), 1 if fail else 2)
        argv = calls[0]["argv"]
        task_dir = argv[0].split(":", 1)[1].rsplit("/", 1)[0]
        self.assertEqual(argv, [
            f"--user:{task_dir}/one-user", f"--project:{task_dir}/one-project", f"--dashboard:{task_dir}/dash",
            "--one_diagnostics", f"--fixture:{task_dir}",
        ])
        self.assertTrue(all(call["task_dir_exists"] == [True] for call in calls))
        if not fail:
            self.assertEqual(calls[1]["argv"][:2], [f"--user:{task_dir}/user", f"--project:{task_dir}/project"])
        self.assertTrue(task_dir.startswith(str(self.root / "tmp")))
        self.assertFalse(os.path.exists(task_dir))
        self.assertEqual(list((self.root / "tmp").iterdir()), [])

    def test_task_dir_exists_during_the_run_and_is_removed(self):
        self.check_task_dir(fail=None)

    def test_task_dir_is_removed_after_a_failure(self):
        self.check_task_dir(fail="one")

    def test_dry_run_prints_without_running(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            args = ['--fixture:"$task_dir"']
            os = ["linux"]
            [[diagnostic]]
            command = 'python3 check.py "$task_dir/x.ppm"'
            os = ["linux"]
        """
        result = self.run_list(toml, "--os", "linux", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, (
            'timeout 120s xvfb-run -a ./build/wheelhouse --user:"$task_dir/one-user" '
            '--project:"$task_dir/one-project" --one_diagnostics --fixture:"$task_dir"\n'
            'python3 check.py "$task_dir/x.ppm"\n'
        ))
        self.assertEqual(self.calls(), [])

    def assert_rejected(self, result, message):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertEqual(self.calls(), [])

    def test_unknown_os_argument_fails(self):
        self.assert_rejected(self.run_list("", "--os", "plan9"), "invalid choice: 'plan9'")

    def test_unknown_os_in_the_file_fails(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            os = ["linux", "freebsd"]
        """
        self.assert_rejected(self.run_list(toml, "--os", "linux"), "diagnostic 1 (one): unknown OS 'freebsd'")

    def test_entry_without_flag_or_command_fails(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            os = ["linux"]
            [[diagnostic]]
            prefix = "two"
            os = ["linux"]
        """
        self.assert_rejected(self.run_list(toml, "--os", "linux"), "diagnostic 2: has no flag and no command")

    def test_display_entry_without_prefix_fails(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            os = ["linux"]
        """
        self.assert_rejected(self.run_list(toml, "--os", "linux"), "needs a prefix")

    def test_unknown_key_fails(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            prefix = "one"
            arg = ["--x"]
            os = ["linux"]
        """
        self.assert_rejected(self.run_list(toml, "--os", "linux"), "unknown key arg")

    def test_running_windows_points_at_the_ps1(self):
        toml = """
            [[diagnostic]]
            flag = "one"
            os = ["windows"]
        """
        self.assert_rejected(self.run_list(toml, "--os", "windows"), "run_tests.bat")


class CheckedInList(unittest.TestCase):
    def test_every_flag_is_one_the_app_accepts(self):
        with open(CHECKED_IN, "rb") as handle:
            flags = {entry["flag"] for entry in tomllib.load(handle)["diagnostic"] if "flag" in entry}
        self.assertTrue(flags)
        source = UISHELL_MAIN.read_text(encoding="utf-8")
        for flag in sorted(flags):
            self.assertIn(f'str8_lit("{flag}_diagnostics")', source, f"--{flag}_diagnostics is not in {UISHELL_MAIN.name}")

    def test_loads_for_every_os_and_keeps_the_windows_set(self):
        for os_name in ("linux", "macos"):
            result = subprocess.run(
                [sys.executable, str(RUNNER), "--os", os_name, "--dry-run"], capture_output=True, text=True
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout)
        result = subprocess.run(
            [sys.executable, str(RUNNER), "--os", "windows", "--names"], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(result.stdout.split()), sorted(WINDOWS))


if __name__ == "__main__":
    unittest.main()
