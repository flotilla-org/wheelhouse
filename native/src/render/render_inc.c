// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef LAYER_COLOR
#define LAYER_COLOR 0xc22121ff

#include "render_core.c"

#if R_BACKEND == R_BACKEND_STUB
# include "stub/render_stub.c"
#elif R_BACKEND == R_BACKEND_D3D11
# include "d3d11/render_d3d11.c"
#elif R_BACKEND == R_BACKEND_OPENGL
# include "opengl/render_opengl.c"
#elif R_BACKEND == R_BACKEND_METAL
# include "metal/render_metal.c"
#else
# error Renderer backend not specified.
#endif

// The renderer this build draws with, as Help shows it.
internal String8
r_backend_name(void)
{
#if R_BACKEND == R_BACKEND_D3D11
  return str8_lit("Direct3D 11");
#elif R_BACKEND == R_BACKEND_METAL
  return str8_lit("Metal");
#elif R_BACKEND == R_BACKEND_OPENGL && OS_LINUX && R_OPENGL_LINUX_BACKEND == R_OPENGL_LINUX_BACKEND_EGL
  return str8_lit("OpenGL (EGL)");
#elif R_BACKEND == R_BACKEND_OPENGL && OS_LINUX
  return str8_lit("OpenGL (GLX)");
#elif R_BACKEND == R_BACKEND_OPENGL
  return str8_lit("OpenGL");
#else
  return str8_lit("none");
#endif
}
