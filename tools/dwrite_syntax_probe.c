// Focused source probe for the Windows DirectWrite font provider.
//
// This is intentionally smaller than the app unity build: it checks the
// provider ABI and implementation without pulling in windowing, rendering, or
// debugger-era Windows shell code.

#define BUILD_DEBUG 1
#define FP_BACKEND FP_BACKEND_DWRITE

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
#include "base/base_log.h"
#include "font_provider/font_provider_inc.h"

#include "font_provider/font_provider_inc.c"
