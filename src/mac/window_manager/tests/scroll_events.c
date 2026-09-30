// Adapted from RAD e8ecc3121b3e33e6ab12c55d327cf2c95460babb.
// Licensed under the MIT license (https://opensource.org/license/mit/)
#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
global U32 failures;
#define Check(test, name) do { B32 pass = (test); failures += !pass; fprintf(stderr, "%s: %s\n", pass ? "PASS" : "FAIL", name); } while(0)

internal void
entry_point(CmdLine *cmdline)
{
  Temp scratch = scratch_begin(0, 0);
  @autoreleasepool
  {
    for(U32 precise = 0; precise < 2; precise += 1)
    {
      wm_get_events(scratch.arena, 0);
      CGEventRef cg = CGEventCreateScrollWheelEvent(0, precise ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine, 1, 1);
      NSEvent *native = [NSEvent eventWithCGEvent:cg];
      [NSApp postEvent:native atStart:NO];
      WM_EventList translated = wm_get_events(scratch.arena, 0);
      U64 count = 0;
      for(WM_Event *e = translated.first; e != 0; e = e->next)
      {
        if(e->kind == WM_EventKind_Scroll)
        {
          count += 1;
          Check(e->scroll_is_precise == precise && e->delta.y == -(F32)[native scrollingDeltaY],
                "AppKit scroll units and distance survive WM translation");
        }
      }
      Check(count == 1, "one local AppKit event produces one WM scroll event");
      CFRelease(cg);
    }
  }
  fprintf(stderr, "Scroll tests: %u failures\n", failures);
  exit(failures != 0);
}
