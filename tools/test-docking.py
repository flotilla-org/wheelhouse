#!/usr/bin/env python3
"""Compile and run the production docking validity, restore and drag query, and
the arrangement module (native/src/shell/shell_arrangement.c), without a GUI.

Their rules are the core's (#516), so the tests link the core library: pass
its directory, as native/build.sh does, or it is built here.
"""
import argparse
import os
import shlex
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("core_lib", type=Path, nargs="?", help="the directory holding the built wheelhouse_ffi library")
parser.add_argument("--core-include", type=Path, default=ROOT / "crates/wheelhouse-ffi/include")
args = parser.parse_args()
core_lib = args.core_lib
if core_lib is None:
    host = subprocess.check_output(["rustc", "-vV"], text=True).split("host: ")[1].split()[0]
    subprocess.run(["cargo", "build", "-p", "wheelhouse-ffi", "--locked", "--target", host], cwd=ROOT, check=True)
    core_lib = ROOT / "target" / host / "debug"
core_lib = core_lib.resolve()
core_include = args.core_include.resolve()
env = dict(os.environ)
if os.name == "nt":
    env["PATH"] = str(core_lib) + os.pathsep + env.get("PATH", "")
for name, test in [("docking_validity", "docking"), ("arrangement", "arrangement")]:
    source = ROOT / "native/src/shell/tests" / (test + ".c")
    binary = BUILD / (name + ".exe" if os.name == "nt" else name)
    if os.name == "nt":
        command = ["cl", "/nologo", "/Z7", "/Od", "/DBUILD_DEBUG=1", "/Zc:preprocessor",
                   "/I" + str(ROOT / "native/src"), "/I" + str(core_include), str(source), "/Fe:" + str(binary),
                   "/link", str(core_lib / "wheelhouse_ffi.dll.lib")]
    else:
        command = shlex.split(os.environ.get("CC", "clang")) + ["-g", "-O0", "-D_GNU_SOURCE", "-I" + str(ROOT / "native/src"),
                   "-I" + str(core_include),
                   "-Wno-initializer-overrides", "-Wno-unused-value",
                   "-Wno-incompatible-pointer-types-discards-qualifiers"]
        if sys.platform == "darwin":
            command += ["-x", "objective-c"]
        command += [str(source), "-x", "none", "-L" + str(core_lib), "-lwheelhouse_ffi", "-Wl,-rpath," + str(core_lib),
                    "-lpthread", "-lm", "-o", str(binary)]
        command += (["-framework", "Cocoa", "-framework", "Security"]
                    if sys.platform == "darwin" else ["-ldl"])
    subprocess.run(command, cwd=BUILD, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, env=env)
