// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RENDER_OPENGL_LINUX_EGL_H
#define RENDER_OPENGL_LINUX_EGL_H

#define glTexImage3D glTexImage3D__static
#define glTexSubImage3D glTexSubImage3D__static
#define glActiveTexture glActiveTexture__static
#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#undef glTexImage3D
#undef glTexSubImage3D
#undef glActiveTexture

// libwayland-egl, loaded only when the window manager is Wayland's.
struct wl_egl_window;
#define R_OGL_LNX_WaylandEGLXList \
X(struct wl_egl_window *, wl_egl_window_create, (void *surface, int width, int height))\
X(void, wl_egl_window_destroy, (struct wl_egl_window *window))\
X(void, wl_egl_window_resize, (struct wl_egl_window *window, int width, int height, int dx, int dy))

#define X(ret, name, params) global ret (*r_ogl_lnx_##name) params = 0;
R_OGL_LNX_WaylandEGLXList
#undef X

typedef struct R_OGL_LNX_Window R_OGL_LNX_Window;
struct R_OGL_LNX_Window
{
  R_OGL_LNX_Window *next;
  EGLSurface surface;
  // Wayland: the buffer size, which follows the window's size.
  struct wl_egl_window *wl_window;
  Vec2S32 wl_size;
  B32 swap_interval_set;
};

typedef struct R_OGL_LNX_State R_OGL_LNX_State;
struct R_OGL_LNX_State
{
  Arena *arena;
  EGLDisplay display;
  EGLConfig config;
  EGLContext context;
  // Current until the first window, and whenever none is (#605).
  EGLSurface pbuffer;
  R_OGL_LNX_Window *free_window;
};

global R_OGL_LNX_State *r_ogl_lnx_state = 0;

#endif // RENDER_OPENGL_LINUX_EGL_H
