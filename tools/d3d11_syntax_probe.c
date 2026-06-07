// Focused source probe for the Windows D3D11 renderer.
//
// This is intentionally smaller than the app unity build: it checks that the
// renderer ABI and implementation, including readback support, parse and
// codegen against a Windows target without launching a D3D device.

#define BUILD_DEBUG 1
#define R_BACKEND R_BACKEND_D3D11

#include "base/base_context_cracking.h"

#include <winsock2.h>
#include <windows.h>

#include "base/base_core.h"
#include "base/base_profile.h"
#include "base/base_memory.h"
#include "base/base_arena.h"
#include "base/base_math.h"
#include "base/base_strings.h"
#include "base/base_system.h"
#include "base/base_threads.h"
#include "base/base_thread_context.h"
#include "base/base_files.h"
#include "base/base_shared_memory.h"
#include "base/base_processes.h"
#include "base/base_dynamic_libraries.h"
#include "base/base_command_line.h"
#include "base/base_log.h"
#include "window_manager/window_manager.h"
#include "win32/window_manager/win32_window_manager.h"
#include "render/render_inc.h"

#include "render/render_inc.c"
