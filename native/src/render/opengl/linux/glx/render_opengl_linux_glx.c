// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal VoidProc *
r_ogl_os_load_procedure(char *name)
{
  VoidProc *result = (VoidProc *)glXGetProcAddressARB((U8 *)name);
  return result;
}

internal void
r_ogl_os_init(CmdLine *cmdln)
{
  WM_NativeSurface native = wm_native_surface(wm_window_zero());
  if(native.kind != WM_NativeSurfaceKind_X11)
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("OpenGL through GLX needs an X11 window manager."));
    abort_self(1);
  }
  r_ogl_lnx_display = (Display *)native.x11.display;
  
  //- rjf: require GLX 1.3+
  int glx_version_major = 0;
  int glx_version_minor = 0;
  if(!glXQueryVersion(r_ogl_lnx_display, &glx_version_major, &glx_version_minor) ||
     (glx_version_major == 1 && glx_version_minor < 3) ||
     glx_version_major < 1)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 message = push_str8f(scratch.arena, "Unsupported GLX version (%i.%i, need at least 1.3)", glx_version_major, glx_version_minor);
    wm_graphical_message(1, str8_lit("Fatal Error"), message);
    abort_self(1);
    scratch_end(scratch);
  }
  
  //- rjf: get frame buffer configs
  local_persist int framebuffer_config_options[] =
  {
    GLX_X_RENDERABLE,   1,
    GLX_DRAWABLE_TYPE,  GLX_WINDOW_BIT,
    GLX_RENDER_TYPE,    GLX_RGBA_BIT,
    GLX_X_VISUAL_TYPE,  GLX_TRUE_COLOR,
    GLX_RED_SIZE,       8,
    GLX_GREEN_SIZE,     8,
    GLX_BLUE_SIZE,      8,
    GLX_ALPHA_SIZE,     8,
    GLX_DEPTH_SIZE,     24,
    GLX_STENCIL_SIZE,   8,
    GLX_DOUBLEBUFFER,   1,
    None
  };
  int framebuffer_configs_count = 0;
  GLXFBConfig *framebuffer_configs = glXChooseFBConfig(r_ogl_lnx_display, DefaultScreen(r_ogl_lnx_display), framebuffer_config_options, &framebuffer_configs_count);
  if(framebuffer_configs == 0)
  {
    wm_graphical_message(1, str8_lit("Fatal Error"), str8_lit("Could not find a suitable framebuffer configuration."));
    abort_self(1);
  }
  GLXFBConfig framebuffer_config = framebuffer_configs[0];
  XFree(framebuffer_configs);
  
  //- rjf: construct set-window-attributes
  XSetWindowAttributes set_window_attributes = {0};
  set_window_attributes.background_pixmap = None;
  set_window_attributes.border_pixel      = 0;
  set_window_attributes.event_mask        = StructureNotifyMask;
  
  //- rjf: construct context
  {
    B32 debug_mode = cmd_line_has_flag(cmdln, str8_lit("opengl_debug"));
#if BUILD_DEBUG
    debug_mode = 1;
#endif
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
    glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)glXGetProcAddressARB((U8 *)"glXCreateContextAttribsARB");
    int context_options[] =
    {
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 3,
      GLX_CONTEXT_FLAGS_ARB,         !!debug_mode*GLX_CONTEXT_DEBUG_BIT_ARB,
      None
    };
    r_ogl_lnx_ctx = glXCreateContextAttribsARB(r_ogl_lnx_display, framebuffer_config, 0, 1, context_options);
  }
  
  // Bind a 1x1 pbuffer until the first window: NVIDIA's GLX never gives a window a working
  // framebuffer once the context has been current with no drawable (#605).
  // A config without GLX_PBUFFER_BIT would raise BadMatch, which Xlib's default handler exits on.
  GLXPbuffer pbuffer = 0;
  int drawable_type = 0;
  if(glXGetFBConfigAttrib(r_ogl_lnx_display, framebuffer_config, GLX_DRAWABLE_TYPE, &drawable_type) == Success &&
     (drawable_type & GLX_PBUFFER_BIT))
  {
    int pbuffer_options[] = {GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, None};
    pbuffer = glXCreatePbuffer(r_ogl_lnx_display, framebuffer_config, pbuffer_options);
  }
  glXMakeCurrent(r_ogl_lnx_display, pbuffer, r_ogl_lnx_ctx);
}

internal R_Handle
r_ogl_os_window_equip(WM_Window window)
{
  R_Handle result = {0};
  return result;
}

internal void
r_ogl_os_window_unequip(WM_Window os, R_Handle r)
{
  
}

internal void
r_ogl_os_select_window(WM_Window os, R_Handle r)
{
  glXMakeCurrent(r_ogl_lnx_display, (Window)wm_native_surface(os).x11.window, r_ogl_lnx_ctx);
}

internal void
r_ogl_os_window_swap(WM_Window os, R_Handle r)
{
  glXSwapBuffers(r_ogl_lnx_display, (Window)wm_native_surface(os).x11.window);
}

// Admission comes from the surface where the window manager has a signal
// for it (Wayland's frame callbacks); the swap paces frames otherwise.
internal B32
r_ogl_os_window_frame_ready(WM_Window os)
{
  return wm_window_frame_ready(os);
}

internal B32
r_ogl_os_window_frame_pending(WM_Window os)
{
  return wm_window_frame_pending(os);
}
