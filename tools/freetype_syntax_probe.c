// Focused source probe for the FreeType font provider.
//
// This keeps Linux color-font provider checks separate from the app unity
// build, so provider source issues are not hidden behind windowing/rendering
// dependencies.

#define BUILD_DEBUG 1
#define FP_BACKEND FP_BACKEND_FREETYPE

#include "base/base_context_cracking.h"

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
