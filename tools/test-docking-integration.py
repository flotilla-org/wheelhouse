#!/usr/bin/env python3
"""Compile real shell command/drag/restore code with OS and FFI fault boundaries.

Pass the built Andamento and Cleat library directories; no display is required.
Interception boundaries: WM client rectangle, Andamento action dispatch, clock,
worker creation/detach/sleep, and WM wake posting. Review new production call
sites at these boundaries; parser, settings, mounts, renderer, commands,
checker, snapshot refresh and retry worker remain real linked collaborators.
"""
import argparse
import os
import re
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
BUILD.mkdir(exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('andamento_lib', type=Path)
parser.add_argument('cleat_lib', type=Path)
parser.add_argument('--andamento-include', type=Path,
                    default=Path(os.environ.get('WHEELHOUSE_ANDAMENTO_DIR', ROOT.parent/'andamento'))/'crates/andamento-ffi/include')
parser.add_argument('--cleat-include', type=Path,
                    default=Path(os.environ.get('WHEELHOUSE_CLEAT_DIR', ROOT.parent/'cleat'))/'crates/cleat/include')
args = parser.parse_args()
andamento = args.andamento_lib.resolve()
cleat = args.cleat_lib.resolve()
# Reuse the production amalgamation's includes, replacing only its application
# entry point. This keeps the command routing and UI collaborators real.
main = (ROOT / 'src/uishell/uishell_main.c').read_text()
prefix = main[:main.index('internal void\nentry_point(CmdLine *cmd_line)')]
prefix = re.sub(r'^#include "uishell/[^"]*diagnostics.c"\n', '', prefix, flags=re.M)
prefix = prefix.replace('#include "shell/shell_inc.c"', '''
#include "andamento.h"
internal Rng2F32 integration_client_rect(WM_Window window);
internal U64 integration_now_ms(void);
internal int integration_thread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
internal int integration_thread_detach(pthread_t);
internal void integration_sleep_ms(U32);
internal void integration_post_wake(void);
internal U32 integration_dispatch(Andamento *core, const AndamentoSnapshot *snapshot, size_t action, char **error);
#define wm_client_rect_from_window integration_client_rect
#define andamento_dispatch integration_dispatch
#define wheelhouse_ingress_now_ms integration_now_ms
#define pthread_create integration_thread_create
#define pthread_detach integration_thread_detach
#define sleep_ms integration_sleep_ms
#define wm_send_wakeup_event integration_post_wake
#include "shell/shell_inc.c"
#undef wm_client_rect_from_window
#undef andamento_dispatch
#undef wheelhouse_ingress_now_ms
#undef pthread_create
#undef pthread_detach
#undef sleep_ms
#undef wm_send_wakeup_event
''')
source = BUILD / 'docking_integration.c'
source.write_text(prefix + '\n#include "shell/tests/docking_integration.c"\n')
command = shlex.split(os.environ.get('CC', 'clang')) + [
    # Release arena bookkeeping avoids the debug table's 256 GiB reservation.
    '-g', '-O0', '-D_GNU_SOURCE', '-DBUILD_DEBUG=0', '-DNO_ASYNC=1',
    '-DWM_STUB=1', '-DR_BACKEND=0', '-DFP_BACKEND=0', '-ffunction-sections', '-fdata-sections',
    '-Wno-initializer-overrides', '-Wno-unused-value',
    '-Wno-incompatible-pointer-types-discards-qualifiers',
    '-I'+str(ROOT/'src'), '-I'+str(args.andamento_include.resolve()),
    '-I'+str(args.cleat_include.resolve()), str(source),
    '-L'+str(andamento), '-landamento_ffi', '-lwheelhouse_ingress', '-Wl,-rpath,'+str(andamento),
    '-L'+str(cleat), '-lcleat', '-Wl,-rpath,'+str(cleat), '-lpthread', '-lm', '-ldl',
    '-Wl,--gc-sections', '-o', str(BUILD/'docking_integration')]
subprocess.run(command, check=True)
subprocess.run([str(BUILD/'docking_integration'), '--async_thread_count:1'], cwd=ROOT, check=True, timeout=45)
