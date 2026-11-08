// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// The rect and blur shaders, generated from native/src/shaders/*.slang.
#include "shaders/generated/core_shaders_msl.h"

@implementation R_MTL_DisplayDelegate
- (void)metalDisplayLink:(CAMetalDisplayLink *)link needsUpdate:(CAMetalDisplayLinkUpdate *)update
{
  // The link and teardown both run on the main run loop. During AppKit's
  // resize tracking loop, deliver through the guarded native resize hook;
  // posting only an outer-loop event leaves the drawable idle until mouse-up.
  if(window != 0 && window->frame_requested && update.drawable != 0)
  {
    B32 wake = window->ready_drawable == 0;
    [window->ready_drawable release];
    window->ready_drawable = [update.drawable retain];
    window->target_presentation_time = update.targetPresentationTimestamp;
    window->link_heard_us = now_time_us();
    if(wake) { mac_wm_send_frame_ready_event(window->os_window); }
  }
}
@end

internal R_Handle
r_mtl_handle_from_window(R_MTL_Window *window)
{
  R_Handle result = {(U64)window};
  return result;
}

internal R_MTL_Window *
r_mtl_window_from_handle(R_Handle handle)
{
  R_MTL_Window *result = (R_MTL_Window *)handle.u64[0];
  return result;
}

internal R_Handle
r_mtl_handle_from_tex2d(R_MTL_Tex2D *texture)
{
  R_Handle result = {(U64)texture};
  return result;
}

internal R_MTL_Tex2D *
r_mtl_tex2d_from_handle(R_Handle handle)
{
  R_MTL_Tex2D *result = (R_MTL_Tex2D *)handle.u64[0];
  return result;
}

internal R_Handle
r_mtl_handle_from_buffer(R_MTL_Buffer *buffer)
{
  R_Handle result = {(U64)buffer};
  return result;
}

internal R_MTL_Buffer *
r_mtl_buffer_from_handle(R_Handle handle)
{
  R_MTL_Buffer *result = (R_MTL_Buffer *)handle.u64[0];
  return result;
}

internal MTLPixelFormat
r_mtl_pixel_format_from_tex2d_format(R_Tex2DFormat format)
{
  MTLPixelFormat result = MTLPixelFormatInvalid;
  switch(format)
  {
    case R_Tex2DFormat_R8:     { result = MTLPixelFormatR8Unorm; } break;
    case R_Tex2DFormat_RG8:    { result = MTLPixelFormatRG8Unorm; } break;
    case R_Tex2DFormat_RGBA8:  { result = MTLPixelFormatRGBA8Unorm; } break;
    case R_Tex2DFormat_BGRA8:  { result = MTLPixelFormatBGRA8Unorm; } break;
    case R_Tex2DFormat_R16:    { result = MTLPixelFormatR16Unorm; } break;
    case R_Tex2DFormat_RGBA16: { result = MTLPixelFormatRGBA16Unorm; } break;
    case R_Tex2DFormat_R32:    { result = MTLPixelFormatR32Float; } break;
    case R_Tex2DFormat_RG32:   { result = MTLPixelFormatRG32Float; } break;
    case R_Tex2DFormat_RGBA32: { result = MTLPixelFormatRGBA32Float; } break;
    default:                   { result = MTLPixelFormatRGBA8Unorm; } break;
  }
  return result;
}

internal U64
r_mtl_bytes_per_pixel_from_tex2d_format(R_Tex2DFormat format)
{
  U64 result = 4;
  switch(format)
  {
    case R_Tex2DFormat_R8:     { result = 1; } break;
    case R_Tex2DFormat_RG8:    { result = 2; } break;
    case R_Tex2DFormat_RGBA8:  { result = 4; } break;
    case R_Tex2DFormat_BGRA8:  { result = 4; } break;
    case R_Tex2DFormat_R16:    { result = 2; } break;
    case R_Tex2DFormat_RGBA16: { result = 8; } break;
    case R_Tex2DFormat_R32:    { result = 4; } break;
    case R_Tex2DFormat_RG32:   { result = 8; } break;
    case R_Tex2DFormat_RGBA32: { result = 16; } break;
    default:                   { result = 4; } break;
  }
  return result;
}

internal NSUInteger
r_mtl_resource_options_from_kind(R_ResourceKind kind)
{
  NSUInteger result = MTLResourceStorageModeShared;
  if(kind == R_ResourceKind_Static) { result = MTLResourceStorageModePrivate; }
  return result;
}

internal U64
r_mtl_blit_texture_bytes_per_row(U64 row_bytes)
{
  U64 result = AlignPow2(row_bytes, 256);
  return result;
}

internal id<MTLBuffer>
r_mtl_static_upload_buffer_from_size(U64 size)
{
  id<MTLBuffer> result = 0;
  if(size != 0)
  {
    if(r_mtl_state->static_upload_buffer == 0 || r_mtl_state->static_upload_buffer_cap < size)
    {
      [r_mtl_state->static_upload_buffer release];
      r_mtl_state->static_upload_buffer_cap = ClampBot(MB(1), size);
      r_mtl_state->static_upload_buffer = [r_mtl_state->device newBufferWithLength:r_mtl_state->static_upload_buffer_cap options:MTLResourceStorageModeShared];
      [r_mtl_state->static_upload_buffer setLabel:@"RAD static upload buffer"];
    }
    result = r_mtl_state->static_upload_buffer;
  }
  return result;
}

internal void
r_mtl_wait_for_upload_slot(U64 slot_idx)
{
  id<MTLCommandBuffer> command_buffer = r_mtl_state->upload_buffer_command_buffers[slot_idx];
  if(command_buffer != 0)
  {
    if(command_buffer.status != MTLCommandBufferStatusCompleted &&
       command_buffer.status != MTLCommandBufferStatusError)
    {
      fprintf(stderr, "raddebugger: Metal upload slot %llu still in flight; waiting before reuse.\n", slot_idx);
      [command_buffer waitUntilCompleted];
    }
    [command_buffer release];
    r_mtl_state->upload_buffer_command_buffers[slot_idx] = 0;
  }
}

internal void
r_mtl_log_ns_error(char *context, NSError *error)
{
  if(error != 0)
  {
    fprintf(stderr, "raddebugger: Metal %s: %s\n", context, [[error localizedDescription] UTF8String]);
  }
}

internal void
r_mtl_log_command_buffer_error(id<MTLCommandBuffer> command_buffer)
{
  if(command_buffer.status == MTLCommandBufferStatusError)
  {
    NSError *error = command_buffer.error;
    if(error != 0)
    {
      fprintf(stderr, "raddebugger: Metal command buffer failure: %s\n", [[error localizedDescription] UTF8String]);
    }
    else
    {
      fprintf(stderr, "raddebugger: Metal command buffer failure.\n");
    }
  }
}

// vertex_descriptor is zero for shaders with no [[stage_in]] attributes (they
// read only uniforms and the vertex id).
internal id<MTLRenderPipelineState>
r_mtl_render_pipeline_from_libraries(id<MTLLibrary> vertex_library, NSString *vertex_name, id<MTLLibrary> fragment_library, NSString *fragment_name, MTLVertexDescriptor *vertex_descriptor, MTLPixelFormat color_pixel_format, MTLPixelFormat depth_pixel_format, B32 blend, B32 coverage_alpha)
{
  id<MTLRenderPipelineState> result = 0;
  id<MTLFunction> vertex_function = [vertex_library newFunctionWithName:vertex_name];
  id<MTLFunction> fragment_function = [fragment_library newFunctionWithName:fragment_name];
  if(vertex_function != 0 && fragment_function != 0)
  {
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = vertex_function;
    descriptor.fragmentFunction = fragment_function;
    if(vertex_descriptor != 0)
    {
      descriptor.vertexDescriptor = vertex_descriptor;
    }
    descriptor.colorAttachments[0].pixelFormat = color_pixel_format;
    descriptor.depthAttachmentPixelFormat = depth_pixel_format;
    descriptor.colorAttachments[0].blendingEnabled = blend;
    if(blend)
    {
      descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
      descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
      descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
      descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
      // NOTE: coverage_alpha accumulates source-over coverage in dst alpha, which renders
      // into transparent-cleared surfaces as premultiplied color + coverage; the default
      // replaces dst alpha (the stage's alpha is never read again, so it doesn't matter there)
      descriptor.colorAttachments[0].destinationAlphaBlendFactor = (coverage_alpha ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorZero);
    }
    NSError *error = 0;
    result = [r_mtl_state->device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    r_mtl_log_ns_error((char *)[[NSString stringWithFormat:@"pipeline %@/%@", vertex_name, fragment_name] UTF8String], error);
    [descriptor release];
  }
  else
  {
    fprintf(stderr, "raddebugger: Metal missing shader function %s/%s\n", [vertex_name UTF8String], [fragment_name UTF8String]);
  }
  [vertex_function release];
  [fragment_function release];
  return result;
}

internal id<MTLLibrary>
r_mtl_library_from_source(String8 source, char *label)
{
  NSString *source_string = [[NSString alloc] initWithBytes:source.str length:source.size encoding:NSUTF8StringEncoding];
  NSError *error = 0;
  id<MTLLibrary> result = [r_mtl_state->device newLibraryWithSource:source_string options:0 error:&error];
  r_mtl_log_ns_error(label, error);
  [source_string release];
  return result;
}

// Every pipeline but the effects', from the shaders generated from Slang.
// Each stage is its own library, with entry points vs_main and fs_main.
// - rect: reads R_Rect2DInst as eight per-instance float4 attributes from
//   R_MTL_CORE_RECT_INSTANCE_BUFFER; uniforms at buffer 0.
// - blur, composite: uniforms at buffer 0.
// - mesh: vertices from R_MTL_CORE_MESH_VERTEX_BUFFER and R_Mesh3DInst
//   instances from R_MTL_CORE_MESH_INSTANCE_BUFFER, as attributes; the
//   view-projection at vertex buffer 0, the group uniforms at fragment
//   buffer 1.
// - finalize: no uniforms.
internal void
r_mtl_build_core_pipelines(void)
{
  id<MTLLibrary> rect_vs = r_mtl_library_from_source(r_core_rect_vs_msl, "core rect vs");
  id<MTLLibrary> rect_fs = r_mtl_library_from_source(r_core_rect_fs_msl, "core rect fs");
  id<MTLLibrary> blur_vs = r_mtl_library_from_source(r_core_blur_vs_msl, "core blur vs");
  id<MTLLibrary> blur_fs = r_mtl_library_from_source(r_core_blur_fs_msl, "core blur fs");
  id<MTLLibrary> mesh_vs = r_mtl_library_from_source(r_core_mesh_vs_msl, "core mesh vs");
  id<MTLLibrary> mesh_fs = r_mtl_library_from_source(r_core_mesh_fs_msl, "core mesh fs");
  id<MTLLibrary> composite_vs = r_mtl_library_from_source(r_core_composite_vs_msl, "core composite vs");
  id<MTLLibrary> composite_fs = r_mtl_library_from_source(r_core_composite_fs_msl, "core composite fs");
  id<MTLLibrary> finalize_vs = r_mtl_library_from_source(r_core_finalize_vs_msl, "core finalize vs");
  id<MTLLibrary> finalize_fs = r_mtl_library_from_source(r_core_finalize_fs_msl, "core finalize fs");
  MTLVertexDescriptor *rect_vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
  for(U32 idx = 0; idx < 8; idx += 1)
  {
    rect_vertex_descriptor.attributes[idx].format = MTLVertexFormatFloat4;
    rect_vertex_descriptor.attributes[idx].offset = idx*sizeof(Vec4F32);
    rect_vertex_descriptor.attributes[idx].bufferIndex = R_MTL_CORE_RECT_INSTANCE_BUFFER;
  }
  rect_vertex_descriptor.layouts[R_MTL_CORE_RECT_INSTANCE_BUFFER].stride = sizeof(R_Rect2DInst);
  rect_vertex_descriptor.layouts[R_MTL_CORE_RECT_INSTANCE_BUFFER].stepFunction = MTLVertexStepFunctionPerInstance;
  rect_vertex_descriptor.layouts[R_MTL_CORE_RECT_INSTANCE_BUFFER].stepRate = 1;
  if(rect_vs != 0 && rect_fs != 0)
  {
    r_mtl_state->rect_pipeline = r_mtl_render_pipeline_from_libraries(rect_vs, @"vs_main", rect_fs, @"fs_main", rect_vertex_descriptor, MTLPixelFormatRGBA16Float, MTLPixelFormatInvalid, 1, 0);
    r_mtl_state->rect_surface_pipeline = r_mtl_render_pipeline_from_libraries(rect_vs, @"vs_main", rect_fs, @"fs_main", rect_vertex_descriptor, MTLPixelFormatRGBA16Float, MTLPixelFormatInvalid, 1, 1);
  }
  if(blur_vs != 0 && blur_fs != 0)
  {
    r_mtl_state->blur_pipeline = r_mtl_render_pipeline_from_libraries(blur_vs, @"vs_main", blur_fs, @"fs_main", 0, MTLPixelFormatRGBA16Float, MTLPixelFormatInvalid, 1, 0);
  }

  // mesh: position, normal, texcoord, rgb per vertex; four transform columns
  // and the albedo remap per instance
  MTLVertexDescriptor *mesh_vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
  {
    MTLVertexFormat vertex_formats[] = {MTLVertexFormatFloat3, MTLVertexFormatFloat3, MTLVertexFormatFloat2, MTLVertexFormatFloat3};
    U64 vertex_offsets[] = {0, 3*sizeof(F32), 6*sizeof(F32), 8*sizeof(F32)};
    for EachElement(idx, vertex_formats)
    {
      mesh_vertex_descriptor.attributes[idx].format = vertex_formats[idx];
      mesh_vertex_descriptor.attributes[idx].offset = vertex_offsets[idx];
      mesh_vertex_descriptor.attributes[idx].bufferIndex = R_MTL_CORE_MESH_VERTEX_BUFFER;
    }
    for(U32 idx = 0; idx < 5; idx += 1)
    {
      mesh_vertex_descriptor.attributes[4+idx].format = MTLVertexFormatFloat4;
      mesh_vertex_descriptor.attributes[4+idx].offset = idx*sizeof(Vec4F32);
      mesh_vertex_descriptor.attributes[4+idx].bufferIndex = R_MTL_CORE_MESH_INSTANCE_BUFFER;
    }
    mesh_vertex_descriptor.layouts[R_MTL_CORE_MESH_VERTEX_BUFFER].stride = R_CORE_MESH_VERTEX_STRIDE;
    mesh_vertex_descriptor.layouts[R_MTL_CORE_MESH_VERTEX_BUFFER].stepFunction = MTLVertexStepFunctionPerVertex;
    mesh_vertex_descriptor.layouts[R_MTL_CORE_MESH_VERTEX_BUFFER].stepRate = 1;
    mesh_vertex_descriptor.layouts[R_MTL_CORE_MESH_INSTANCE_BUFFER].stride = sizeof(R_Mesh3DInst);
    mesh_vertex_descriptor.layouts[R_MTL_CORE_MESH_INSTANCE_BUFFER].stepFunction = MTLVertexStepFunctionPerInstance;
    mesh_vertex_descriptor.layouts[R_MTL_CORE_MESH_INSTANCE_BUFFER].stepRate = 1;
  }
  if(mesh_vs != 0 && mesh_fs != 0)
  {
    r_mtl_state->mesh_pipeline = r_mtl_render_pipeline_from_libraries(mesh_vs, @"vs_main", mesh_fs, @"fs_main", mesh_vertex_descriptor, MTLPixelFormatRGBA8Unorm, MTLPixelFormatDepth32Float, 1, 0);
  }
  if(composite_vs != 0 && composite_fs != 0)
  {
    r_mtl_state->geo3d_composite_pipeline = r_mtl_render_pipeline_from_libraries(composite_vs, @"vs_main", composite_fs, @"fs_main", 0, MTLPixelFormatRGBA16Float, MTLPixelFormatInvalid, 1, 0);
    r_mtl_state->geo3d_composite_surface_pipeline = r_mtl_render_pipeline_from_libraries(composite_vs, @"vs_main", composite_fs, @"fs_main", 0, MTLPixelFormatRGBA16Float, MTLPixelFormatInvalid, 1, 1);
  }
  if(finalize_vs != 0 && finalize_fs != 0)
  {
    r_mtl_state->finalize_pipeline = r_mtl_render_pipeline_from_libraries(finalize_vs, @"vs_main", finalize_fs, @"fs_main", 0, MTLPixelFormatBGRA8Unorm_sRGB, MTLPixelFormatInvalid, 1, 0);
  }
  id<MTLLibrary> libraries[] = {rect_vs, rect_fs, blur_vs, blur_fs, mesh_vs, mesh_fs, composite_vs, composite_fs, finalize_vs, finalize_fs};
  for EachElement(idx, libraries)
  {
    [libraries[idx] release];
  }
  // Without these the first draw would fail inside Metal; stop here instead.
  if(r_mtl_state->rect_pipeline == 0 || r_mtl_state->rect_surface_pipeline == 0 || r_mtl_state->blur_pipeline == 0 ||
     r_mtl_state->mesh_pipeline == 0 || r_mtl_state->geo3d_composite_pipeline == 0 ||
     r_mtl_state->geo3d_composite_surface_pipeline == 0 || r_mtl_state->finalize_pipeline == 0)
  {
    fprintf(stderr, "raddebugger: Metal could not build the core pipelines; see the shader errors above.\n");
    abort_self(1);
  }
}

internal void
r_mtl_retire_object(id object)
{
  if(object != 0)
  {
    R_MTL_RetiredObject *node = r_mtl_state->free_retired_object;
    if(node != 0)
    {
      SLLStackPop(r_mtl_state->free_retired_object);
    }
    else
    {
      node = push_array_no_zero(r_mtl_state->arena, R_MTL_RetiredObject, 1);
    }
    node->object = object;
    SLLStackPush(r_mtl_state->retired_object[r_mtl_state->retire_idx], node);
  }
}

internal void
r_mtl_drain_retired_slot(U64 slot_idx)
{
  for(R_MTL_RetiredObject *node = r_mtl_state->retired_object[slot_idx], *next = 0; node != 0; node = next)
  {
    next = node->next;
    [node->object release];
    node->object = 0;
    SLLStackPush(r_mtl_state->free_retired_object, node);
  }
  r_mtl_state->retired_object[slot_idx] = 0;
  for(R_MTL_Tex2D *tex = r_mtl_state->retired_tex2d[slot_idx], *next = 0; tex != 0; tex = next)
  {
    next = tex->next;
    if(tex->texture != 0)
    {
      [tex->texture release];
    }
    MemoryZeroStruct(tex);
    SLLStackPush(r_mtl_state->free_tex2d, tex);
  }
  r_mtl_state->retired_tex2d[slot_idx] = 0;
  for(R_MTL_Buffer *buf = r_mtl_state->retired_buffer[slot_idx], *next = 0; buf != 0; buf = next)
  {
    next = buf->next;
    if(buf->buffer != 0)
    {
      [buf->buffer release];
    }
    MemoryZeroStruct(buf);
    SLLStackPush(r_mtl_state->free_buffer, buf);
  }
  r_mtl_state->retired_buffer[slot_idx] = 0;
}

internal void
r_mtl_window_resize_targets(R_MTL_Window *window)
{
  if(window != 0 && r_mtl_state->device != 0)
  {
    r_mtl_retire_object(window->stage_color);
    r_mtl_retire_object(window->stage_scratch_color);
    r_mtl_retire_object(window->geo3d_color);
    r_mtl_retire_object(window->geo3d_depth);

    S32 width = Max(window->drawable_size.x, 1);
    S32 height = Max(window->drawable_size.y, 1);
    MTLTextureDescriptor *stage_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                                width:width
                                                                                               height:height
                                                                                            mipmapped:NO];
    stage_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    stage_descriptor.storageMode = MTLStorageModePrivate;
    window->stage_color = [r_mtl_state->device newTextureWithDescriptor:stage_descriptor];
    window->stage_scratch_color = [r_mtl_state->device newTextureWithDescriptor:stage_descriptor];
    [window->stage_color setLabel:@"RAD stage color"];
    [window->stage_scratch_color setLabel:@"RAD stage scratch color"];

    MTLTextureDescriptor *geo3d_color_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                      width:width
                                                                                                     height:height
                                                                                                  mipmapped:NO];
    geo3d_color_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    geo3d_color_descriptor.storageMode = MTLStorageModePrivate;
    window->geo3d_color = [r_mtl_state->device newTextureWithDescriptor:geo3d_color_descriptor];
    [window->geo3d_color setLabel:@"RAD Geo3D color"];

    MTLTextureDescriptor *geo3d_depth_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                                      width:width
                                                                                                     height:height
                                                                                                  mipmapped:NO];
    geo3d_depth_descriptor.usage = MTLTextureUsageRenderTarget;
    geo3d_depth_descriptor.storageMode = MTLStorageModePrivate;
    window->geo3d_depth = [r_mtl_state->device newTextureWithDescriptor:geo3d_depth_descriptor];
    [window->geo3d_depth setLabel:@"RAD Geo3D depth"];
  }
}

internal id<MTLBuffer>
r_mtl_upload_buffer_reserve(U64 size, U64 align, U64 *out_offset, void **out_ptr)
{
  id<MTLBuffer> result = 0;
  if(size != 0)
  {
    U64 idx = r_mtl_state->upload_buffer_idx % ArrayCount(r_mtl_state->upload_buffers);
    align = ClampBot(1, align);
    U64 pos = AlignPow2(r_mtl_state->upload_buffer_pos, align);
    U64 need_cap = pos + size;
    if(r_mtl_state->upload_buffers[idx] == 0 || r_mtl_state->upload_buffer_caps[idx] < need_cap)
    {
      U64 new_cap = ClampBot(MB(1), need_cap);
      r_mtl_retire_object(r_mtl_state->upload_buffers[idx]);
      r_mtl_state->upload_buffers[idx] = [r_mtl_state->device newBufferWithLength:new_cap options:MTLResourceStorageModeShared];
      [r_mtl_state->upload_buffers[idx] setLabel:@"RAD transient upload buffer"];
      r_mtl_state->upload_buffer_caps[idx] = new_cap;
    }
    result = r_mtl_state->upload_buffers[idx];
    r_mtl_state->upload_buffer_pos = pos + size;
    *out_offset = pos;
    *out_ptr = (U8 *)[result contents] + pos;
  }
  return result;
}

internal id<MTLBuffer>
r_mtl_upload_buffer(void *data, U64 size, U64 align, U64 *out_offset)
{
  void *ptr = 0;
  id<MTLBuffer> result = r_mtl_upload_buffer_reserve(size, align, out_offset, &ptr);
  if(ptr != 0)
  {
    MemoryCopy(ptr, data, size);
  }
  return result;
}

internal F32
r_mtl_contents_scale_from_window(WM_Window window)
{
  MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
  F32 result = 1.f;
  if(mac_window != 0)
  {
    result = (F32)[mac_window->ns_window backingScaleFactor];
  }
  result = ClampBot(1.f, result);
  return result;
}

internal Vec2S32
r_mtl_drawable_size_from_window(WM_Window window)
{
  Rng2F32 rect = wm_client_rect_from_window(window);
  F32 scale = r_mtl_contents_scale_from_window(window);
  Vec2S32 result =
  {
    (S32)ceil_f32((rect.x1 - rect.x0)*scale),
    (S32)ceil_f32((rect.y1 - rect.y0)*scale),
  };
  result.x = Max(result.x, 1);
  result.y = Max(result.y, 1);
  return result;
}

internal B32
r_mtl_scissor_from_clip(Rng2F32 clip, Vec2S32 drawable_size, F32 scale, MTLScissorRect *out)
{
  S32 x0 = Clamp(0, (S32)floor_f32(clip.x0*scale), drawable_size.x);
  S32 y0 = Clamp(0, (S32)floor_f32(clip.y0*scale), drawable_size.y);
  S32 x1 = Clamp(0, (S32)ceil_f32(clip.x1*scale), drawable_size.x);
  S32 y1 = Clamp(0, (S32)ceil_f32(clip.y1*scale), drawable_size.y);
  B32 result = (x0 < x1 && y0 < y1);
  if(result)
  {
    out->x = (NSUInteger)x0;
    out->y = (NSUInteger)y0;
    out->width = (NSUInteger)(x1 - x0);
    out->height = (NSUInteger)(y1 - y0);
  }
  return result;
}

//- Encodes a Geo3D pass: meshes into geo_color/geo_depth (sized like the
// stage, drawable_size pixels), then the viewport region composited into the
// stage or the pass's surface. Pass descriptors may be a window's reusable
// ones, or zero for fresh ones.
internal void
r_mtl_encode_geo3d(id<MTLCommandBuffer> command_buffer, R_PassParams_Geo3D *params,
                   Vec2S32 drawable_size, F32 scale,
                   id<MTLTexture> geo_color, id<MTLTexture> geo_depth, id<MTLTexture> stage,
                   MTLRenderPassDescriptor *geo_pass_descriptor, MTLRenderPassDescriptor *composite_pass_descriptor)
{
  R_BatchGroup3DMap *mesh_group_map = &params->mesh_batches;

  //- unpack optional composite target; like UI passes, geo3d content
  // inside a surface bracket composites into the surface's texture
  R_MTL_Tex2D *target = r_mtl_tex2d_from_handle(params->target);
  B32 to_surface = (target != 0 && target->texture != 0);
  if(to_surface && params->preserve)
  {
    // surface content is up-to-date; skip the render & keep the texture
    return;
  }

  MTLRenderPassDescriptor *geo_pass = (geo_pass_descriptor != 0 ? geo_pass_descriptor : [MTLRenderPassDescriptor renderPassDescriptor]);
  geo_pass.colorAttachments[0].texture = geo_color;
  geo_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  geo_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  geo_pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
  geo_pass.depthAttachment.texture = geo_depth;
  geo_pass.depthAttachment.loadAction = MTLLoadActionClear;
  geo_pass.depthAttachment.storeAction = MTLStoreActionDontCare;
  geo_pass.depthAttachment.clearDepth = 1.0;
  id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:geo_pass];
  [encoder setRenderPipelineState:r_mtl_state->mesh_pipeline];
  [encoder setDepthStencilState:r_mtl_state->mesh_depth_stencil];

  Vec2F32 geo_viewport_dim = dim_2f32(params->viewport);
  MTLViewport geo_viewport =
  {
    params->viewport.x0*scale,
    params->viewport.y0*scale,
    geo_viewport_dim.x*scale,
    geo_viewport_dim.y*scale,
    0,
    1,
  };
  [encoder setViewport:geo_viewport];
  MTLScissorRect full_scissor = {0, 0, (NSUInteger)drawable_size.x, (NSUInteger)drawable_size.y};
  [encoder setScissorRect:full_scissor];

  R_CoreMeshUniforms uniforms = {0};
  uniforms.view_projection = mul_4x4f32(params->projection, params->view);
  U64 uniform_offset = 0;
  id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&uniforms, sizeof(uniforms), 256, &uniform_offset);
  [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:0];

  // groups draw in submission order - alpha blending over the depth
  // buffer needs determinism, & callers submit far-to-near
  for(R_BatchGroup3DMapNode *n = mesh_group_map->insert_first; n != 0; n = n->insert_next)
  {
    {
      R_BatchGroup3DParams *group_params = &n->params;
      R_MTL_Buffer *mesh_vertices = r_mtl_buffer_from_handle(group_params->mesh_vertices);
      R_MTL_Buffer *mesh_indices = r_mtl_buffer_from_handle(group_params->mesh_indices);
      if(group_params->mesh_geo_topology == R_GeoTopologyKind_Triangles &&
         mesh_vertices != 0 && mesh_vertices->buffer != 0 &&
         mesh_indices != 0 && mesh_indices->buffer != 0 &&
         n->batches.byte_count != 0)
      {
        // rjf: upload instance transforms
        U64 inst_count = n->batches.byte_count / n->batches.bytes_per_inst;
        U64 insts_offset = 0;
        void *insts_ptr = 0;
        id<MTLBuffer> insts_buffer = r_mtl_upload_buffer_reserve(n->batches.byte_count, 16, &insts_offset, &insts_ptr);
        U8 *insts = (U8 *)insts_ptr;
        for(R_BatchNode *batch_n = n->batches.first; batch_n != 0; batch_n = batch_n->next)
        {
          MemoryCopy(insts, batch_n->v.v, batch_n->v.byte_count);
          insts += batch_n->v.byte_count;
        }

        // rjf: bind albedo (white texture when absent)
        R_MTL_Tex2D *albedo = r_mtl_tex2d_from_handle(group_params->albedo_tex);
        B32 has_albedo = (albedo != 0 && albedo->texture != 0);
        R_CoreMeshGroupUniforms group_uniforms = {0};
        group_uniforms.has_albedo = (F32)!!has_albedo;
        group_uniforms.albedo_sample_is_surface = (F32)!!group_params->albedo_tex_sample_is_surface;
        U64 group_uniform_offset = 0;
        id<MTLBuffer> group_uniform_buffer = r_mtl_upload_buffer(&group_uniforms, sizeof(group_uniforms), 256, &group_uniform_offset);

        [encoder setVertexBuffer:mesh_vertices->buffer offset:0 atIndex:R_MTL_CORE_MESH_VERTEX_BUFFER];
        [encoder setVertexBuffer:insts_buffer offset:insts_offset atIndex:R_MTL_CORE_MESH_INSTANCE_BUFFER];
        [encoder setFragmentBuffer:group_uniform_buffer offset:group_uniform_offset atIndex:1];
        [encoder setFragmentTexture:(has_albedo ? albedo->texture : r_mtl_state->white_texture->texture) atIndex:0];
        [encoder setFragmentSamplerState:r_mtl_state->samplers[group_params->albedo_tex_sample_kind] atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh_indices->size/sizeof(U32)
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:mesh_indices->buffer
                     indexBufferOffset:0
                         instanceCount:inst_count];
      }
    }
  }
  [encoder endEncoding];

  //- composite the rendered viewport region of geo3d_color into the
  // target (the stage, or the open surface's texture)
  Vec2S32 composite_attachment_size = drawable_size;
  Vec2F32 target_origin = v2f32(0, 0);
  F32 composite_scale = scale;
  if(to_surface)
  {
    composite_attachment_size = target->size;
    target_origin = params->target_rect.p0;
    Vec2F32 target_rect_dim = dim_2f32(params->target_rect);
    if(target_rect_dim.x > 0)
    {
      composite_scale = (F32)target->size.x/target_rect_dim.x;
    }
  }

  MTLScissorRect composite_scissor = {0};
  B32 has_composite_scissor = 0;
  if(params->clip.x0 != 0 ||
     params->clip.y0 != 0 ||
     params->clip.x1 != 0 ||
     params->clip.y1 != 0)
  {
    Rng2F32 composite_clip = params->clip;
    if(to_surface)
    {
      composite_clip = shift_2f32(composite_clip, v2f32(-target_origin.x, -target_origin.y));
    }
    has_composite_scissor = r_mtl_scissor_from_clip(composite_clip, composite_attachment_size, composite_scale, &composite_scissor);
  }
  else
  {
    composite_scissor = (MTLScissorRect){0, 0, (NSUInteger)composite_attachment_size.x, (NSUInteger)composite_attachment_size.y};
    has_composite_scissor = 1;
  }
  if(has_composite_scissor)
  {
    // dst: the pass viewport in the target's clip space; src: the
    // same region of the window-sized geo3d color texture
    R_CoreCompositeUniforms composite_uniforms = r_core_composite_uniforms(params->viewport, target_origin, composite_scale, composite_attachment_size,
                                                                           scale, drawable_size, 0);
    U64 composite_uniform_offset = 0;
    id<MTLBuffer> composite_uniform_buffer = r_mtl_upload_buffer(&composite_uniforms, sizeof(composite_uniforms), 256, &composite_uniform_offset);

    MTLRenderPassDescriptor *composite_pass = (composite_pass_descriptor != 0 ? composite_pass_descriptor : [MTLRenderPassDescriptor renderPassDescriptor]);
    composite_pass.colorAttachments[0].texture = (to_surface ? target->texture : stage);
    composite_pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    composite_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    encoder = [command_buffer renderCommandEncoderWithDescriptor:composite_pass];
    [encoder setRenderPipelineState:(to_surface ? r_mtl_state->geo3d_composite_surface_pipeline : r_mtl_state->geo3d_composite_pipeline)];
    [encoder setScissorRect:composite_scissor];
    [encoder setVertexBuffer:composite_uniform_buffer offset:composite_uniform_offset atIndex:0];
    [encoder setFragmentTexture:geo_color atIndex:0];
    [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Linear] atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
  }
}

// Blurs params->rect of stage in place, through scratch: the horizontal pass
// writes scratch, the vertical pass writes back to stage. viewport_dim is in
// points (the units of params->rect and params->clip), drawable_size the
// textures' size in pixels. pass_descriptor may be a window's reusable one,
// or zero for a fresh one.
internal void
r_mtl_encode_blur(id<MTLCommandBuffer> command_buffer, R_PassParams_Blur *params,
                  Vec2F32 viewport_dim, Vec2S32 drawable_size, F32 scale,
                  id<MTLTexture> stage, id<MTLTexture> scratch,
                  MTLRenderPassDescriptor *pass_descriptor)
{
  // params->blur_size is in points, like the rect; the kernel steps in texels
  // of the drawable, so scale it as OpenGL does (ADR 0015: points x backing scale).
  R_CoreBlurUniforms uniforms = r_core_blur_uniforms(params, viewport_dim, params->blur_size*scale, 0);
  MTLScissorRect scissor = {0};
  B32 has_scissor = 0;
  if(params->clip.x0 != 0 ||
     params->clip.y0 != 0 ||
     params->clip.x1 != 0 ||
     params->clip.y1 != 0)
  {
    has_scissor = r_mtl_scissor_from_clip(params->clip, drawable_size, scale, &scissor);
  }
  else
  {
    scissor = (MTLScissorRect){0, 0, (NSUInteger)drawable_size.x, (NSUInteger)drawable_size.y};
    has_scissor = 1;
  }
  if(has_scissor)
  {
    for(Axis2 axis = (Axis2)0; axis < Axis2_COUNT; axis = (Axis2)(axis + 1))
    {
      id<MTLTexture> src = (axis == Axis2_X ? stage : scratch);
      id<MTLTexture> dst = (axis == Axis2_X ? scratch : stage);
      // The horizontal pass covers the vertical pass's whole input and isn't
      // clipped; only the vertical pass is clipped and masked.
      r_core_blur_pass(&uniforms, params, axis, viewport_dim, drawable_size);
      MTLScissorRect pass_scissor = scissor;
      if(axis == Axis2_X)
      {
        pass_scissor = (MTLScissorRect){0, 0, (NSUInteger)drawable_size.x, (NSUInteger)drawable_size.y};
      }
      U64 uniform_offset = 0;
      id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&uniforms, sizeof(uniforms), 256, &uniform_offset);

      MTLRenderPassDescriptor *blur_pass = (pass_descriptor != 0 ? pass_descriptor : [MTLRenderPassDescriptor renderPassDescriptor]);
      blur_pass.colorAttachments[0].texture = dst;
      blur_pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
      blur_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:blur_pass];
      [encoder setRenderPipelineState:r_mtl_state->blur_pipeline];
      [encoder setScissorRect:pass_scissor];
      [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:0];
      [encoder setFragmentBuffer:uniform_buffer offset:uniform_offset atIndex:0];
      [encoder setFragmentTexture:src atIndex:0];
      [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Linear] atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      [encoder endEncoding];
    }
  }
}

r_hook void
r_init(CmdLine *cmdln)
{
  Arena *arena = arena_alloc();
  r_mtl_state = push_array(arena, R_MTL_State, 1);
  r_mtl_state->arena = arena;
  r_mtl_state->legacy_pacing = cmd_line_has_flag(cmdln, str8_lit("legacy_presentation"));
  r_mtl_state->presentation_log_fd = -1;
  String8 presentation_log = cmd_line_string(cmdln, str8_lit("metal_presentation_log"));
  if(presentation_log.size != 0)
  {
    char *path = (char *)push_str8f(arena, "%S", presentation_log).str;
    int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND|O_CLOEXEC, 0600);
    r_mtl_state->presentation_log_fd = fd;
    if(fd >= 0)
    {
      char header[] = "window,serial,submit_seconds,target_seconds,presented_seconds,display_driven\n";
      (void)write(fd, header, sizeof(header)-1);
    }
    else { fprintf(stderr, "Metal presentation trace could not open %s\n", path); }
  }
  r_mtl_state->device_rw_mutex = rw_mutex_alloc();
  r_mtl_state->device = MTLCreateSystemDefaultDevice();
  if(r_mtl_state->device != 0)
  {
    r_mtl_state->command_queue = [r_mtl_state->device newCommandQueue];
    r_mtl_state->static_upload_buffer_cap = MB(1);
    r_mtl_state->static_upload_buffer = [r_mtl_state->device newBufferWithLength:r_mtl_state->static_upload_buffer_cap options:MTLResourceStorageModeShared];
    [r_mtl_state->static_upload_buffer setLabel:@"RAD static upload buffer"];
    r_mtl_state->command_buffer_completion_handler = [^(id<MTLCommandBuffer> completed_buffer)
    {
      r_mtl_log_command_buffer_error(completed_buffer);
    } copy];

    r_mtl_build_core_pipelines();

    MTLDepthStencilDescriptor *depth_stencil_descriptor = [MTLDepthStencilDescriptor new];
    depth_stencil_descriptor.depthCompareFunction = MTLCompareFunctionLess;
    depth_stencil_descriptor.depthWriteEnabled = YES;
    r_mtl_state->mesh_depth_stencil = [r_mtl_state->device newDepthStencilStateWithDescriptor:depth_stencil_descriptor];
    [depth_stencil_descriptor release];

    for EachIndex(idx, R_Tex2DSampleKind_COUNT)
    {
      MTLSamplerDescriptor *sampler_descriptor = [MTLSamplerDescriptor new];
      // Linear sampling clamps to edge so scaled images (e.g. Kitty terminal
      // images) don't wrap the opposite edge in at their borders; Nearest keeps
      // the inherited Repeat. Nothing in the renderer tiles, so this only
      // affects sampling at a texture's outer edge.
      MTLSamplerAddressMode address_mode = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat);
      sampler_descriptor.sAddressMode = address_mode;
      sampler_descriptor.tAddressMode = address_mode;
      sampler_descriptor.minFilter = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest);
      sampler_descriptor.magFilter = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest);
      r_mtl_state->samplers[idx] = [r_mtl_state->device newSamplerStateWithDescriptor:sampler_descriptor];
      [sampler_descriptor release];
    }

    U32 white_pixels[] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    r_mtl_state->white_texture = r_mtl_tex2d_from_handle(r_tex2d_alloc(R_ResourceKind_Static, v2s32(2, 2), R_Tex2DFormat_RGBA8, white_pixels));
  }
}

// Gives a window a display link, paused, in place of any it had; a stalled
// window's caller unpauses the new one.
internal void
r_mtl_window_link(R_MTL_Window *window)
{
  if(@available(macOS 14.0, *))
  {
    [window->display_link invalidate];
    [window->display_link release];
    [window->ready_drawable release];
    window->ready_drawable = 0;
    window->display_link = [[CAMetalDisplayLink alloc] initWithMetalLayer:window->layer];
    window->display_link.delegate = window->display_delegate;
    window->display_link.preferredFrameLatency = 1.f;
    if(window->display_hz > 0) { window->display_link.preferredFrameRateRange = CAFrameRateRangeMake(window->display_hz, window->display_hz, window->display_hz); }
    window->display_link.paused = YES;
    [window->display_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    window->link_heard_us = now_time_us();
  }
}

r_hook R_Handle
r_window_equip(WM_Window window)
{
  R_MTL_Window *result = 0;
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    if(r_mtl_state->device != 0)
    {
      result = r_mtl_state->free_window;
      if(result != 0)
      {
        SLLStackPop(r_mtl_state->free_window);
      }
      else
      {
        result = push_array_no_zero(r_mtl_state->arena, R_MTL_Window, 1);
      }
      MemoryZeroStruct(result);
      result->os_window = window;

      MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
      NSView *content_view = [mac_window->ns_window contentView];
      result->layer = [CAMetalLayer layer];
      result->layer.device = r_mtl_state->device;
      result->layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
      result->layer.framebufferOnly = YES;
      result->stage_clear_pass = [MTLRenderPassDescriptor new];
      result->stage_pass = [MTLRenderPassDescriptor new];
      result->blur_pass = [MTLRenderPassDescriptor new];
      result->geo_pass = [MTLRenderPassDescriptor new];
      result->composite_pass = [MTLRenderPassDescriptor new];
      result->final_pass = [MTLRenderPassDescriptor new];
      result->contents_scale = r_mtl_contents_scale_from_window(window);
      result->layer.contentsScale = result->contents_scale;
      result->drawable_size = r_mtl_drawable_size_from_window(window);
      result->layer.drawableSize = CGSizeMake(result->drawable_size.x, result->drawable_size.y);
      r_mtl_window_resize_targets(result);
      [content_view setWantsLayer:YES];
      [content_view setLayer:result->layer];
      if(@available(macOS 14.0, *))
      {
        if(!r_mtl_state->legacy_pacing)
        {
          result->display_delegate = [R_MTL_DisplayDelegate new];
          result->display_delegate->window = result;
          r_mtl_window_link(result);
        }
      }
    }
  }
  return r_mtl_handle_from_window(result);
}

r_hook void
r_window_unequip(WM_Window window, R_Handle window_equip)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
    if(mtl_window != 0)
    {
      [mtl_window->display_link invalidate];
      [mtl_window->display_link release];
      if(mtl_window->display_delegate != 0) { mtl_window->display_delegate->window = 0; }
      [mtl_window->display_delegate release];
      [mtl_window->ready_drawable release];
      mtl_window->display_link = 0;
      mtl_window->display_delegate = 0;
      mtl_window->ready_drawable = 0;
      MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
      NSView *content_view = [mac_window->ns_window contentView];
      if([content_view layer] == mtl_window->layer)
      {
        [content_view setLayer:0];
      }
      r_mtl_retire_object(mtl_window->stage_color);
      r_mtl_retire_object(mtl_window->stage_scratch_color);
      r_mtl_retire_object(mtl_window->geo3d_color);
      r_mtl_retire_object(mtl_window->geo3d_depth);
      [mtl_window->stage_clear_pass release];
      [mtl_window->stage_pass release];
      [mtl_window->blur_pass release];
      [mtl_window->geo_pass release];
      [mtl_window->composite_pass release];
      [mtl_window->final_pass release];
      mtl_window->layer = 0;
      mtl_window->stage_color = 0;
      mtl_window->stage_scratch_color = 0;
      mtl_window->geo3d_color = 0;
      mtl_window->geo3d_depth = 0;
      mtl_window->stage_clear_pass = 0;
      mtl_window->stage_pass = 0;
      mtl_window->blur_pass = 0;
      mtl_window->geo_pass = 0;
      mtl_window->composite_pass = 0;
      mtl_window->final_pass = 0;
      SLLStackPush(r_mtl_state->free_window, mtl_window);
    }
  }
}

r_hook R_Handle
r_tex2d_alloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void *data)
{
  R_Handle result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_state->free_tex2d;
    if(texture != 0)
    {
      SLLStackPop(r_mtl_state->free_tex2d);
    }
    else
    {
      texture = push_array_no_zero(r_mtl_state->arena, R_MTL_Tex2D, 1);
    }
    MemoryZeroStruct(texture);
    texture->kind = kind;
    texture->format = format;
    texture->size = size;

    MTLPixelFormat pixel_format = r_mtl_pixel_format_from_tex2d_format(format);
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                                                         width:Max(size.x, 1)
                                                                                        height:Max(size.y, 1)
                                                                                     mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = (kind == R_ResourceKind_Static ? MTLStorageModePrivate : MTLStorageModeShared);
    texture->texture = [r_mtl_state->device newTextureWithDescriptor:descriptor];
    if(data != 0 && texture->texture != 0)
    {
      U64 bytes_per_pixel = r_mtl_bytes_per_pixel_from_tex2d_format(texture->format);
      if(kind == R_ResourceKind_Static)
      {
        U64 row_bytes = size.x*bytes_per_pixel;
        U64 blit_row_bytes = r_mtl_blit_texture_bytes_per_row(row_bytes);
        U64 staging_size = blit_row_bytes*size.y;
        id<MTLBuffer> staging_buffer = r_mtl_static_upload_buffer_from_size(staging_size);
        if(staging_buffer != 0)
        {
          U8 *dst = (U8 *)[staging_buffer contents];
          U8 *src = (U8 *)data;
          for(S64 y = 0; y < size.y; y += 1)
          {
            MemoryCopy(dst + y*blit_row_bytes, src + y*row_bytes, row_bytes);
          }
          id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
          id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
          MTLSize blit_size = MTLSizeMake(size.x, size.y, 1);
          MTLOrigin origin = MTLOriginMake(0, 0, 0);
          [encoder copyFromBuffer:staging_buffer
                      sourceOffset:0
                 sourceBytesPerRow:blit_row_bytes
               sourceBytesPerImage:staging_size
                        sourceSize:blit_size
                         toTexture:texture->texture
                  destinationSlice:0
                  destinationLevel:0
                 destinationOrigin:origin];
          [encoder endEncoding];
          [command_buffer commit];
          [command_buffer waitUntilCompleted];
          r_mtl_log_command_buffer_error(command_buffer);
        }
      }
      else
      {
        MTLRegion region = MTLRegionMake2D(0, 0, size.x, size.y);
        [texture->texture replaceRegion:region
                             mipmapLevel:0
                               withBytes:data
                             bytesPerRow:size.x*bytes_per_pixel];
      }
    }
    result = r_mtl_handle_from_tex2d(texture);
  }
  return result;
}

r_hook R_Handle
r_tex2d_alloc_render_target(Vec2S32 size)
{
  R_Handle result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_state->free_tex2d;
    if(texture != 0)
    {
      SLLStackPop(r_mtl_state->free_tex2d);
    }
    else
    {
      texture = push_array_no_zero(r_mtl_state->arena, R_MTL_Tex2D, 1);
    }
    MemoryZeroStruct(texture);
    texture->kind = R_ResourceKind_Static;
    // NOTE: render targets share the stage's pixel format (RGBA16Float), which has no
    // R_Tex2DFormat; RGBA16 gives the identity sample channel map & correct bytes-per-pixel
    texture->format = R_Tex2DFormat_RGBA16;
    texture->size = size;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                         width:Max(size.x, 1)
                                                                                        height:Max(size.y, 1)
                                                                                     mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    texture->texture = [r_mtl_state->device newTextureWithDescriptor:descriptor];
    result = r_mtl_handle_from_tex2d(texture);
  }
  return result;
}

r_hook R_Handle
r_effect_alloc(String8 name, R_EffectSources *sources)
{
  R_Handle result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    NSString *vs_src = [[NSString alloc] initWithBytes:sources->msl_vs.str length:sources->msl_vs.size encoding:NSUTF8StringEncoding];
    NSString *fs_src = [[NSString alloc] initWithBytes:sources->msl_fs.str length:sources->msl_fs.size encoding:NSUTF8StringEncoding];
    NSError *error = 0;
    id<MTLLibrary> vs_lib = [r_mtl_state->device newLibraryWithSource:vs_src options:0 error:&error];
    r_mtl_log_ns_error((char *)[[NSString stringWithFormat:@"effect %.*s vs", (int)name.size, name.str] UTF8String], error);
    error = 0;
    id<MTLLibrary> fs_lib = [r_mtl_state->device newLibraryWithSource:fs_src options:0 error:&error];
    r_mtl_log_ns_error((char *)[[NSString stringWithFormat:@"effect %.*s fs", (int)name.size, name.str] UTF8String], error);
    if(vs_lib != 0 && fs_lib != 0)
    {
      id<MTLFunction> vs_function = [vs_lib newFunctionWithName:@"vs_main"];
      id<MTLFunction> fs_function = [fs_lib newFunctionWithName:@"fs_main"];
      if(vs_function != 0 && fs_function != 0)
      {
        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = vs_function;
        descriptor.fragmentFunction = fs_function;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        // premultiplied texture->texture: the fullscreen pass replaces every pixel
        descriptor.colorAttachments[0].blendingEnabled = NO;
        error = 0;
        id<MTLRenderPipelineState> pipeline = [r_mtl_state->device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        r_mtl_log_ns_error((char *)[[NSString stringWithFormat:@"effect %.*s pipeline", (int)name.size, name.str] UTF8String], error);
        if(pipeline != 0)
        {
          R_MTL_Effect *effect = push_array(r_mtl_state->arena, R_MTL_Effect, 1);
          effect->pipeline = pipeline;
          result.u64[0] = (U64)effect;
        }
        [descriptor release];
      }
      [vs_function release];
      [fs_function release];
    }
    [vs_lib release];
    [fs_lib release];
    [vs_src release];
    [fs_src release];
  }
  return result;
}

r_hook void
r_tex2d_release(R_Handle handle)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      SLLStackPush(r_mtl_state->retired_tex2d[r_mtl_state->retire_idx], texture);
    }
  }
}

r_hook R_ResourceKind
r_kind_from_tex2d(R_Handle handle)
{
  R_ResourceKind result = R_ResourceKind_Static;
  MutexScopeR(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      result = texture->kind;
    }
  }
  return result;
}

r_hook Vec2S32
r_size_from_tex2d(R_Handle handle)
{
  Vec2S32 result = {1, 1};
  MutexScopeR(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      result = texture->size;
    }
  }
  return result;
}

r_hook R_Tex2DFormat
r_format_from_tex2d(R_Handle handle)
{
  R_Tex2DFormat result = R_Tex2DFormat_RGBA8;
  MutexScopeR(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      result = texture->format;
    }
  }
  return result;
}

r_hook void
r_fill_tex2d_region(R_Handle handle, Rng2S32 subrect, void *data)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0 && texture->texture != 0 && texture->kind == R_ResourceKind_Dynamic && data != 0)
    {
      U64 bytes_per_pixel = r_mtl_bytes_per_pixel_from_tex2d_format(texture->format);
      MTLRegion region = MTLRegionMake2D(subrect.x0, subrect.y0, subrect.x1 - subrect.x0, subrect.y1 - subrect.y0);
      [texture->texture replaceRegion:region
                           mipmapLevel:0
                             withBytes:data
                           bytesPerRow:(subrect.x1 - subrect.x0)*bytes_per_pixel];
    }
  }
}

r_hook R_Handle
r_buffer_alloc(R_ResourceKind kind, U64 size, void *data)
{
  R_Handle result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Buffer *buffer = r_mtl_state->free_buffer;
    if(buffer != 0)
    {
      SLLStackPop(r_mtl_state->free_buffer);
    }
    else
    {
      buffer = push_array_no_zero(r_mtl_state->arena, R_MTL_Buffer, 1);
    }
    MemoryZeroStruct(buffer);
    buffer->kind = kind;
    buffer->size = size;
    NSUInteger options = r_mtl_resource_options_from_kind(kind);
    if(kind == R_ResourceKind_Static && data != 0)
    {
      buffer->buffer = [r_mtl_state->device newBufferWithLength:size options:options];
      id<MTLBuffer> staging_buffer = r_mtl_static_upload_buffer_from_size(size);
      if(buffer->buffer != 0 && staging_buffer != 0)
      {
        MemoryCopy([staging_buffer contents], data, size);
        id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
        [encoder copyFromBuffer:staging_buffer
                   sourceOffset:0
                       toBuffer:buffer->buffer
              destinationOffset:0
                           size:size];
        [encoder endEncoding];
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        r_mtl_log_command_buffer_error(command_buffer);
      }
    }
    else if(data != 0)
    {
      buffer->buffer = [r_mtl_state->device newBufferWithBytes:data length:size options:options];
    }
    else
    {
      buffer->buffer = [r_mtl_state->device newBufferWithLength:size options:options];
    }
    result = r_mtl_handle_from_buffer(buffer);
  }
  return result;
}

r_hook void
r_buffer_release(R_Handle handle)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Buffer *buffer = r_mtl_buffer_from_handle(handle);
    if(buffer != 0)
    {
      SLLStackPush(r_mtl_state->retired_buffer[r_mtl_state->retire_idx], buffer);
    }
  }
}

// One wake is outstanding at a time. It wakes the shell's event pump once the
// latest pending wait has run out, and follows that deadline as it moves.
internal void
r_mtl_display_wait_wake_after(U64 delay_us)
{
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (S64)delay_us*1000), dispatch_get_main_queue(), ^{
    U64 now = now_time_us();
    if(now < r_mtl_state->display_wait_deadline_us) { r_mtl_display_wait_wake_after(r_mtl_state->display_wait_deadline_us-now); return; }
    r_mtl_state->display_wait_wake_armed = 0;
    wm_send_wakeup_event();
  });
}

// Once a display link is attached, every drawable must come from it, including
// the first frame. The shell shows new windows while that first frame is pending.
internal B32
r_mtl_window_uses_display_link(WM_Window window, R_MTL_Window *mtl_window)
{
  return mtl_window != 0 && mtl_window->display_link != 0;
}

r_hook void
r_window_request_frame(WM_Window window, R_Handle window_equip, B32 requested)
{
  R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
  if(mtl_window != 0 && mtl_window->display_link != 0)
  {
    MAC_WM_Window *native = mac_wm_window_from_handle(window);
    // Apply geometry before acquiring the next drawable. Input/resize can have
    // arrived after an earlier opportunity; discard that obsolete drawable.
    F32 scale = r_mtl_contents_scale_from_window(window);
    Vec2S32 size = r_mtl_drawable_size_from_window(window);
    if(scale != mtl_window->contents_scale || size.x != mtl_window->drawable_size.x || size.y != mtl_window->drawable_size.y)
    {
      [mtl_window->ready_drawable release];
      mtl_window->ready_drawable = 0;
      r_window_begin_frame(window, window_equip);
    }
    S32 hz = (S32)[[native->ns_window screen] maximumFramesPerSecond];
    if(hz > 0 && hz != mtl_window->display_hz)
    {
      mtl_window->display_hz = hz;
      mtl_window->display_link.preferredFrameRateRange = CAFrameRateRangeMake(hz, hz, hz);
    }
    mtl_window->frame_requested = requested;
    B32 was_paused = mtl_window->display_link.paused;
    mtl_window->display_link.paused = !requested ||
                                     ![native->ns_window isVisible] || [native->ns_window isMiniaturized];
    U64 now = now_time_us();
    if(was_paused && !mtl_window->display_link.paused) { mtl_window->link_heard_us = now; }
    // A link can stop for good while AppKit still runs the window (after the
    // screen locks, say), whatever occlusion it reports; a new one fires. A
    // covered window, which may legitimately get no drawables, is relinked
    // each interval too: cheap, and its occlusion state can't be trusted.
    else if(!mtl_window->display_link.paused && mtl_window->ready_drawable == 0 &&
            now - mtl_window->link_heard_us >= R_MTL_DISPLAY_RELINK_INTERVAL_US)
    {
      r_mtl_window_link(mtl_window);
      mtl_window->display_link.paused = NO;
    }
    if(mtl_window->display_link.paused)
    {
      [mtl_window->ready_drawable release];
      mtl_window->ready_drawable = 0;
    }
    if(!requested || mtl_window->display_link.paused || mtl_window->ready_drawable != 0)
    {
      mtl_window->pending_since_us = 0;
    }
    else if(mtl_window->pending_since_us == 0)
    {
      mtl_window->pending_since_us = now_time_us();
      // The shell waits only for frames r_window_frame_pending reports, and a
      // display link that stops firing for a visible window would otherwise
      // stall it until input arrives. Wake the pump when the wait runs out.
      r_mtl_state->display_wait_deadline_us = mtl_window->pending_since_us + R_MTL_DISPLAY_WAIT_LIMIT_US;
      if(!r_mtl_state->display_wait_wake_armed)
      {
        r_mtl_state->display_wait_wake_armed = 1;
        r_mtl_display_wait_wake_after(R_MTL_DISPLAY_WAIT_LIMIT_US);
      }
    }
  }
}

r_hook B32
r_window_frame_ready(WM_Window window, R_Handle window_equip)
{
  R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
  MAC_WM_Window *native = mac_wm_window_from_handle(window);
  if(mtl_window != 0 && mtl_window->display_link != 0 &&
     (![native->ns_window isVisible] || [native->ns_window isMiniaturized])) { return 0; }
  return !r_mtl_window_uses_display_link(window, mtl_window) || mtl_window->ready_drawable != 0;
}

r_hook B32
r_window_frame_pending(WM_Window window, R_Handle window_equip)
{
  R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
  if(!r_mtl_window_uses_display_link(window, mtl_window)) { return 0; }
  // AppKit may stop display callbacks for a window with no visible part
  // (covered, or its display asleep). Nothing it builds can be seen, so
  // waiting for that window would only stall input and content.
  NSWindow *ns_window = mac_wm_window_from_handle(window)->ns_window;
  B32 pending = mtl_window->frame_requested && !mtl_window->display_link.paused && mtl_window->ready_drawable == 0 &&
                ([ns_window occlusionState] & NSWindowOcclusionStateVisible) && mtl_window->pending_since_us != 0;
  // A display link can also stop firing for a window AppKit still reports
  // visible. Past the limit this wait ends: the frame builds without a
  // drawable, and the next one's wait starts when it is requested, so builds
  // are paced by the limit rather than spinning.
  if(pending && now_time_us() - mtl_window->pending_since_us >= R_MTL_DISPLAY_WAIT_LIMIT_US)
  {
    mtl_window->pending_since_us = 0;
    pending = 0;
  }
  return pending;
}

r_hook void
r_begin_frame(void)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    r_mtl_state->upload_buffer_idx += 1;
    r_mtl_state->retire_idx = r_mtl_state->upload_buffer_idx % ArrayCount(r_mtl_state->retired_object);
    r_mtl_wait_for_upload_slot(r_mtl_state->retire_idx);
    r_mtl_drain_retired_slot(r_mtl_state->retire_idx);
    r_mtl_state->upload_buffer_pos = 0;
  }
}

r_hook void
r_end_frame(void)
{
}

r_hook void
r_window_begin_frame(WM_Window window, R_Handle window_equip)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
    if(mtl_window != 0)
    {
      F32 contents_scale = r_mtl_contents_scale_from_window(window);
      Vec2S32 drawable_size = r_mtl_drawable_size_from_window(window);
      if(contents_scale != mtl_window->contents_scale ||
         drawable_size.x != mtl_window->drawable_size.x ||
         drawable_size.y != mtl_window->drawable_size.y)
      {
        mtl_window->contents_scale = contents_scale;
        mtl_window->drawable_size = drawable_size;
        mtl_window->layer.contentsScale = contents_scale;
        mtl_window->layer.drawableSize = CGSizeMake(drawable_size.x, drawable_size.y);
        r_mtl_window_resize_targets(mtl_window);
      }
    }
  }
}

r_hook void
r_window_end_frame(WM_Window window, R_Handle window_equip)
{
}

//- render passes shared by window submission and readback, so the
// diagnostics read back what windows draw

// Encodes a UI pass into its surface target, or else into stage_color. Passes
// are in points (viewport_dim); the stage is drawable_size pixels. A surface
// target's first pass in a submission clears it; touched remembers which.
internal void
r_mtl_encode_ui_pass(id<MTLCommandBuffer> command_buffer, MTLRenderPassDescriptor *stage_pass, R_PassParams_UI *params,
                     id<MTLTexture> stage_color, Vec2F32 viewport_dim, Vec2S32 drawable_size, F32 scale,
                     R_MTL_TouchedTargets *touched)
{
  if(r_mtl_state->rect_pipeline == 0)
  {
    return;
  }

  //- unpack optional render target; surface passes draw into their own
  // texture (cleared to transparent), in coordinates relative to target_rect
  R_MTL_Tex2D *target = r_mtl_tex2d_from_handle(params->target);
  B32 to_surface = (target != 0 && target->texture != 0);
  if(to_surface && params->preserve)
  {
    // surface content is up-to-date; skip the render & keep the texture
    return;
  }
  Vec2F32 pass_viewport_dim = viewport_dim;
  Vec2S32 pass_attachment_size = drawable_size;
  Vec2F32 target_origin = v2f32(0, 0);
  F32 pass_scale = scale;
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
      if(touched->v[idx] == target)
      {
        target_first_touch = 0;
        break;
      }
    }
    if(target_first_touch && touched->count < ArrayCount(touched->v))
    {
      touched->v[touched->count] = target;
      touched->count += 1;
    }
  }

  stage_pass.colorAttachments[0].texture = (to_surface ? target->texture : stage_color);
  stage_pass.colorAttachments[0].loadAction = ((to_surface && target_first_touch) ? MTLLoadActionClear : MTLLoadActionLoad);
  stage_pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
  stage_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:stage_pass];
  [encoder setRenderPipelineState:(to_surface ? r_mtl_state->rect_surface_pipeline : r_mtl_state->rect_pipeline)];

  for(R_BatchGroup2DNode *group_n = params->rects.first; group_n != 0; group_n = group_n->next)
  {
    R_BatchList *batches = &group_n->batches;
    R_BatchGroup2DParams *group_params = &group_n->params;
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(group_params->tex);
    if(texture == 0 || texture->texture == 0)
    {
      texture = r_mtl_state->white_texture;
    }
    if(texture != 0 && texture->texture != 0 && batches->byte_count != 0)
    {
      U64 inst_count = batches->byte_count / batches->bytes_per_inst;
      U64 insts_offset = 0;
      void *insts_ptr = 0;
      id<MTLBuffer> insts_buffer = r_mtl_upload_buffer_reserve(batches->byte_count, 16, &insts_offset, &insts_ptr);
      U8 *insts = (U8 *)insts_ptr;
      for(R_BatchNode *batch_n = batches->first; batch_n != 0; batch_n = batch_n->next)
      {
        MemoryCopy(insts, batch_n->v.v, batch_n->v.byte_count);
        insts += batch_n->v.byte_count;
      }
      Mat3x3F32 group_xform = group_params->xform;
      if(to_surface)
      {
        group_xform = mul_3x3f32(make_translate_3x3f32(v2f32(-target_origin.x, -target_origin.y)), group_xform);
      }
      R_CoreRectUniforms group_uniforms = r_core_rect_uniforms(pass_viewport_dim, group_params, group_xform, texture->format, texture->size, 0);
      U64 uniform_offset = 0;
      id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&group_uniforms, sizeof(group_uniforms), 256, &uniform_offset);
      if(group_params->clip.x0 != 0 ||
         group_params->clip.y0 != 0 ||
         group_params->clip.x1 != 0 ||
         group_params->clip.y1 != 0)
      {
        Rng2F32 clip = group_params->clip;
        if(to_surface)
        {
          clip = shift_2f32(clip, v2f32(-target_origin.x, -target_origin.y));
        }
        MTLScissorRect scissor = {0};
        if(!r_mtl_scissor_from_clip(clip, pass_attachment_size, pass_scale, &scissor))
        {
          continue;
        }
        else
        {
          [encoder setScissorRect:scissor];
        }
      }
      else
      {
        MTLScissorRect scissor = {0, 0, (NSUInteger)pass_attachment_size.x, (NSUInteger)pass_attachment_size.y};
        [encoder setScissorRect:scissor];
      }
      [encoder setVertexBuffer:insts_buffer offset:insts_offset atIndex:R_MTL_CORE_RECT_INSTANCE_BUFFER];
      [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:0];
      [encoder setFragmentBuffer:uniform_buffer offset:uniform_offset atIndex:0];
      [encoder setFragmentTexture:texture->texture atIndex:0];
      [encoder setFragmentSamplerState:r_mtl_state->samplers[group_params->tex_sample_kind] atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:inst_count];
    }
  }
  [encoder endEncoding];
}

// Encodes one effect: a fullscreen triangle from params->source into
// params->target.
internal void
r_mtl_encode_effect_pass(id<MTLCommandBuffer> command_buffer, MTLRenderPassDescriptor *stage_pass, R_PassParams_Effect *params)
{
  R_MTL_Effect *effect = (R_MTL_Effect *)params->effect.u64[0];
  R_MTL_Tex2D *source = r_mtl_tex2d_from_handle(params->source);
  R_MTL_Tex2D *target = r_mtl_tex2d_from_handle(params->target);
  if(effect != 0 && effect->pipeline != 0 &&
     source != 0 && source->texture != 0 &&
     target != 0 && target->texture != 0)
  {
    R_MTL_EffectUniforms uniforms = {0};
    uniforms.source_size_px = v2f32((F32)Max(source->size.x, 1), (F32)Max(source->size.y, 1));
    uniforms.output_size_px = v2f32((F32)Max(target->size.x, 1), (F32)Max(target->size.y, 1));
    uniforms.params0 = params->params[0];
    uniforms.params1 = params->params[1];
    U64 uniform_offset = 0;
    id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&uniforms, sizeof(uniforms), 256, &uniform_offset);

    stage_pass.colorAttachments[0].texture = target->texture;
    stage_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    stage_pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    stage_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:stage_pass];
    [encoder setRenderPipelineState:effect->pipeline];
    [encoder setFragmentBuffer:uniform_buffer offset:uniform_offset atIndex:0];
    [encoder setFragmentTexture:source->texture atIndex:0];
    [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Linear] atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
  }
}

r_hook void
r_window_submit(WM_Window window, R_Handle window_equip, R_PassList *passes)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
    if(mtl_window != 0 && r_mtl_state->command_queue != 0 && mtl_window->stage_color != 0)
    {
      B32 display_driven = r_mtl_window_uses_display_link(window, mtl_window);
      id<CAMetalDrawable> drawable = display_driven ? mtl_window->ready_drawable : [mtl_window->layer nextDrawable];
      if(drawable != 0)
      {
        id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
        Rng2F32 viewport_rect = wm_client_rect_from_window(window);
        Vec2F32 viewport_dim = dim_2f32(viewport_rect);
        F32 scale = r_mtl_contents_scale_from_window(window);

      MTLRenderPassDescriptor *stage_clear_pass = mtl_window->stage_clear_pass;
      stage_clear_pass.colorAttachments[0].texture = mtl_window->stage_color;
      stage_clear_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      stage_clear_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      stage_clear_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);
      id<MTLRenderCommandEncoder> clear_encoder = [command_buffer renderCommandEncoderWithDescriptor:stage_clear_pass];
      [clear_encoder endEncoding];

      // NOTE: a surface target's first pass this submit clears it; later passes
      // (e.g. a parent surface resuming after a nested child's bracket) load
      R_MTL_TouchedTargets touched = {0};

      for(R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
      {
        R_Pass *render_pass = &pass_n->v;
        switch(render_pass->kind)
        {
          default:{}break;
          case R_PassKind_UI:
          {
            r_mtl_encode_ui_pass(command_buffer, mtl_window->stage_pass, render_pass->params_ui, mtl_window->stage_color,
                                 viewport_dim, mtl_window->drawable_size, scale, &touched);
          }break;
          case R_PassKind_Effect:
          {
            r_mtl_encode_effect_pass(command_buffer, mtl_window->stage_pass, render_pass->params_effect);
          }break;

          case R_PassKind_Blur:
          {
            if(r_mtl_state->blur_pipeline != 0)
            {
              r_mtl_encode_blur(command_buffer, render_pass->params_blur, viewport_dim, mtl_window->drawable_size, scale,
                                mtl_window->stage_color, mtl_window->stage_scratch_color, mtl_window->blur_pass);
            }
          }break;
          case R_PassKind_Geo3D:
          {
            if(r_mtl_state->mesh_pipeline != 0 &&
               r_mtl_state->geo3d_composite_pipeline != 0 &&
               mtl_window->geo3d_color != 0 &&
               mtl_window->geo3d_depth != 0)
            {
              r_mtl_encode_geo3d(command_buffer, render_pass->params_geo3d, mtl_window->drawable_size, scale,
                                 mtl_window->geo3d_color, mtl_window->geo3d_depth, mtl_window->stage_color,
                                 mtl_window->geo_pass, mtl_window->composite_pass);
            }
          }break;
        }
      }

      if(r_mtl_state->finalize_pipeline != 0)
      {
        MTLRenderPassDescriptor *final_pass = mtl_window->final_pass;
        final_pass.colorAttachments[0].texture = drawable.texture;
        final_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        final_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        final_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);
        id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:final_pass];
        [encoder setRenderPipelineState:r_mtl_state->finalize_pipeline];
        [encoder setFragmentTexture:mtl_window->stage_color atIndex:0];
        [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Nearest] atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [encoder endEncoding];
      }
        [command_buffer addCompletedHandler:r_mtl_state->command_buffer_completion_handler];
        // A bounded opt-in trace observes actual presentation without stopping
        // the UI thread. Capture values, never renderer/window state, in the
        // asynchronous callback. The descriptor stays open until process exit.
        int trace_fd = r_mtl_state->presentation_log_fd;
        if(trace_fd >= 0 && r_mtl_state->presentation_log_serial < 600)
        {
          U64 serial = ++r_mtl_state->presentation_log_serial;
          S64 number = [mac_wm_window_from_handle(window)->ns_window windowNumber];
          double submitted = CACurrentMediaTime();
          double target = display_driven ? mtl_window->target_presentation_time : 0;
          [drawable addPresentedHandler:^(id<MTLDrawable> shown)
          {
            char line[192];
            int count = snprintf(line, sizeof(line), "%lld,%llu,%.9f,%.9f,%.9f,%u\n",
                                 number, serial, submitted, target, shown.presentedTime, display_driven);
            if(count > 0) { (void)write(trace_fd, line, Min((U64)count, sizeof(line)-1)); }
          }];
        }
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
        if(display_driven)
        {
          [mtl_window->ready_drawable release];
          mtl_window->ready_drawable = 0;
        }
        U64 upload_slot_idx = r_mtl_state->upload_buffer_idx % ArrayCount(r_mtl_state->upload_buffer_command_buffers);
        id<MTLCommandBuffer> old_command_buffer = r_mtl_state->upload_buffer_command_buffers[upload_slot_idx];
        if(old_command_buffer != 0)
        {
          [old_command_buffer release];
        }
        r_mtl_state->upload_buffer_command_buffers[upload_slot_idx] = [command_buffer retain];
      }
    }
  }
}

r_hook R_Readback
r_pass_list_readback(Arena *arena, Vec2S32 size, R_PassList *passes)
{
  R_Readback result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    size.x = Max(size.x, 1);
    size.y = Max(size.y, 1);
    if(r_mtl_state->device != 0 &&
       r_mtl_state->command_queue != 0 &&
       r_mtl_state->rect_pipeline != 0 &&
       r_mtl_state->finalize_pipeline != 0)
    {
      MTLTextureDescriptor *stage_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                                  width:(NSUInteger)size.x
                                                                                                 height:(NSUInteger)size.y
                                                                                              mipmapped:NO];
      stage_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
      stage_descriptor.storageMode = MTLStorageModePrivate;
      id<MTLTexture> stage_color = [r_mtl_state->device newTextureWithDescriptor:stage_descriptor];
      [stage_color setLabel:@"RAD readback stage color"];

      MTLTextureDescriptor *final_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB
                                                                                                  width:(NSUInteger)size.x
                                                                                                 height:(NSUInteger)size.y
                                                                                              mipmapped:NO];
      final_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
      final_descriptor.storageMode = MTLStorageModeShared;
      id<MTLTexture> final_color = [r_mtl_state->device newTextureWithDescriptor:final_descriptor];
      [final_color setLabel:@"RAD readback final color"];
      id<MTLTexture> scratch_color = 0;
      id<MTLTexture> geo_color = 0;
      id<MTLTexture> geo_depth = 0;

      if(stage_color != 0 && final_color != 0)
      {
        id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
        Vec2F32 viewport_dim = v2f32((F32)size.x, (F32)size.y);

        MTLRenderPassDescriptor *stage_clear_pass = [MTLRenderPassDescriptor renderPassDescriptor];
        stage_clear_pass.colorAttachments[0].texture = stage_color;
        stage_clear_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        stage_clear_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        stage_clear_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);
        id<MTLRenderCommandEncoder> clear_encoder = [command_buffer renderCommandEncoderWithDescriptor:stage_clear_pass];
        [clear_encoder endEncoding];
        R_MTL_TouchedTargets touched = {0};

        for(R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
        {
          R_Pass *render_pass = &pass_n->v;
          if(render_pass->kind == R_PassKind_Blur && render_pass->params_blur != 0 && r_mtl_state->blur_pipeline != 0)
          {
            // Blur the stage in place, as window submission does, through a
            // scratch texture made the first time a blur needs one.
            if(scratch_color == 0)
            {
              scratch_color = [r_mtl_state->device newTextureWithDescriptor:stage_descriptor];
              [scratch_color setLabel:@"RAD readback stage scratch color"];
            }
            if(scratch_color != 0)
            {
              r_mtl_encode_blur(command_buffer, render_pass->params_blur, viewport_dim, size, 1.f, stage_color, scratch_color, 0);
            }
            continue;
          }
          if(render_pass->kind == R_PassKind_Geo3D && render_pass->params_geo3d != 0 &&
             r_mtl_state->mesh_pipeline != 0 && r_mtl_state->geo3d_composite_pipeline != 0)
          {
            // Geo3D targets sized like the readback stage, made the first time
            // a Geo3D pass needs them.
            if(geo_color == 0)
            {
              MTLTextureDescriptor *geo_color_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                              width:(NSUInteger)size.x
                                                                                                             height:(NSUInteger)size.y
                                                                                                          mipmapped:NO];
              geo_color_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
              geo_color_descriptor.storageMode = MTLStorageModePrivate;
              geo_color = [r_mtl_state->device newTextureWithDescriptor:geo_color_descriptor];
              [geo_color setLabel:@"RAD readback Geo3D color"];
              MTLTextureDescriptor *geo_depth_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                                              width:(NSUInteger)size.x
                                                                                                             height:(NSUInteger)size.y
                                                                                                          mipmapped:NO];
              geo_depth_descriptor.usage = MTLTextureUsageRenderTarget;
              geo_depth_descriptor.storageMode = MTLStorageModePrivate;
              geo_depth = [r_mtl_state->device newTextureWithDescriptor:geo_depth_descriptor];
              [geo_depth setLabel:@"RAD readback Geo3D depth"];
            }
            if(geo_color != 0 && geo_depth != 0)
            {
              r_mtl_encode_geo3d(command_buffer, render_pass->params_geo3d, size, 1.f, geo_color, geo_depth, stage_color, 0, 0);
            }
            continue;
          }
          if(render_pass->kind == R_PassKind_UI && render_pass->params_ui != 0)
          {
            r_mtl_encode_ui_pass(command_buffer, [MTLRenderPassDescriptor renderPassDescriptor], render_pass->params_ui,
                                 stage_color, viewport_dim, size, 1.f, &touched);
          }
          else if(render_pass->kind == R_PassKind_Effect && render_pass->params_effect != 0)
          {
            r_mtl_encode_effect_pass(command_buffer, [MTLRenderPassDescriptor renderPassDescriptor], render_pass->params_effect);
          }
        }

        MTLRenderPassDescriptor *final_pass = [MTLRenderPassDescriptor renderPassDescriptor];
        final_pass.colorAttachments[0].texture = final_color;
        final_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        final_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        final_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);
        id<MTLRenderCommandEncoder> final_encoder = [command_buffer renderCommandEncoderWithDescriptor:final_pass];
        [final_encoder setRenderPipelineState:r_mtl_state->finalize_pipeline];
        [final_encoder setFragmentTexture:stage_color atIndex:0];
        [final_encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Nearest] atIndex:0];
        [final_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [final_encoder endEncoding];

        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        r_mtl_log_command_buffer_error(command_buffer);

        U64 row_bytes = (U64)size.x*4;
        U64 data_size = row_bytes*(U64)size.y;
        U8 *data = push_array_no_zero(arena, U8, data_size);
        MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)size.x, (NSUInteger)size.y);
        [final_color getBytes:data bytesPerRow:row_bytes fromRegion:region mipmapLevel:0];
        result.size = size;
        result.format = R_Tex2DFormat_BGRA8;
        result.data = str8(data, data_size);
      }

      [scratch_color release];
      [geo_color release];
      [geo_depth release];
      [stage_color release];
      [final_color release];
    }
  }
  return result;
}

//- Adapter identity and resources shared by other processes.
typedef struct R_MTL_Timeline R_MTL_Timeline;
struct R_MTL_Timeline
{
  id<MTLSharedEvent> event;
  MTLSharedEventHandle *handle;
};

r_hook R_AdapterInfo
r_adapter_info(void)
{
  R_AdapterInfo info = {0};
  if(r_mtl_state->device)
  {
    info.id = r_mtl_state->device.registryID;
    info.shared_timelines = 1;
    snprintf((char *)info.description, sizeof(info.description), "%s", r_mtl_state->device.name.UTF8String);
  }
  return info;
}

r_hook void *
r_native_device(void)
{
  return r_mtl_state->device;
}

r_hook R_Handle
r_tex2d_open_shared(void *os_handle)
{
  R_Handle result = {0};
  IOSurfaceRef surface = (IOSurfaceRef)os_handle;
  if(!surface) { return result; }
  OSType fourcc = IOSurfaceGetPixelFormat(surface);
  R_Tex2DFormat format;
  MTLPixelFormat mtl_format;
  if(fourcc == 'BGRA') { format = R_Tex2DFormat_BGRA8; mtl_format = MTLPixelFormatBGRA8Unorm; }
  else if(fourcc == 'RGBA') { format = R_Tex2DFormat_RGBA8; mtl_format = MTLPixelFormatRGBA8Unorm; }
  else { return result; }
  size_t width = IOSurfaceGetWidth(surface), height = IOSurfaceGetHeight(surface);
  if(!width || !height || width > 16384 || height > 16384 ||
     IOSurfaceGetPlaneCount(surface) || IOSurfaceGetBytesPerElement(surface) != 4 ||
     IOSurfaceGetBytesPerRow(surface) < width*4) { return result; }
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_format width:width height:height mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> imported = [r_mtl_state->device newTextureWithDescriptor:desc iosurface:surface plane:0];
    if(imported)
    {
      R_MTL_Tex2D *texture = r_mtl_state->free_tex2d;
      if(texture) { SLLStackPop(r_mtl_state->free_tex2d); }
      else { texture = push_array_no_zero(r_mtl_state->arena, R_MTL_Tex2D, 1); }
      MemoryZeroStruct(texture);
      texture->texture = imported;
      texture->kind = R_ResourceKind_Dynamic;
      texture->format = format;
      texture->size = v2s32((S32)width, (S32)height);
      result = r_mtl_handle_from_tex2d(texture);
    }
  }
  return result;
}

internal R_Handle
r_mtl_timeline(id<MTLSharedEvent> event)
{
  R_Handle result = {0};
  if(event)
  {
    R_MTL_Timeline *timeline = calloc(1, sizeof(*timeline));
    MTLSharedEventHandle *handle = [event newSharedEventHandle];
    if(timeline && handle)
    {
      timeline->event = event;
      timeline->handle = handle;
      result.u64[0] = (U64)timeline;
    }
    else { [handle release]; [event release]; free(timeline); }
  }
  return result;
}

r_hook R_Handle
r_timeline_alloc_shared(void)
{
  return r_mtl_timeline([r_mtl_state->device newSharedEvent]);
}

r_hook R_Handle
r_timeline_open_shared(void *os_handle)
{
  return r_mtl_timeline(os_handle ? [r_mtl_state->device newSharedEventWithHandle:(MTLSharedEventHandle *)os_handle] : 0);
}

r_hook void
r_timeline_release(R_Handle handle)
{
  R_MTL_Timeline *timeline = (R_MTL_Timeline *)handle.u64[0];
  if(timeline) { [timeline->handle release]; [timeline->event release]; free(timeline); }
}

r_hook void *
r_native_timeline(R_Handle handle)
{
  R_MTL_Timeline *timeline = (R_MTL_Timeline *)handle.u64[0];
  return timeline ? timeline->handle : 0;
}

r_hook B32
r_queue_wait(R_Handle handle, U64 value)
{
  R_MTL_Timeline *timeline = (R_MTL_Timeline *)handle.u64[0];
  if(!timeline) { return 0; }
  id<MTLCommandBuffer> buffer = [r_mtl_state->command_queue commandBuffer];
  if(!buffer) { return 0; }
  [buffer encodeWaitForEvent:timeline->event value:value];
  [buffer commit];
  return 1;
}

r_hook B32
r_queue_signal(R_Handle handle, U64 value)
{
  R_MTL_Timeline *timeline = (R_MTL_Timeline *)handle.u64[0];
  if(!timeline) { return 0; }
  id<MTLCommandBuffer> buffer = [r_mtl_state->command_queue commandBuffer];
  if(!buffer) { return 0; }
  [buffer encodeSignalEvent:timeline->event value:value];
  [buffer commit];
  return 1;
}
