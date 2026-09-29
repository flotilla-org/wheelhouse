#!/usr/bin/env python3
"""Compile and run the production config/WM menu policy without a GUI."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
source = ROOT / "src/config/tests/native_menu.c"
binary = BUILD / ("native_menu_policy.exe" if os.name == "nt" else "native_menu_policy")
if os.name == "nt":
    command = ["cl", "/nologo", "/Z7", "/Od", "/DBUILD_DEBUG=1", "/Zc:preprocessor",
               "/I" + str(ROOT / "src"), str(source), "/Fe:" + str(binary)]
else:
    command = ["clang", "-g", "-O0", "-D_GNU_SOURCE", "-I" + str(ROOT / "src"),
               "-Wno-initializer-overrides", "-Wno-unused-value",
               "-Wno-incompatible-pointer-types-discards-qualifiers"]
    if sys.platform == "darwin":
        command += ["-x", "objective-c"]
    command += [str(source), "-lpthread", "-lm", "-o", str(binary)]
    command += (["-framework", "Cocoa", "-framework", "Security"]
                if sys.platform == "darwin" else ["-ldl"])
subprocess.run(command, cwd=BUILD, check=True)
subprocess.run([str(binary)], cwd=ROOT, check=True)
