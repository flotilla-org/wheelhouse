#!/usr/bin/env python3
"""Tests for ci/pins.py. Standard library only: python3 ci/test_pins.py"""

import pathlib
import subprocess
import sys
import tempfile
import tomllib
import unittest

CI = pathlib.Path(__file__).resolve().parent
PINS = CI / "pins.py"
CHECKED_IN = CI / "pins.toml"

CLEAT = "c88692aa62c10fbeb10ecd22f6e637fb600ebd62"
JACKSTAY = "bdf1d39ee2d95fa663820d4eaed6b0bcfe8345ac"


def pins_file(cleat_rev=CLEAT, jackstay_env="JACKSTAY_REV"):
    return f"""format = 1

[cleat]
env = "CLEAT_REV"
repo = "flotilla-org/cleat"
rev = "{cleat_rev}"
track = "manual"

[jackstay]
env = "{jackstay_env}"
repo = "flotilla-org/jackstay"
rev = "{JACKSTAY}"
track = "manual"
"""


def github_env(path):
    return subprocess.run(
        [sys.executable, str(PINS), "github-env", "--file", str(path)], capture_output=True, text=True
    )


class GithubEnv(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.path = pathlib.Path(self.dir.name) / "pins.toml"

    def write(self, text):
        self.path.write_text(text)
        return github_env(self.path)

    def assert_fails(self, result, message):
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn(message, result.stderr)

    def test_checked_in_file_prints_each_pin(self):
        with open(CHECKED_IN, "rb") as handle:
            data = tomllib.load(handle)
        result = github_env(CHECKED_IN)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            f"CLEAT_REV={data['cleat']['rev']}\nJACKSTAY_REV={data['jackstay']['rev']}\n",
        )
        for line in result.stdout.splitlines():
            self.assertRegex(line, r"^[A-Z_]+=[0-9a-f]{40}$")

    def test_default_path_is_the_checked_in_file(self):
        result = subprocess.run([sys.executable, str(PINS), "github-env"], capture_output=True, text=True)
        self.assertEqual(result.stdout, github_env(CHECKED_IN).stdout)

    def test_valid_file(self):
        result = self.write(pins_file())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"CLEAT_REV={CLEAT}\nJACKSTAY_REV={JACKSTAY}\n")

    def test_short_rev_fails(self):
        self.assert_fails(self.write(pins_file(cleat_rev=CLEAT[:39])), "40 lower-case hex")

    def test_long_rev_fails(self):
        self.assert_fails(self.write(pins_file(cleat_rev=CLEAT + "0")), "40 lower-case hex")

    def test_uppercase_rev_fails(self):
        self.assert_fails(self.write(pins_file(cleat_rev=CLEAT.upper())), "40 lower-case hex")

    def test_duplicate_env_fails(self):
        self.assert_fails(self.write(pins_file(jackstay_env="CLEAT_REV")), "CLEAT_REV is already used by [cleat]")

    def test_missing_file_fails(self):
        self.assert_fails(github_env(self.path), "pins file not found")

    def test_unknown_track_fails(self):
        self.assert_fails(self.write(pins_file().replace('"manual"', '"latest"', 1)), "track 'latest'")

    def test_missing_key_fails(self):
        self.assert_fails(self.write(pins_file().replace('repo = "flotilla-org/cleat"\n', "")), "missing repo")


if __name__ == "__main__":
    unittest.main()
