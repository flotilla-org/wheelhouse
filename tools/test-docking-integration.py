#!/usr/bin/env python3
"""Compile real shell command/drag/restore code with OS and FFI fault boundaries.

Pass the built Andamento and Cleat library directories; no display is required.
"""
import os
import re
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
BUILD.mkdir(exist_ok=True)
andamento = Path(sys.argv[1]).resolve()
cleat = Path(sys.argv[2]).resolve()
# Reuse the production amalgamation's includes, replacing only its application
# entry point. This keeps the command routing and UI collaborators real.
main = (ROOT / 'src/uishell/uishell_main.c').read_text()
prefix = main[:main.index('internal void\nentry_point(CmdLine *cmd_line)')]
prefix = re.sub(r'^#include "uishell/[^"]*diagnostics.c"\n', '', prefix, flags=re.M)
prefix = prefix.replace('#include "shell/shell_inc.c"', '''
#include "andamento.h"
internal Rng2F32 integration_client_rect(WM_Window window);
internal U64 integration_now_ms(void);
internal U32 integration_dispatch(Andamento *core, const AndamentoSnapshot *snapshot, size_t action, char **error);
#define wm_client_rect_from_window integration_client_rect
#define andamento_dispatch integration_dispatch
#define wheelhouse_ingress_now_ms integration_now_ms
#include "shell/shell_inc.c"
#undef wm_client_rect_from_window
#undef andamento_dispatch
#undef wheelhouse_ingress_now_ms
''')
source = BUILD / 'docking_integration.c'
source.write_text(prefix + '\n#include "shell/tests/docking_integration.c"\n')
command = shlex.split(os.environ.get('CC', 'clang')) + [
    '-g', '-O0', '-D_GNU_SOURCE', '-DBUILD_DEBUG=1', '-DNO_ASYNC=1',
    '-DWM_STUB=1', '-DR_BACKEND=0', '-DFP_BACKEND=0', '-ffunction-sections', '-fdata-sections',
    '-Wno-initializer-overrides', '-Wno-unused-value',
    '-Wno-incompatible-pointer-types-discards-qualifiers',
    '-I'+str(ROOT/'src'), '-I'+str(ROOT.parent/'andamento/crates/andamento-ffi/include'),
    '-I'+str(ROOT.parent/'cleat/crates/cleat/include'), str(source),
    '-L'+str(andamento), '-landamento_ffi', '-lwheelhouse_ingress', '-Wl,-rpath,'+str(andamento),
    '-L'+str(cleat), '-lcleat', '-Wl,-rpath,'+str(cleat), '-lpthread', '-lm', '-ldl',
    '-Wl,--gc-sections', '-o', str(BUILD/'docking_integration')]
subprocess.run(command, check=True)
subprocess.run([str(BUILD/'docking_integration')], cwd=ROOT, check=True, timeout=45)
