// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: OS Portion Includes

#if OS_WINDOWS
# include "render/opengl/win32/render_opengl_win32.c"
#elif OS_LINUX
# include "render/opengl/linux/render_opengl_linux.c"
#else
# error OS portion of OpenGL rendering backend not defined.
#endif

// The core shaders, generated from native/src/shaders/*.slang.
#include "shaders/generated/core_shaders_glsl.h"

////////////////////////////////
//~ rjf: Attribute Tables

global read_only R_OGL_Attribute r_ogl_rect_input_attributes[] =
{
  {0, str8_lit_comp("c2v_dst_rect"),        GL_FLOAT,  4},
  {1, str8_lit_comp("c2v_src_rect"),        GL_FLOAT,  4},
  {2, str8_lit_comp("c2v_colors_0"),        GL_FLOAT,  4},
  {3, str8_lit_comp("c2v_colors_1"),        GL_FLOAT,  4},
  {4, str8_lit_comp("c2v_colors_2"),        GL_FLOAT,  4},
  {5, str8_lit_comp("c2v_colors_3"),        GL_FLOAT,  4},
  {6, str8_lit_comp("c2v_corner_radii"),    GL_FLOAT,  4},
  {7, str8_lit_comp("c2v_style"),           GL_FLOAT,  4},
};

global read_only R_OGL_Attribute r_ogl_single_color_output_attributes[] =
{
  {0, str8_lit_comp("final_color")},
};

////////////////////////////////
//~ rjf: Generated Code

#include "render/opengl/generated/render_opengl.meta.c"

////////////////////////////////
//~ rjf: Helpers

internal R_Handle
r_ogl_handle_from_tex2d(R_OGL_Tex2D *t)
{
  R_Handle h = {(U64)t};
  return h;
}

internal R_OGL_Tex2D *
r_ogl_tex2d_from_handle(R_Handle h)
{
  R_OGL_Tex2D *t = (R_OGL_Tex2D *)h.u64[0];
  return t;
}

internal R_OGL_FormatInfo
r_ogl_format_info_from_tex2dformat(R_Tex2DFormat fmt)
{
  R_OGL_FormatInfo result;
  result.internal_format = GL_RGBA8;
  result.format = GL_RGBA;
  result.base_type = GL_UNSIGNED_BYTE;
  // Matches D3D11's DXGI formats (render_d3d11.c), so data and channel maps mean the same on every backend.
  switch(fmt)
  {
    default:{}break;
    case R_Tex2DFormat_R8:    {result.internal_format = GL_R8;      result.format = GL_RED;  result.base_type = GL_UNSIGNED_BYTE;}break;
    case R_Tex2DFormat_RG8:   {result.internal_format = GL_RG8;     result.format = GL_RG;   result.base_type = GL_UNSIGNED_BYTE;}break;
    case R_Tex2DFormat_BGRA8: {result.internal_format = GL_RGBA8;   result.format = GL_BGRA; result.base_type = GL_UNSIGNED_BYTE;}break;
    case R_Tex2DFormat_R16:   {result.internal_format = GL_R16;     result.format = GL_RED;  result.base_type = GL_UNSIGNED_SHORT;}break;
    case R_Tex2DFormat_RGBA16:{result.internal_format = GL_RGBA16;  result.format = GL_RGBA; result.base_type = GL_UNSIGNED_SHORT;}break;
    case R_Tex2DFormat_R32:   {result.internal_format = GL_R32F;    result.format = GL_RED;  result.base_type = GL_FLOAT;}break;
    case R_Tex2DFormat_RG32:  {result.internal_format = GL_RG32F;   result.format = GL_RG;   result.base_type = GL_FLOAT;}break;
    case R_Tex2DFormat_RGBA32:{result.internal_format = GL_RGBA32F; result.format = GL_RGBA; result.base_type = GL_FLOAT;}break;
  }
  return result;
}

// Appends a batch list's instances to the instance ring (see
// R_OGL_InstanceRing), returning the buffer, left bound to GL_ARRAY_BUFFER,
// and the offset of the first instance.
internal R_OGL_InstanceUpload
r_ogl_instance_upload(R_BatchList *batches)
{
  R_OGL_InstanceRing *ring = &r_ogl_state->instance_ring;
  U64 size = batches->byte_count;
  // Uploads start on 256 bytes; attribute pointers start at the upload's
  // offset, so it needn't be a multiple of the instance size.
  U64 offset = AlignPow2(ring->cursor, 256);
  glBindBuffer(GL_ARRAY_BUFFER, ring->buffer);
  if(offset + size > ring->capacity)
  {
    if(size > ring->capacity)
    {
      while(ring->capacity < size) { ring->capacity *= 2; }
      ring->grows += 1;
    }
    else
    {
      ring->orphans += 1;
    }
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ring->capacity, 0, GL_STREAM_DRAW);
    offset = 0;
  }
  U8 *dst = (U8 *)glMapBufferRange(GL_ARRAY_BUFFER, (GLintptr)offset, (GLsizeiptr)size,
                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
  if(dst != 0)
  {
    U64 off = 0;
    for(R_BatchNode *batch_n = batches->first; batch_n != 0; batch_n = batch_n->next)
    {
      MemoryCopy(dst + off, batch_n->v.v, batch_n->v.byte_count);
      off += batch_n->v.byte_count;
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);
  }
  else
  {
    // A driver that can't map the range still takes the data, synchronized.
    U64 off = offset;
    for(R_BatchNode *batch_n = batches->first; batch_n != 0; batch_n = batch_n->next)
    {
      glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)off, batch_n->v.byte_count, batch_n->v.v);
      off += batch_n->v.byte_count;
    }
  }
  ring->cursor = offset + size;
  R_OGL_InstanceUpload result = {ring->buffer, offset};
  return result;
}

internal void
r_ogl_debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
  raddbg_log("[OpenGL] %.*s\n", (int)length, message);
  fprintf(stderr, "[OpenGL] %.*s\n", (int)length, message);
}

internal B32
r_ogl_scissor_from_pixel_clip(Rng2F32 clip_px, Vec2S32 attachment_size)
{
  // clip_px is in the attachment's pixels, top-left origin. Sets the scissor
  // and returns 1 when some of it is on the attachment; returns 0 without
  // touching GL state when none is, so the caller skips the draw. An
  // unclamped, inverted or offscreen rect would make glScissor fail with
  // GL_INVALID_VALUE and leave the previous rect in force.
  S32 x0 = (S32)clip_px.x0;
  S32 y0 = (S32)clip_px.y0;
  S32 x1 = (S32)clip_px.x1 + 1;
  S32 y1 = (S32)clip_px.y1 + 1;
  if(x0 < 0) { x0 = 0; }
  if(y0 < 0) { y0 = 0; }
  if(x1 > attachment_size.x) { x1 = attachment_size.x; }
  if(y1 > attachment_size.y) { y1 = attachment_size.y; }
  if(x1 <= x0 || y1 <= y0)
  {
    return 0;
  }
  glScissor(x0, attachment_size.y - y1, x1 - x0, y1 - y0);
  return 1;
}

// Uploads a core shader's uniform block to binding point 0 or 1, through that
// binding's scratch buffer (re-specified each time, so earlier draws keep
// theirs).
internal void
r_ogl_core_ubo_upload(U32 binding, void *data, U64 size)
{
  GLuint *ubo = (binding == 0 ? &r_ogl_state->core_ubo : &r_ogl_state->core_ubo1);
  if(*ubo == 0)
  {
    glGenBuffers(1, ubo);
  }
  glBindBuffer(GL_UNIFORM_BUFFER, *ubo);
  glBufferData(GL_UNIFORM_BUFFER, size, data, GL_STREAM_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, binding, *ubo);
}

// Sets the bound GL_TEXTURE_2D's sampling. Linear clamps to edge so scaled
// images don't wrap; nearest keeps repeat. Nothing tiles.
internal void
r_ogl_texture_sampling(R_Tex2DSampleKind kind)
{
  GLint wrap_mode = (kind == R_Tex2DSampleKind_Linear) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
  GLint filter = (kind == R_Tex2DSampleKind_Linear) ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
}

// Sets the rect shader's uniforms for one batch group. xform is the group's
// transform as the pass sees it; texture_size is the bound texture's.
internal void
r_ogl_rect_uniforms(Vec2F32 viewport_dim, R_BatchGroup2DParams *group_params, Mat3x3F32 xform,
                    R_Tex2DFormat texture_fmt, Vec2S32 texture_size)
{
  // Surfaces render through the flipped stage projection, so they are stored bottom-up.
  R_CoreRectUniforms uniforms = r_core_rect_uniforms(viewport_dim, group_params, xform, texture_fmt, texture_size, 1);
  r_ogl_core_ubo_upload(0, &uniforms, sizeof(uniforms));
}

// The blur kernel folds each pair of taps into one bilinear sample between
// them (r_core_blur_uniforms), as the linear, clamped samplers on D3D11 and Metal do.
// Nearest sampling would land every folded tap on one texel and turn fine
// detail into its negative instead of averaging it.
internal void
r_ogl_blur_sampling(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Blurs params->rect of stage in place, through scratch: the horizontal pass
// writes scratch, the vertical pass writes back to stage. The kernel packs
// pairs of taps into single bilinear samples (r_core_blur_uniforms). The
// horizontal pass widens its rect to cover the vertical pass's whole input
// and isn't clipped; only the vertical pass is clipped and masked to the
// rounded rect. viewport_dim is in points (the units of params->rect and
// params->clip), pixel_dim the targets' size in pixels.
internal void
r_ogl_blur(R_PassParams_Blur *params, F32 backing_scale, Vec2F32 viewport_dim, Vec2S32 pixel_dim,
                R_OGL_RenderTarget *stage, R_OGL_RenderTarget *scratch)
{
  B32 clipped = (params->clip.x0 != 0 ||
                 params->clip.x1 != 0 ||
                 params->clip.y0 != 0 ||
                 params->clip.y1 != 0);
  Rng2F32 clip_px = r2f32p(params->clip.x0*backing_scale, params->clip.y0*backing_scale, params->clip.x1*backing_scale, params->clip.y1*backing_scale);
  if(clipped && !r_ogl_scissor_from_pixel_clip(clip_px, pixel_dim))
  {
    return;
  }
  
  // The kernel steps in texels, pixels of the window's targets; the stage is stored bottom-up.
  R_CoreBlurUniforms uniforms = r_core_blur_uniforms(params, viewport_dim, params->blur_size*backing_scale, 1);
  
  glViewport(0, 0, pixel_dim.x, pixel_dim.y);
  GLuint shader = r_ogl_state->shaders[R_OGL_ShaderKind_Blur];
  glUseProgramScope(shader)
    glBindVertexArrayScope(r_ogl_state->all_purpose_vao)
  {
    glActiveTexture(GL_TEXTURE0);
    for EachIndex(axis, 2)
    {
      R_OGL_RenderTarget *src = (axis == 0 ? stage : scratch);
      R_OGL_RenderTarget *dst = (axis == 0 ? scratch : stage);
      r_core_blur_pass(&uniforms, params, axis == 0 ? Axis2_X : Axis2_Y, viewport_dim, pixel_dim);
      if(axis == 0)
      {
        glDisable(GL_SCISSOR_TEST);
      }
      else
      {
        if(clipped)
        {
          r_ogl_scissor_from_pixel_clip(clip_px, pixel_dim);
          glEnable(GL_SCISSOR_TEST);
        }
      }
      r_ogl_core_ubo_upload(0, &uniforms, sizeof(uniforms));
      glBindFramebufferScope(GL_FRAMEBUFFER, dst->fbo)
        glBindTextureScope(GL_TEXTURE_2D, src->color_texture)
      {
        r_ogl_blur_sampling();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
    }
  }
  glDisable(GL_SCISSOR_TEST);
}

// (Re)makes a Geo3D target: 8-bit colour, as Metal's, and a 24-bit depth
// texture, both size pixels.
internal void
r_ogl_geo3d_target_alloc(R_OGL_RenderTarget *geo, GLuint *depth_texture, Vec2S32 size)
{
  if(geo->fbo != 0)
  {
    glDeleteFramebuffers(1, &geo->fbo);
    glDeleteTextures(1, &geo->color_texture);
    glDeleteTextures(1, depth_texture);
  }
  glGenFramebuffers(1, &geo->fbo);
  glGenTextures(1, &geo->color_texture);
  glGenTextures(1, depth_texture);
  glBindFramebufferScope(GL_FRAMEBUFFER, geo->fbo)
  {
    glBindTextureScope(GL_TEXTURE_2D, geo->color_texture)
    {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, geo->color_texture, 0);
    }
    glBindTextureScope(GL_TEXTURE_2D, *depth_texture)
    {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size.x, size.y, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, *depth_texture, 0);
    }
  }
}

// Draws a Geo3D pass's meshes into geo (colour and depth, geo_size pixels,
// cleared here), then composites the pass's viewport from there into the
// stage or, when the pass has one, its surface. Points are backing_scale
// pixels, except inside a surface, whose scale comes from its own size.
// Every target is stored bottom-up, so the mesh viewport is placed from the
// bottom and the composite samples geo with its v flipped. Groups draw in
// submission order: alpha blending over the depth buffer needs it, and
// callers submit far to near.
internal void
r_ogl_geo3d(R_PassParams_Geo3D *params, F32 backing_scale,
            R_OGL_RenderTarget *geo, Vec2S32 geo_size, R_OGL_RenderTarget *stage, Vec2S32 stage_size)
{
  R_OGL_Tex2D *target = r_ogl_tex2d_from_handle(params->target);
  B32 to_surface = (target != 0 && target->id != 0);
  if(to_surface && params->preserve)
  {
    // surface content is up-to-date; skip the render & keep the texture
    return;
  }

  //- meshes -> geo
  glBindFramebufferScope(GL_FRAMEBUFFER, geo->fbo)
    glUseProgramScope(r_ogl_state->shaders[R_OGL_ShaderKind_Mesh])
    glBindVertexArrayScope(r_ogl_state->mesh_vao)
  {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, geo_size.x, geo_size.y);
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Rng2F32 vp = params->viewport;
    glViewport((GLint)(vp.x0*backing_scale), (GLint)(geo_size.y - vp.y1*backing_scale),
               (GLsizei)((vp.x1 - vp.x0)*backing_scale), (GLsizei)((vp.y1 - vp.y0)*backing_scale));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    R_CoreMeshUniforms uniforms = {0};
    uniforms.view_projection = mul_4x4f32(params->projection, params->view);
    r_ogl_core_ubo_upload(0, &uniforms, sizeof(uniforms));

    for(R_BatchGroup3DMapNode *n = params->mesh_batches.insert_first; n != 0; n = n->insert_next)
    {
      R_BatchGroup3DParams *group_params = &n->params;
      R_OGL_Buffer *mesh_vertices = (R_OGL_Buffer *)group_params->mesh_vertices.u64[0];
      R_OGL_Buffer *mesh_indices = (R_OGL_Buffer *)group_params->mesh_indices.u64[0];
      if(group_params->mesh_geo_topology != R_GeoTopologyKind_Triangles ||
         mesh_vertices == 0 || mesh_vertices->id == 0 || mesh_indices == 0 || mesh_indices->id == 0 ||
         n->batches.byte_count == 0 || n->batches.bytes_per_inst == 0)
      {
        continue;
      }

      // vertices: position, normal, texcoord, rgb (locations 0-3)
      glBindBuffer(GL_ARRAY_BUFFER, mesh_vertices->id);
      U32 vertex_counts[] = {3, 3, 2, 3};
      U64 vertex_off = 0;
      for EachElement(idx, vertex_counts)
      {
        glEnableVertexAttribArray((GLuint)idx);
        glVertexAttribDivisor((GLuint)idx, 0);
        glVertexAttribPointer((GLuint)idx, vertex_counts[idx], GL_FLOAT, GL_FALSE, R_CORE_MESH_VERTEX_STRIDE, (void *)vertex_off);
        vertex_off += vertex_counts[idx]*sizeof(F32);
      }

      // instances: four transform columns, then the albedo remap (locations 4-8)
      U64 inst_count = n->batches.byte_count / n->batches.bytes_per_inst;
      R_OGL_InstanceUpload inst = r_ogl_instance_upload(&n->batches);
      for(U32 idx = 0; idx < 5; idx += 1)
      {
        glEnableVertexAttribArray(4+idx);
        glVertexAttribDivisor(4+idx, 1);
        glVertexAttribPointer(4+idx, 4, GL_FLOAT, GL_FALSE, (GLsizei)n->batches.bytes_per_inst, (void *)(inst.offset + idx*sizeof(Vec4F32)));
      }
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      // albedo (white when absent; the shader doesn't sample it then)
      R_OGL_Tex2D *albedo = r_ogl_tex2d_from_handle(group_params->albedo_tex);
      B32 has_albedo = (albedo != 0 && albedo->id != 0);
      R_CoreMeshGroupUniforms group_uniforms = {0};
      group_uniforms.has_albedo = (F32)!!has_albedo;
      group_uniforms.albedo_sample_is_surface = (F32)!!group_params->albedo_tex_sample_is_surface;
      r_ogl_core_ubo_upload(1, &group_uniforms, sizeof(group_uniforms));
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, has_albedo ? albedo->id : r_ogl_state->white_texture);
      r_ogl_texture_sampling(group_params->albedo_tex_sample_kind);

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_indices->id);
      glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)(mesh_indices->size/sizeof(U32)), GL_UNSIGNED_INT, 0, (GLsizei)inst_count);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDisable(GL_DEPTH_TEST);
  }

  //- geo -> stage or surface
  GLuint dst_fbo = stage->fbo;
  Vec2S32 attachment_size = stage_size;
  Vec2F32 target_origin = v2f32(0, 0);
  F32 target_scale = backing_scale;
  if(to_surface)
  {
    if(r_ogl_state->surface_fbo == 0)
    {
      glGenFramebuffers(1, &r_ogl_state->surface_fbo);
    }
    dst_fbo = r_ogl_state->surface_fbo;
    attachment_size = target->size;
    target_origin = params->target_rect.p0;
    Vec2F32 target_rect_dim = dim_2f32(params->target_rect);
    if(target_rect_dim.x > 0)
    {
      target_scale = (F32)target->size.x/target_rect_dim.x;
    }
  }
  B32 clipped = (params->clip.x0 != 0 || params->clip.y0 != 0 || params->clip.x1 != 0 || params->clip.y1 != 0);
  glBindFramebufferScope(GL_FRAMEBUFFER, dst_fbo)
    glUseProgramScope(r_ogl_state->shaders[R_OGL_ShaderKind_Composite])
    glBindVertexArrayScope(r_ogl_state->all_purpose_vao)
  {
    if(to_surface)
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->id, 0);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glViewport(0, 0, attachment_size.x, attachment_size.y);
    B32 visible = 1;
    if(clipped)
    {
      Rng2F32 clip = shift_2f32(params->clip, v2f32(-target_origin.x, -target_origin.y));
      Rng2F32 clip_px = r2f32p(clip.x0*target_scale, clip.y0*target_scale, clip.x1*target_scale, clip.y1*target_scale);
      visible = r_ogl_scissor_from_pixel_clip(clip_px, attachment_size);
      if(visible)
      {
        glEnable(GL_SCISSOR_TEST);
      }
    }
    if(visible)
    {
      R_CoreCompositeUniforms uniforms = r_core_composite_uniforms(params->viewport, target_origin, target_scale, attachment_size,
                                                                   backing_scale, geo_size, 1);
      r_ogl_core_ubo_upload(0, &uniforms, sizeof(uniforms));
      glActiveTexture(GL_TEXTURE0);
      glBindTextureScope(GL_TEXTURE_2D, geo->color_texture)
      {
        r_ogl_texture_sampling(R_Tex2DSampleKind_Linear);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
    }
    glDisable(GL_SCISSOR_TEST);
  }
  glViewport(0, 0, stage_size.x, stage_size.y);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

////////////////////////////////
//~ rjf: Backend Hooks

//- rjf: top-level layer initialization

r_hook void
r_init(CmdLine *cmdln)
{
  //- rjf: do os-specific portion of work
  r_ogl_os_init(cmdln);
  
  //- rjf: top-level initialization
  Arena *arena = arena_alloc();
  r_ogl_state = push_array(arena, R_OGL_State, 1);
  r_ogl_state->arena = arena;
  
  //- rjf: load gl procedures
#define X(name, r, p) name = (name##_FunctionType *)r_ogl_os_load_procedure(#name);
  R_OGL_ProcedureXList
#undef X
  
  //- rjf: build all shaders
  for EachEnumVal(R_OGL_ShaderKind, k)
  {
    // rjf: compile
    struct {GLenum type; String8 *src; GLuint out; String8 errors;} stages[] =
    {
      {GL_VERTEX_SHADER,   r_ogl_shader_kind_vshad_src_table[k]},
      {GL_FRAGMENT_SHADER, r_ogl_shader_kind_pshad_src_table[k]},
    };
    for EachElement(idx, stages)
    {
      stages[idx].out = glCreateShader(stages[idx].type);
      GLint src_size = stages[idx].src->size;
      glShaderSource(stages[idx].out, 1, (char **)&stages[idx].src->str, &src_size);
      glCompileShader(stages[idx].out);
      GLint info_log_length = 0;
      GLint status = 0;
      glGetShaderiv(stages[idx].out, GL_COMPILE_STATUS, &status);
      glGetShaderiv(stages[idx].out, GL_INFO_LOG_LENGTH, &info_log_length);
      if(info_log_length != 0)
      {
        stages[idx].errors.str = push_array(r_ogl_state->arena, U8, info_log_length+1);
        stages[idx].errors.size = info_log_length;
        glGetShaderInfoLog(stages[idx].out, info_log_length, 0, (char *)stages[idx].errors.str);
        raddbg_log("[OpenGL shader] %.*s\n", (int)stages[idx].errors.size, stages[idx].errors.str);
        if(status == 0)
        {
          fprintf(stderr, "[OpenGL shader %.*s] %.*s\n", str8_varg(r_ogl_shader_kind_name_table[k]), (int)stages[idx].errors.size, stages[idx].errors.str);
        }
      }
    }
    
    // rjf: attach compilations to program
    GLuint program = glCreateProgram();
    for EachElement(idx, stages)
    {
      glAttachShader(program, stages[idx].out);
    }
    
    // rjf: bind inputs
    R_OGL_AttributeArray inputs = r_ogl_shader_kind_input_attributes_table[k];
    for EachIndex(idx, inputs.count)
    {
      glBindAttribLocation(program, inputs.v[idx].index, (char *)inputs.v[idx].name.str);
    }
    
    // rjf: bind outputs
    R_OGL_AttributeArray outputs = r_ogl_shader_kind_output_attributes_table[k];
    for EachIndex(idx, outputs.count)
    {
      glBindFragDataLocation(program, outputs.v[idx].index, (char *)outputs.v[idx].name.str);
    }
    
    // rjf: link / validate / store
    glLinkProgram(program);
    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if(link_status == 0)
    {
      char log_buffer[2048] = {0};
      glGetProgramInfoLog(program, sizeof(log_buffer)-1, 0, log_buffer);
      fprintf(stderr, "[OpenGL shader %.*s] link failed: %s\n", str8_varg(r_ogl_shader_kind_name_table[k]), log_buffer);
    }
    // Uniform blocks read from binding point 0, except the mesh shader's group
    // block (HLSL b1), which reads from 1. GL numbers a program's blocks in
    // its own order, so that one is found by name.
    GLint block_count = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &block_count);
    for(GLint block_idx = 0; block_idx < block_count; block_idx += 1)
    {
      glUniformBlockBinding(program, (GLuint)block_idx, 0);
    }
    GLuint group_block = glGetUniformBlockIndex(program, "MeshGroupUniforms_std140");
    if(group_block != GL_INVALID_INDEX)
    {
      glUniformBlockBinding(program, group_block, 1);
    }
    glValidateProgram(program);
    r_ogl_state->shaders[k] = program;
  }
  
  //- rjf: set up built-in resources
  {
    // rjf: all-purpose VAO
    glGenVertexArrays(1, &r_ogl_state->all_purpose_vao);
    glGenVertexArrays(1, &r_ogl_state->mesh_vao);
    
    // Instance ring: 4 MB holds a typical frame's instances (128 bytes each).
    {
      R_OGL_InstanceRing *ring = &r_ogl_state->instance_ring;
      ring->capacity = MB(4);
      glGenBuffers(1, &ring->buffer);
      glBindBuffer(GL_ARRAY_BUFFER, ring->buffer);
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ring->capacity, 0, GL_STREAM_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    
    // Tightly packed rows: R8 glyph pages fill rects of any width (#605).
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // rjf: white texture
    {
      glGenTextures(1, &r_ogl_state->white_texture);
      glBindTexture(GL_TEXTURE_2D, r_ogl_state->white_texture);
      U32 white_pixel = 0xffffffff;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
    }
  }
  
  //- rjf: set up options
  glEnable(GL_FRAMEBUFFER_SRGB);
  
  //- rjf: set up debug callback
  B32 debug_mode = cmd_line_has_flag(cmdln, str8_lit("opengl_debug"));
#if BUILD_DEBUG
  debug_mode = 1;
#endif
  if(debug_mode)
  {
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(r_ogl_debug_message_callback, 0);
  }
}

//- rjf: window setup/teardown

r_hook R_Handle
r_window_equip(WM_Window window)
{
  R_OGL_Window *w = r_ogl_state->free_window;
  if(w != 0)
  {
    SLLStackPop(r_ogl_state->free_window);
  }
  else
  {
    w = push_array(r_ogl_state->arena, R_OGL_Window, 1);
  }
  MemoryZeroStruct(w);
  w->os = r_ogl_os_window_equip(window);
  R_Handle result = {(U64)w};
  return result;
}

r_hook void
r_window_unequip(WM_Window window, R_Handle window_equip)
{
  R_OGL_Window *w = (R_OGL_Window *)window_equip.u64[0];
  if(w != 0)
  {
    r_ogl_os_window_unequip(window, w->os);
    SLLStackPush(r_ogl_state->free_window, w);
  }
}

//- rjf: textures

r_hook R_Handle
r_tex2d_alloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void *data)
{
  //- rjf: allocate texture record
  R_OGL_Tex2D *tex2d = r_ogl_state->free_tex2d;
  if(tex2d)
  {
    SLLStackPop(r_ogl_state->free_tex2d);
  }
  else
  {
    tex2d = push_array(r_ogl_state->arena, R_OGL_Tex2D, 1);
  }
  
  //- rjf: map kind/format -> gl counterparts
  R_OGL_FormatInfo gl_fmt_info = r_ogl_format_info_from_tex2dformat(format);
  
  //- rjf: allocate GL texture
  {
    glGenTextures(1, &tex2d->id);
    glBindTexture(GL_TEXTURE_2D, tex2d->id);
    glTexImage2D(GL_TEXTURE_2D, 0, gl_fmt_info.internal_format, size.x, size.y, 0, gl_fmt_info.format, gl_fmt_info.base_type, data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  
  //- rjf: fill
  tex2d->resource_kind = kind;
  tex2d->fmt = format;
  tex2d->size = size;
  
  //- rjf: bundle & return
  R_Handle result = r_ogl_handle_from_tex2d(tex2d);
  return result;
}

r_hook R_Handle
r_effect_alloc(String8 name, R_EffectSources *sources)
{
  R_Handle result = {0};
  struct {GLenum type; String8 src; GLuint id;} stages[] =
  {
    {GL_VERTEX_SHADER,   sources->glsl_vs},
    {GL_FRAGMENT_SHADER, sources->glsl_fs},
  };
  B32 good = 1;
  for EachElement(idx, stages)
  {
    stages[idx].id = glCreateShader(stages[idx].type);
    GLint src_size = (GLint)stages[idx].src.size;
    char *src_ptr = (char *)stages[idx].src.str;
    glShaderSource(stages[idx].id, 1, &src_ptr, &src_size);
    glCompileShader(stages[idx].id);
    GLint status = 0;
    glGetShaderiv(stages[idx].id, GL_COMPILE_STATUS, &status);
    if(status == 0)
    {
      good = 0;
      GLint info_log_length = 0;
      glGetShaderiv(stages[idx].id, GL_INFO_LOG_LENGTH, &info_log_length);
      if(info_log_length != 0)
      {
        Temp scratch = scratch_begin(0, 0);
        char *log_buffer = push_array(scratch.arena, char, info_log_length+1);
        glGetShaderInfoLog(stages[idx].id, info_log_length, 0, log_buffer);
        fprintf(stderr, "[OpenGL effect %.*s] %s\n", (int)name.size, name.str, log_buffer);
        scratch_end(scratch);
      }
    }
  }
  if(good)
  {
    GLuint program = glCreateProgram();
    for EachElement(idx, stages)
    {
      glAttachShader(program, stages[idx].id);
    }
    glLinkProgram(program);
    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if(link_status != 0)
    {
      // route the effect's sole uniform block to binding point 0
      GLint block_count = 0;
      glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &block_count);
      if(block_count > 0)
      {
        glUniformBlockBinding(program, 0, 0);
      }
      R_OGL_Effect *effect = push_array(r_ogl_state->arena, R_OGL_Effect, 1);
      effect->program = program;
      result.u64[0] = (U64)effect;
    }
    else
    {
      fprintf(stderr, "[OpenGL effect %.*s] link failed\n", (int)name.size, name.str);
    }
  }
  for EachElement(idx, stages)
  {
    glDeleteShader(stages[idx].id);
  }
  return result;
}

r_hook R_Handle
r_tex2d_alloc_render_target(Vec2S32 size)
{
  R_OGL_Tex2D *tex2d = r_ogl_state->free_tex2d;
  if(tex2d)
  {
    SLLStackPop(r_ogl_state->free_tex2d);
  }
  else
  {
    tex2d = push_array(r_ogl_state->arena, R_OGL_Tex2D, 1);
  }
  {
    glGenTextures(1, &tex2d->id);
    glBindTexture(GL_TEXTURE_2D, tex2d->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, Max(size.x, 1), Max(size.y, 1), 0, GL_RGBA, GL_HALF_FLOAT, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  tex2d->resource_kind = R_ResourceKind_Static;
  // NOTE: render targets share the stage's float pixel format, which has no
  // R_Tex2DFormat; RGBA16 gives the identity sample channel map
  tex2d->fmt = R_Tex2DFormat_RGBA16;
  tex2d->size = size;
  R_Handle result = r_ogl_handle_from_tex2d(tex2d);
  return result;
}

r_hook void
r_tex2d_release(R_Handle texture)
{
  R_OGL_Tex2D *t = r_ogl_tex2d_from_handle(texture);
  if(t != 0)
  {
    glDeleteTextures(1, &t->id);
    SLLStackPush(r_ogl_state->free_tex2d, t);
  }
}

r_hook R_ResourceKind
r_kind_from_tex2d(R_Handle texture)
{
  R_ResourceKind result = R_ResourceKind_Static;
  R_OGL_Tex2D *t = r_ogl_tex2d_from_handle(texture);
  if(t)
  {
    result = t->resource_kind;
  }
  return result;
}

r_hook Vec2S32
r_size_from_tex2d(R_Handle texture)
{
  Vec2S32 result = {0, 0};
  R_OGL_Tex2D *t = r_ogl_tex2d_from_handle(texture);
  if(t)
  {
    result = t->size;
  }
  return result;
}

r_hook R_Tex2DFormat
r_format_from_tex2d(R_Handle texture)
{
  R_Tex2DFormat result = R_Tex2DFormat_RGBA8;
  R_OGL_Tex2D *t = r_ogl_tex2d_from_handle(texture);
  if(t)
  {
    result = t->fmt;
  }
  return result;
}

r_hook void
r_fill_tex2d_region(R_Handle texture, Rng2S32 subrect, void *data)
{
  R_OGL_Tex2D *t = r_ogl_tex2d_from_handle(texture);
  if(t)
  {
    R_OGL_FormatInfo fmt_info = r_ogl_format_info_from_tex2dformat(t->fmt);
    glBindTexture(GL_TEXTURE_2D, t->id);
    Vec2S32 rect_size = dim_2s32(subrect);
    glTexSubImage2D(GL_TEXTURE_2D, 0, subrect.x0, subrect.y0, rect_size.x, rect_size.y, fmt_info.format, fmt_info.base_type, data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}

//- rjf: buffers

r_hook R_Handle
r_buffer_alloc(R_ResourceKind kind, U64 size, void *data)
{
  R_OGL_Buffer *buffer = r_ogl_state->free_buffer;
  if(buffer != 0)
  {
    SLLStackPop(r_ogl_state->free_buffer);
    MemoryZeroStruct(buffer);
  }
  else
  {
    buffer = push_array(r_ogl_state->arena, R_OGL_Buffer, 1);
  }
  buffer->resource_kind = kind;
  buffer->size = size;
  glGenBuffers(1, &buffer->id);
  glBindBuffer(GL_ARRAY_BUFFER, buffer->id);
  glBufferData(GL_ARRAY_BUFFER, size, data, kind == R_ResourceKind_Static ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  R_Handle result = {0};
  result.u64[0] = (U64)buffer;
  return result;
}

r_hook void
r_buffer_release(R_Handle handle)
{
  R_OGL_Buffer *buffer = (R_OGL_Buffer *)handle.u64[0];
  if(buffer != 0)
  {
    // GL keeps a deleted buffer alive until draws already issued with it finish.
    glDeleteBuffers(1, &buffer->id);
    buffer->id = 0;
    SLLStackPush(r_ogl_state->free_buffer, buffer);
  }
}

//- rjf: frame markers

r_hook void
r_window_request_frame(WM_Window window, R_Handle window_equip, B32 requested)
{
}

r_hook B32
r_window_frame_ready(WM_Window window, R_Handle window_equip)
{
  return r_ogl_os_window_frame_ready(window);
}

r_hook B32
r_window_frame_pending(WM_Window window, R_Handle window_equip)
{
  return r_ogl_os_window_frame_pending(window);
}

r_hook void
r_begin_frame(void)
{
  // TODO(rjf)
}

r_hook void
r_end_frame(void)
{
}

internal Vec2S32
r_ogl_pixel_size_from_window(WM_Window os)
{
  Vec2F32 dim = dim_2f32(wm_client_rect_from_window(os));
  F32 scale = wm_backing_scale_from_window(os);
  return v2s32(Max((S32)round_f32(dim.x*scale), 1), Max((S32)round_f32(dim.y*scale), 1));
}

r_hook void
r_window_begin_frame(WM_Window os, R_Handle r)
{
  R_OGL_Window *window = (R_OGL_Window *)r.u64[0];
  r_ogl_os_select_window(os, window->os);
  
  //- rjf: unpack window viewport info
  Rng2F32 client_rect = wm_client_rect_from_window(os);
  Vec2F32 client_rect_dim = dim_2f32(client_rect);
  Vec2S32 pixel_dim = r_ogl_pixel_size_from_window(os);
  window->last_client_rect_dim = client_rect_dim;
  window->last_backing_scale = wm_backing_scale_from_window(os);
  
  //- rjf: set up targets if needed
  if(pixel_dim.x != window->last_pixel_dim.x || pixel_dim.y != window->last_pixel_dim.y)
  {
    window->last_pixel_dim = pixel_dim;
    R_OGL_RenderTarget *targets[] =
    {
      &window->stage_target,
      &window->stage_scratch_target,
    };
    for EachElement(idx, targets)
    {
      if(targets[idx]->fbo != 0)
      {
        glDeleteFramebuffers(1, &targets[idx]->fbo);
        glDeleteTextures(1, &targets[idx]->color_texture);
      }
      glGenFramebuffers(1, &targets[idx]->fbo);
      glGenTextures(1, &targets[idx]->color_texture);
      glBindFramebufferScope(GL_FRAMEBUFFER, targets[idx]->fbo)
        glBindTextureScope(GL_TEXTURE_2D, targets[idx]->color_texture)
      {
        // The stage holds linear color (GL_FRAMEBUFFER_SRGB encodes only on the
        // final blit), so 8 bits per channel bands dark gradients and blurs.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, pixel_dim.x, pixel_dim.y, 0, GL_RGBA, GL_HALF_FLOAT, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[idx]->color_texture, 0);
      }
    }
    r_ogl_geo3d_target_alloc(&window->geo3d_target, &window->geo3d_depth_texture, pixel_dim);
  }
  
  //- rjf: clear and reset state
  {
    GLuint fbos[] =
    {
      window->stage_target.fbo,
      window->stage_scratch_target.fbo,
      0,
    };
    for EachElement(idx, fbos)
    {
      glBindFramebufferScope(GL_FRAMEBUFFER, fbos[idx])
      {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      }
    }
  }
  glViewport(0, 0, pixel_dim.x, pixel_dim.y);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

r_hook void
r_window_end_frame(WM_Window os, R_Handle r)
{
  R_OGL_Window *window = (R_OGL_Window *)r.u64[0];
  
  //- rjf: blit window's main staging frame buffer to window frame buffer
  glBindFramebufferScope(GL_READ_FRAMEBUFFER, window->stage_target.fbo) glBindFramebufferScope(GL_DRAW_FRAMEBUFFER, 0)
  {
    glBlitFramebuffer(0, 0, window->last_pixel_dim.x, window->last_pixel_dim.y,
                      0, 0, window->last_pixel_dim.x, window->last_pixel_dim.y,
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
  }
  
  //- rjf: swap buffers
  r_ogl_os_window_swap(os, window->os);
}

//- rjf: render pass submission

//- render passes shared by window submission and readback, so the
// diagnostics read back what windows draw

// Draws a UI pass into its surface target, or else into the stage. Passes are
// in points (viewport_dim); the stage is pixel_dim pixels. A surface target's
// first pass in a submission clears it; touched remembers which.
internal void
r_ogl_ui_pass(R_PassParams_UI *params, GLuint stage_fbo, Vec2F32 viewport_dim, Vec2S32 pixel_dim, F32 backing_scale,
              R_OGL_TouchedTargets *touched)
{
  //- rjf: unpack params
  R_BatchGroup2DList *rect_batch_groups = &params->rects;

  //- rjf: unpack optional render target; surface passes draw into their own
  // texture (cleared to transparent), in coordinates relative to target_rect
  R_OGL_Tex2D *target = r_ogl_tex2d_from_handle(params->target);
  B32 to_surface = (target != 0 && target->id != 0);
  if(to_surface && params->preserve)
  {
    // surface content is up-to-date; skip the render & keep the texture
    return;
  }
  Vec2F32 pass_viewport_dim = viewport_dim;
  Vec2S32 pass_attachment_size = pixel_dim;
  Vec2F32 target_origin = v2f32(0, 0);
  F32 pass_scale = backing_scale;
  if(to_surface)
  {
    pass_viewport_dim = dim_2f32(params->target_rect);
    pass_attachment_size = target->size;
    target_origin = params->target_rect.p0;
    // surfaces may be allocated at any resolution (e.g. reduced-res
    // previews); derive the point->pixel scale from the target itself
    if(pass_viewport_dim.x > 0)
    {
      pass_scale = (F32)target->size.x/pass_viewport_dim.x;
    }
  }
  B32 target_first_touch = 0;
  if(to_surface)
  {
    target_first_touch = 1;
    for(U64 idx = 0; idx < touched->count; idx += 1)
    {
      if(touched->v[idx] == target->id)
      {
        target_first_touch = 0;
        break;
      }
    }
    if(target_first_touch && touched->count < ArrayCount(touched->v))
    {
      touched->v[touched->count] = target->id;
      touched->count += 1;
    }
    if(r_ogl_state->surface_fbo == 0)
    {
      glGenFramebuffers(1, &r_ogl_state->surface_fbo);
    }
  }

  //- rjf: draw each batch group
  GLuint shader = r_ogl_state->shaders[R_OGL_ShaderKind_Rect];
  glUseProgramScope(shader)
    glBindVertexArrayScope(r_ogl_state->all_purpose_vao)
    glBindFramebufferScope(GL_FRAMEBUFFER, to_surface ? r_ogl_state->surface_fbo : stage_fbo)
  {
    if(to_surface)
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->id, 0);
      if(target_first_touch)
      {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
      }
      // NOTE: accumulate source-over coverage in dst alpha, so transparent-cleared
      // surfaces end up holding premultiplied color + coverage; the stage's alpha
      // is never read again, so the plain blend func is fine there
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glViewport(0, 0, pass_attachment_size.x, pass_attachment_size.y);
    for(R_BatchGroup2DNode *group_n = rect_batch_groups->first; group_n != 0; group_n = group_n->next)
    {
      R_BatchList *batches = &group_n->batches;
      R_BatchGroup2DParams *group_params = &group_n->params;
      if(batches->byte_count == 0 || batches->bytes_per_inst == 0)
      {
        continue;
      }
      
      //- rjf: unpack texture
      R_Tex2DFormat texture_fmt = R_Tex2DFormat_RGBA8;
      GLuint texture_id = r_ogl_state->white_texture;
      Vec2S32 texture_size = v2s32(1, 1);
      {
        R_OGL_Tex2D *tex = r_ogl_tex2d_from_handle(group_params->tex);
        if(tex != 0)
        {
          texture_id = tex->id;
          texture_fmt = tex->fmt;
          texture_size = tex->size;
        }
      }
      
      //- rjf: get & fill buffer
      R_OGL_InstanceUpload inst = r_ogl_instance_upload(batches);
      
      //- rjf: bind input attributes
      // Every rect attribute is a float vector (R_Rect2DInst is eight float4s,
      // statically asserted); an integer one would need glVertexAttribIPointer
      // and its own size.
      {
        R_OGL_AttributeArray inputs = r_ogl_shader_kind_input_attributes_table[R_OGL_ShaderKind_Rect];
        U64 off = inst.offset;
        for EachIndex(idx, inputs.count)
        {
          Assert(inputs.v[idx].type == GL_FLOAT);
          glEnableVertexAttribArray(inputs.v[idx].index);
          glVertexAttribDivisor(inputs.v[idx].index, 1);
          glVertexAttribPointer(inputs.v[idx].index, inputs.v[idx].count, inputs.v[idx].type, GL_FALSE, sizeof(R_Rect2DInst), (void *)(off));
          off += inputs.v[idx].count*sizeof(F32);
        }
      }
      
      //- rjf: bind texture
      {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        r_ogl_texture_sampling(group_params->tex_sample_kind);
      }
      
      //- rjf: upload misc. uniforms
      {
        Mat3x3F32 group_xform = group_params->xform;
        if(to_surface)
        {
          group_xform = mul_3x3f32(make_translate_3x3f32(v2f32(-target_origin.x, -target_origin.y)), group_xform);
        }
        r_ogl_rect_uniforms(pass_viewport_dim, group_params, group_xform, texture_fmt, texture_size);
      }

      //- rjf: set up scissor
      if(group_params->clip.x0 != 0 ||
         group_params->clip.x1 != 0 ||
         group_params->clip.y0 != 0 ||
         group_params->clip.y1 != 0)
      {
        Rng2F32 clip = group_params->clip;
        if(to_surface)
        {
          clip = shift_2f32(clip, v2f32(-target_origin.x, -target_origin.y));
        }
        Rng2F32 clip_px = r2f32p(clip.x0*pass_scale, clip.y0*pass_scale, clip.x1*pass_scale, clip.y1*pass_scale);
        if(!r_ogl_scissor_from_pixel_clip(clip_px, pass_attachment_size))
        {
          continue;
        }
        glEnable(GL_SCISSOR_TEST);
      }

      //- rjf: draw
      {
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batches->byte_count / batches->bytes_per_inst);
      }

      //- rjf: unset scissor
      glDisable(GL_SCISSOR_TEST);
    }
  }
  if(to_surface)
  {
    glViewport(0, 0, pixel_dim.x, pixel_dim.y);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
}

// Runs one effect: a fullscreen triangle from params->source into
// params->target, leaving the viewport on the stage's pixel_dim.
internal void
r_ogl_effect_pass(R_PassParams_Effect *params, Vec2S32 pixel_dim)
{
  R_OGL_Effect *effect = (R_OGL_Effect *)params->effect.u64[0];
  R_OGL_Tex2D *source = r_ogl_tex2d_from_handle(params->source);
  R_OGL_Tex2D *target = r_ogl_tex2d_from_handle(params->target);
  if(effect != 0 && effect->program != 0 &&
     source != 0 && source->id != 0 &&
     target != 0 && target->id != 0)
  {
    if(r_ogl_state->surface_fbo == 0)
    {
      glGenFramebuffers(1, &r_ogl_state->surface_fbo);
    }
    if(r_ogl_state->effect_ubo == 0)
    {
      glGenBuffers(1, &r_ogl_state->effect_ubo);
    }
    R_OGL_EffectUniforms uniforms = {0};
    uniforms.source_size_px = v2f32((F32)Max(source->size.x, 1), (F32)Max(source->size.y, 1));
    uniforms.output_size_px = v2f32((F32)Max(target->size.x, 1), (F32)Max(target->size.y, 1));
    uniforms.params0 = params->params[0];
    uniforms.params1 = params->params[1];
    glBindBuffer(GL_UNIFORM_BUFFER, r_ogl_state->effect_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(uniforms), &uniforms, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glUseProgramScope(effect->program)
      glBindVertexArrayScope(r_ogl_state->all_purpose_vao)
      glBindFramebufferScope(GL_FRAMEBUFFER, r_ogl_state->surface_fbo)
    {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->id, 0);
      glViewport(0, 0, target->size.x, target->size.y);
      glDisable(GL_BLEND);
      glDisable(GL_SCISSOR_TEST);
      glBindBufferBase(GL_UNIFORM_BUFFER, 0, r_ogl_state->effect_ubo);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, source->id);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    glEnable(GL_BLEND);
    glViewport(0, 0, pixel_dim.x, pixel_dim.y);
  }
}

r_hook void
r_window_submit(WM_Window window, R_Handle window_equip, R_PassList *passes)
{
  R_OGL_Window *w = (R_OGL_Window *)window_equip.u64[0];
  // Passes are in points; the window's targets are in pixels.
  Vec2F32 viewport_dim = w->last_client_rect_dim;
  Vec2S32 pixel_dim = w->last_pixel_dim;
  F32 backing_scale = w->last_backing_scale > 0 ? w->last_backing_scale : 1.f;
  // NOTE: a surface target's first pass this submit clears it; later passes
  // (e.g. a parent surface resuming after a nested child's bracket) load
  R_OGL_TouchedTargets touched = {0};
  for(R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
  {
    R_Pass *pass = &pass_n->v;
    switch(pass->kind)
    {
      default:{}break;
      
      ////////////////////////
      //- rjf: ui rendering pass
      //
      case R_PassKind_UI:
      {
        r_ogl_ui_pass(pass->params_ui, w->stage_target.fbo, viewport_dim, pixel_dim, backing_scale, &touched);
      }break;
      
      ////////////////////////
      //- rjf: effect pass (fullscreen texture->texture link of an effect chain)
      //
      case R_PassKind_Effect:
      {
        r_ogl_effect_pass(pass->params_effect, pixel_dim);
      }break;

      ////////////////////////
      //- rjf: blur rendering pass
      //
      case R_PassKind_Blur:
      {
        r_ogl_blur(pass->params_blur, backing_scale, viewport_dim, pixel_dim, &w->stage_target, &w->stage_scratch_target);
      }break;
      
      
      ////////////////////////
      //- rjf: 3d geometry rendering pass
      //
      case R_PassKind_Geo3D:
      {
        r_ogl_geo3d(pass->params_geo3d, backing_scale, &w->geo3d_target, pixel_dim, &w->stage_target, pixel_dim);
      }break;
    }
  }
}

r_hook R_Readback
r_pass_list_readback(Arena *arena, Vec2S32 size, R_PassList *passes)
{
  R_Readback result = {0};
  size.x = Max(size.x, 1);
  size.y = Max(size.y, 1);
  if(r_ogl_state != 0 &&
     r_ogl_state->shaders[R_OGL_ShaderKind_Rect] != 0 &&
     r_ogl_state->all_purpose_vao != 0)
  {
    GLuint fbo = 0;
    GLuint color_texture = 0;
    R_OGL_RenderTarget scratch = {0};
    R_OGL_RenderTarget geo = {0};
    GLuint geo_depth_texture = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &color_texture);
    if(fbo != 0 && color_texture != 0)
    {
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glBindTexture(GL_TEXTURE_2D, color_texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0);
      if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
      {
        Vec2F32 viewport_dim = v2f32((F32)size.x, (F32)size.y);
        glViewport(0, 0, size.x, size.y);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.06f, 0.06f, 0.065f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        R_OGL_RenderTarget stage = {fbo, color_texture};
        R_OGL_TouchedTargets touched = {0};
        for(R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
        {
          R_Pass *pass = &pass_n->v;
          if(pass->kind == R_PassKind_Blur && pass->params_blur != 0 &&
             r_ogl_state->shaders[R_OGL_ShaderKind_Blur] != 0)
          {
            // Blur the stage in place, as window submission does, through a
            // scratch target made the first time a blur needs one.
            if(scratch.fbo == 0)
            {
              glGenFramebuffers(1, &scratch.fbo);
              glGenTextures(1, &scratch.color_texture);
              glBindFramebufferScope(GL_FRAMEBUFFER, scratch.fbo)
                glBindTextureScope(GL_TEXTURE_2D, scratch.color_texture)
              {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scratch.color_texture, 0);
              }
            }
            r_ogl_blur(pass->params_blur, 1.f, viewport_dim, size, &stage, &scratch);
            continue;
          }
          if(pass->kind == R_PassKind_Geo3D && pass->params_geo3d != 0)
          {
            // Geo3D draws and composites as window submission does, through a
            // Geo3D target made the first time a pass needs one.
            if(geo.fbo == 0)
            {
              r_ogl_geo3d_target_alloc(&geo, &geo_depth_texture, size);
            }
            r_ogl_geo3d(pass->params_geo3d, 1.f, &geo, size, &stage, size);
            continue;
          }
          if(pass->kind == R_PassKind_UI && pass->params_ui != 0)
          {
            r_ogl_ui_pass(pass->params_ui, fbo, viewport_dim, size, 1.f, &touched);
          }
          else if(pass->kind == R_PassKind_Effect && pass->params_effect != 0)
          {
            r_ogl_effect_pass(pass->params_effect, size);
          }
        }

        U64 data_size = (U64)size.x*(U64)size.y*4;
        U8 *rgba_data = push_array_no_zero(arena, U8, data_size);
        U8 *bgra_data = push_array_no_zero(arena, U8, data_size);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
        for(S32 y = 0; y < size.y; y += 1)
        {
          U64 src_row_offset = (U64)y*(U64)size.x*4;
          U64 dst_row_offset = (U64)(size.y - 1 - y)*(U64)size.x*4;
          for(S32 x = 0; x < size.x; x += 1)
          {
            U64 src_idx = src_row_offset + (U64)x*4;
            U64 dst_idx = dst_row_offset + (U64)x*4;
            bgra_data[dst_idx + 0] = rgba_data[src_idx + 2];
            bgra_data[dst_idx + 1] = rgba_data[src_idx + 1];
            bgra_data[dst_idx + 2] = rgba_data[src_idx + 0];
            bgra_data[dst_idx + 3] = rgba_data[src_idx + 3];
          }
        }
        result.size = size;
        result.format = R_Tex2DFormat_BGRA8;
        result.data = str8(bgra_data, data_size);
      }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if(geo.fbo != 0)
    {
      glDeleteFramebuffers(1, &geo.fbo);
      glDeleteTextures(1, &geo.color_texture);
      glDeleteTextures(1, &geo_depth_texture);
    }
    if(scratch.color_texture != 0)
    {
      glDeleteTextures(1, &scratch.color_texture);
    }
    if(scratch.fbo != 0)
    {
      glDeleteFramebuffers(1, &scratch.fbo);
    }
    if(color_texture != 0)
    {
      glDeleteTextures(1, &color_texture);
    }
    if(fbo != 0)
    {
      glDeleteFramebuffers(1, &fbo);
    }
  }
  return result;
}

//- adapter identity and frames shared by other processes
// Not implemented for OpenGL: frames take the CPU path.

r_hook R_AdapterInfo
r_adapter_info(void)
{
  R_AdapterInfo info = {0};
  return info;
}

r_hook void *
r_native_device(void)
{
  return 0;
}

r_hook R_Handle
r_tex2d_open_shared(void *os_handle)
{
  R_Handle handle = {0};
  return handle;
}

r_hook R_Handle
r_timeline_alloc_shared(void)
{
  R_Handle handle = {0};
  return handle;
}

r_hook R_Handle
r_timeline_open_shared(void *os_handle)
{
  R_Handle handle = {0};
  return handle;
}

r_hook void
r_timeline_release(R_Handle timeline)
{
}

r_hook void *
r_native_timeline(R_Handle timeline)
{
  return 0;
}

r_hook B32
r_queue_wait(R_Handle timeline, U64 value)
{
  return 0;
}

r_hook B32
r_queue_signal(R_Handle timeline, U64 value)
{
  return 0;
}
