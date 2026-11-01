// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal VoidProc *
r_ogl_os_load_procedure(char *name)
{
  VoidProc *result = (VoidProc *)eglGetProcAddress(name);
  return result;
}

internal void
r_ogl_os_init(CmdLine *cmdln)
{
  //- rjf: set up state
  {
    Arena *arena = arena_alloc();
    r_ogl_lnx_state = push_array(arena, R_OGL_LNX_State, 1);
    r_ogl_lnx_state->arena = arena;
  }

  //- rjf: get EGL display
  // Only X11 so far: a Wayland window manager would also hand this backend a
  // wl_display here, and a wl_egl_window per window.
  WM_NativeSurface native = wm_native_surface(wm_window_zero());
  if(native.kind != WM_NativeSurfaceKind_X11)
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("OpenGL through EGL needs an X11 window manager."));
    abort_self(1);
  }
  r_ogl_lnx_state->display = eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, native.x11.display, 0);
  if(r_ogl_lnx_state->display == EGL_NO_DISPLAY)
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Failed to get EGL display."));
    abort_self(1);
  }

  //- rjf: initialize EGL
  EGLint egl_version_major = 0;
  EGLint egl_version_minor = 0;
  if(!eglInitialize(r_ogl_lnx_state->display, &egl_version_major, &egl_version_minor))
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Couldn't initialize EGL display."));
    abort_self(1);
  }
  if(egl_version_major < 1 || (egl_version_major == 1 && egl_version_minor < 5))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 message = push_str8f(scratch.arena, "Unsupported EGL version (%i.%i, need at least 1.5)", egl_version_major, egl_version_minor);
    wm_graphical_message(1, str8_lit("Fatal Error"), message);
    abort_self(1);
    scratch_end(scratch);
  }

  //- rjf: pick GL API
  if(!eglBindAPI(EGL_OPENGL_API))
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Couldn't initialize EGL API to OpenGL."));
    abort_self(1);
  }

  //- rjf: choose config
  // The same buffers GLX asked for. Windows are created with the root's
  // visual, and NVIDIA refuses a window surface whose config has another, so
  // the config must match it; one that also makes pbuffers is preferred.
  {
    EGLint options[] =
    {
      EGL_SURFACE_TYPE,      EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE,   EGL_OPENGL_BIT,
      EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
      EGL_RED_SIZE,          8,
      EGL_GREEN_SIZE,        8,
      EGL_BLUE_SIZE,         8,
      EGL_DEPTH_SIZE,        24,
      EGL_STENCIL_SIZE,      8,
      EGL_NONE,
    };
    EGLConfig configs[256] = {0};
    EGLint configs_count = 0;
    if(!eglChooseConfig(r_ogl_lnx_state->display, options, configs, ArrayCount(configs), &configs_count) || configs_count == 0)
    {
      wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Could not find a suitable framebuffer configuration."));
      abort_self(1);
    }
    S32 best_score = -1;
    for(EGLint idx = 0; idx < configs_count; idx += 1)
    {
      EGLint visual_id = 0, surface_type = 0;
      eglGetConfigAttrib(r_ogl_lnx_state->display, configs[idx], EGL_NATIVE_VISUAL_ID, &visual_id);
      eglGetConfigAttrib(r_ogl_lnx_state->display, configs[idx], EGL_SURFACE_TYPE, &surface_type);
      S32 score = 2*((U64)visual_id == native.x11.visual_id) + !!(surface_type & EGL_PBUFFER_BIT);
      if(score > best_score)
      {
        best_score = score;
        r_ogl_lnx_state->config = configs[idx];
      }
    }
  }

  //- rjf: construct context
  {
    B32 debug_mode = cmd_line_has_flag(cmdln, str8_lit("opengl_debug"));
#if BUILD_DEBUG
    debug_mode = 1;
#endif
    EGLint options[] =
    {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
      EGL_CONTEXT_OPENGL_DEBUG, debug_mode ? EGL_TRUE : EGL_FALSE,
      EGL_NONE,
    };
    r_ogl_lnx_state->context = eglCreateContext(r_ogl_lnx_state->display, r_ogl_lnx_state->config, EGL_NO_CONTEXT, options);
    if(r_ogl_lnx_state->context == EGL_NO_CONTEXT)
    {
      wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Couldn't create OpenGL context with EGL."));
      abort_self(1);
    }
  }

  // Bind a 1x1 pbuffer until the first window: NVIDIA never gives a window a
  // working framebuffer once the context has been current with no drawable (#605).
  EGLint surface_type = 0;
  eglGetConfigAttrib(r_ogl_lnx_state->display, r_ogl_lnx_state->config, EGL_SURFACE_TYPE, &surface_type);
  r_ogl_lnx_state->pbuffer = EGL_NO_SURFACE;
  if(surface_type & EGL_PBUFFER_BIT)
  {
    EGLint pbuffer_options[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    r_ogl_lnx_state->pbuffer = eglCreatePbufferSurface(r_ogl_lnx_state->display, r_ogl_lnx_state->config, pbuffer_options);
  }
  eglMakeCurrent(r_ogl_lnx_state->display, r_ogl_lnx_state->pbuffer, r_ogl_lnx_state->pbuffer, r_ogl_lnx_state->context);
}

internal R_Handle
r_ogl_os_window_equip(WM_Window window)
{
  WM_NativeSurface native = wm_native_surface(window);
  R_OGL_LNX_Window *w = r_ogl_lnx_state->free_window;
  if(w != 0)
  {
    SLLStackPop(r_ogl_lnx_state->free_window);
  }
  else
  {
    w = push_array(r_ogl_lnx_state->arena, R_OGL_LNX_Window, 1);
  }
  // On the X11 platform the native window is a pointer to the XID.
  unsigned long xid = (unsigned long)native.x11.window;
  w->surface = eglCreatePlatformWindowSurface(r_ogl_lnx_state->display, r_ogl_lnx_state->config, &xid, 0);
  if(w->surface == EGL_NO_SURFACE)
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Couldn't create EGL surface."));
    abort_self(1);
  }
  R_Handle result = {(U64)w};
  return result;
}

internal void
r_ogl_os_window_unequip(WM_Window os, R_Handle r)
{
  R_OGL_LNX_Window *w = (R_OGL_LNX_Window *)r.u64[0];
  if(eglGetCurrentSurface(EGL_DRAW) == w->surface)
  {
    eglMakeCurrent(r_ogl_lnx_state->display, r_ogl_lnx_state->pbuffer, r_ogl_lnx_state->pbuffer, r_ogl_lnx_state->context);
  }
  eglDestroySurface(r_ogl_lnx_state->display, w->surface);
  w->surface = EGL_NO_SURFACE;
  SLLStackPush(r_ogl_lnx_state->free_window, w);
}

internal void
r_ogl_os_select_window(WM_Window os, R_Handle r)
{
  R_OGL_LNX_Window *w = (R_OGL_LNX_Window *)r.u64[0];
  eglMakeCurrent(r_ogl_lnx_state->display, w->surface, w->surface, r_ogl_lnx_state->context);
}

internal void
r_ogl_os_window_swap(WM_Window os, R_Handle r)
{
  R_OGL_LNX_Window *w = (R_OGL_LNX_Window *)r.u64[0];
  eglSwapBuffers(r_ogl_lnx_state->display, w->surface);
}
