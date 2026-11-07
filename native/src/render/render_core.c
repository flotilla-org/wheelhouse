// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Generated Code

#include "generated/render.meta.c"

////////////////////////////////
//~ rjf: Helpers

internal U64
r_bytes_per_pixel_from_tex2dfmt(R_Tex2DFmt fmt)
{
  U64 num_bits = 0;
  for EachIndex(channel_idx, 4)
  {
    R_ChannelSizeKind size_kind = r_size_kind_from_tex2dfmt_channel(fmt, channel_idx);
    switch(size_kind)
    {
      default:{}break;
      case R_ChannelSizeKind_2: {num_bits += 2;}break;
      case R_ChannelSizeKind_8: {num_bits += 8;}break;
      case R_ChannelSizeKind_10:{num_bits += 10;}break;
      case R_ChannelSizeKind_11:{num_bits += 11;}break;
      case R_ChannelSizeKind_16:{num_bits += 16;}break;
      case R_ChannelSizeKind_24:{num_bits += 24;}break;
      case R_ChannelSizeKind_32:{num_bits += 32;}break;
    }
  }
  U64 num_bits_rounded = num_bits+7;
  num_bits_rounded -= num_bits_rounded%8;
  U64 num_bytes = num_bits_rounded/8;
  return num_bytes;
}

internal Mat4x4F32
r_sample_channel_map_from_tex2dfmt(R_Tex2DFmt fmt)
{
  Mat4x4F32 result =
  {
    {
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1},
    }
  };
  return result;
}

internal Mat4x4F32
r_sample_channel_map_from_tex2dformat(R_Tex2DFormat fmt)
{
  Mat4x4F32 result =
  {
    {
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1},
    }
  };
  switch(fmt)
  {
    default:{}break;
    // Coverage in every channel. Backends sample R8 as (r, 0, 0, 1): any
    // other row left set would add that 1 to the alpha, making masks opaque.
    case R_Tex2DFormat_R8:
    {
      MemoryZeroStruct(&result);
      result.v[0][0] = result.v[0][1] = result.v[0][2] = result.v[0][3] = 1.f;
    }break;
  }
  return result;
}

////////////////////////////////
//~ rjf: Basic Type Functions

internal R_Handle
r_handle_zero(void)
{
  R_Handle handle = {0};
  return handle;
}

internal B32
r_handle_match(R_Handle a, R_Handle b)
{
  return MemoryMatchStruct(&a, &b);
}

////////////////////////////////
//~ rjf: Batch Type Functions

internal R_BatchList
r_batch_list_make(U64 instance_size)
{
  R_BatchList list = {0};
  list.bytes_per_inst = instance_size;
  return list;
}

internal void *
r_batch_list_push_inst(Arena *arena, R_BatchList *list, U64 batch_inst_cap)
{
  void *inst = 0;
  {
    R_BatchNode *n = list->last;
    if(n == 0 || n->v.byte_count+list->bytes_per_inst > n->v.byte_cap)
    {
      n = push_array(arena, R_BatchNode, 1);
      n->v.byte_cap = batch_inst_cap*list->bytes_per_inst;
      n->v.v = push_array_no_zero(arena, U8, n->v.byte_cap); 
      SLLQueuePush(list->first, list->last, n);
      list->batch_count += 1;
    }
    inst = n->v.v + n->v.byte_count;
    n->v.byte_count += list->bytes_per_inst;
    list->byte_count += list->bytes_per_inst;
  }
  return inst;
}

////////////////////////////////
//~ rjf: Pass Type Functions

internal R_Pass *
r_pass_push(Arena *arena, R_PassList *list, R_PassKind kind)
{
  R_PassNode *n = push_array(arena, R_PassNode, 1);
  SLLQueuePush(list->first, list->last, n);
  list->count += 1;
  n->v.kind = kind;
  n->v.params = push_array(arena, U8, r_pass_kind_params_size_table[kind]);
  return &n->v;
}

internal R_Pass *
r_pass_from_kind(Arena *arena, R_PassList *list, R_PassKind kind)
{
  R_PassNode *n = list->last;
  if(!r_pass_kind_batch_table[kind])
  {
    n = 0;
  }
  if(n == 0 || n->v.kind != kind)
  {
    return r_pass_push(arena, list, kind);
  }
  return &n->v;
}

////////////////////////////////
//~ Core Shader Uniform Functions

internal R_CoreRectUniforms
r_core_rect_uniforms(Vec2F32 viewport_dim, R_BatchGroup2DParams *group_params, Mat3x3F32 xform,
                     R_Tex2DFormat texture_fmt, Vec2S32 texture_size, B32 surface_flip_y)
{
  R_CoreRectUniforms result = {0};
  result.viewport_size = viewport_dim;
  result.opacity = 1.f - group_params->transparency;
  result.sample_is_surface = (F32)!!group_params->tex_sample_is_surface;
  result.texture_sample_channel_map = r_sample_channel_map_from_tex2dformat(texture_fmt);
  result.texture_size = v2f32((F32)Max(texture_size.x, 1), (F32)Max(texture_size.y, 1));
  result.xform_scale = v2f32(length_2f32(v2f32(xform.v[0][0], xform.v[0][1])),
                             length_2f32(v2f32(xform.v[1][0], xform.v[1][1])));
  for(U64 row = 0; row < 3; row += 1)
  {
    result.xform[row] = v4f32(xform.v[0][row], xform.v[1][row], xform.v[2][row], 0);
  }
  result.surface_flip_y = surface_flip_y ? 1.f : 0.f;
  return result;
}

// A Gaussian of blur_size_texels taps, packed as the blur shader reads it:
// the centre weight, then pairs of taps folded into one bilinear sample each.
internal R_CoreBlurUniforms
r_core_blur_uniforms(R_PassParams_Blur *params, Vec2F32 viewport_dim, F32 blur_size_texels, B32 texcoord_flip_y)
{
  R_CoreBlurUniforms result = {0};
  result.rect = params->rect;
  result.corner_radii = v4f32(params->corner_radii[Corner_00], params->corner_radii[Corner_01],
                              params->corner_radii[Corner_10], params->corner_radii[Corner_11]);
  result.viewport_size = viewport_dim;
  result.texcoord_flip_y = texcoord_flip_y ? 1.f : 0.f;
  F32 weights[ArrayCount(result.kernel)*2] = {0};
  // One kernel slot holds the centre weight; the rest hold pairs.
  F32 blur_size = Min(blur_size_texels, ArrayCount(weights)-1);
  U64 blur_count = (U64)round_f32(blur_size);
  F32 stdev = (blur_size-1.f)/2.f;
  F32 one_over_root_2pi_stdev2 = 1/sqrt_f32(2*pi32*stdev*stdev);
  F32 euler32 = 2.718281828459045f;
  weights[0] = 1.f;
  if(stdev > 0.f)
  {
    for(U64 idx = 0; idx < blur_count; idx += 1)
    {
      F32 kernel_x = (F32)idx;
      weights[idx] = one_over_root_2pi_stdev2*pow_f32(euler32, -kernel_x*kernel_x/(2.f*stdev*stdev));
    }
  }
  if(weights[0] > 1.f)
  {
    MemoryZeroArray(weights);
    weights[0] = 1.f;
  }
  else
  {
    for(U64 idx = 1; idx < blur_count; idx += 2)
    {
      F32 w0 = weights[idx + 0];
      F32 w1 = weights[idx + 1];
      F32 w = w0 + w1;
      F32 t = w1 / w;
      result.kernel[(idx+1)/2] = v4f32(w, (F32)idx + t, 0, 0);
    }
  }
  result.kernel[0].x = weights[0];
  result.blur_count = 1 + (U32)(blur_count / 2);
  return result;
}

// Sets the rect and direction for one of the blur's two passes. The vertical
// pass samples above and below the final rect, and the bilinear neighbours
// beside it, so the horizontal pass widens its rect to write all of them;
// only the vertical pass should be clipped.
internal void
r_core_blur_pass(R_CoreBlurUniforms *uniforms, R_PassParams_Blur *params, Axis2 axis, Vec2F32 viewport_dim, Vec2S32 pixel_dim)
{
  uniforms->rect = params->rect;
  if(axis == Axis2_X)
  {
    F32 sample_radius = 0.f;
    for(U32 idx = 1; idx < uniforms->blur_count && idx < ArrayCount(uniforms->kernel); idx += 1)
    {
      sample_radius = Max(sample_radius, uniforms->kernel[idx].y);
    }
    F32 pixel_w = viewport_dim.x/(F32)Max(pixel_dim.x, 1);
    F32 pixel_h = viewport_dim.y/(F32)Max(pixel_dim.y, 1);
    uniforms->rect.x0 -= pixel_w;
    uniforms->rect.x1 += pixel_w;
    uniforms->rect.y0 -= (ceil_f32(sample_radius)+1.f)*pixel_h;
    uniforms->rect.y1 += (ceil_f32(sample_radius)+1.f)*pixel_h;
    uniforms->direction = v2f32(1.f/(F32)Max(pixel_dim.x, 1), 0);
  }
  else
  {
    uniforms->direction = v2f32(0, 1.f/(F32)Max(pixel_dim.y, 1));
  }
}
