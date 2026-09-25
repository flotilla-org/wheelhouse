// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Globals

global FP_DWrite_State *fp_dwrite_state = 0;
global FP_DWrite_FontFileLoaderVTable fp_dwrite_static_data_font_file_loader__vtable =
{
  fp_dwrite_iunknown_noop__query_interface,
  fp_dwrite_iunknown_noop__add_ref,
  fp_dwrite_iunknown_noop__release,
  fp_dwrite_static_font_file_loader__stream_from_key,
};
global FP_DWrite_FontFileLoader fp_dwrite_static_data_font_file_loader = {&fp_dwrite_static_data_font_file_loader__vtable};
global FP_DWrite_FontFileStreamVTable fp_dwrite_static_data_font_file_stream__vtable =
{
  fp_dwrite_iunknown_noop__query_interface,
  fp_dwrite_iunknown_noop__add_ref,
  fp_dwrite_iunknown_noop__release,
  fp_dwrite_static_font_file_stream__read_file_fragment,
  fp_dwrite_static_font_file_stream__release_file_fragment,
  fp_dwrite_static_font_file_stream__get_file_size,
  fp_dwrite_static_font_file_stream__get_last_write_time,
};

////////////////////////////////
//~ rjf: Helpers

//- rjf: handle conversion functions

internal FP_DWrite_Font
fp_dwrite_font_from_handle(FP_Handle handle)
{
  FP_DWrite_Font result = {0};
  result.file = (IDWriteFontFile *)handle.u64[0];
  result.face = (IDWriteFontFace *)handle.u64[1];
  return result;
}

internal FP_Handle
fp_dwrite_handle_from_font(FP_DWrite_Font font)
{
  FP_Handle result = {0};
  result.u64[0] = (U64)font.file;
  result.u64[1] = (U64)font.face;
  return result;
}

internal void
fp_dwrite_bitmap_render_target_clear(IDWriteBitmapRenderTarget *render_target, Vec2S16 dim, COLORREF color)
{
  HDC dc = IDWriteBitmapRenderTarget_GetMemoryDC(render_target);
  HGDIOBJ original_pen = SelectObject(dc, GetStockObject(DC_PEN));
  SetDCPenColor(dc, color);
  HGDIOBJ original_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  SetDCBrushColor(dc, color);
  Rectangle(dc, 0, 0, dim.x, dim.y);
  SelectObject(dc, original_brush);
  SelectObject(dc, original_pen);
}

internal DIBSECTION
fp_dwrite_dib_from_bitmap_render_target(IDWriteBitmapRenderTarget *render_target)
{
  HDC dc = IDWriteBitmapRenderTarget_GetMemoryDC(render_target);
  HBITMAP bitmap = (HBITMAP)GetCurrentObject(dc, OBJ_BITMAP);
  DIBSECTION result = {0};
  GetObject(bitmap, sizeof(result), &result);
  return result;
}

internal Vec4F32
fp_dwrite_rgba_from_color_glyph_run(DWRITE_COLOR_GLYPH_RUN const *run)
{
  Vec4F32 result = v4f32(1, 1, 1, 1);
  if(run->paletteIndex != 0xffff ||
     run->runColor.r != 0 ||
     run->runColor.g != 0 ||
     run->runColor.b != 0 ||
     run->runColor.a != 0)
  {
    result = v4f32(Clamp(0.f, run->runColor.r, 1.f),
                   Clamp(0.f, run->runColor.g, 1.f),
                   Clamp(0.f, run->runColor.b, 1.f),
                   Clamp(0.f, run->runColor.a, 1.f));
  }
  return result;
}

internal void
fp_dwrite_composite_straight_rgba(U8 *dst_pixel, Vec4F32 color, F32 coverage)
{
  F32 src_a = Clamp(0.f, color.w*coverage, 1.f);
  if(src_a > 0)
  {
    F32 dst_r = (F32)dst_pixel[0]/255.f;
    F32 dst_g = (F32)dst_pixel[1]/255.f;
    F32 dst_b = (F32)dst_pixel[2]/255.f;
    F32 dst_a = (F32)dst_pixel[3]/255.f;
    F32 out_a = src_a + dst_a*(1.f - src_a);
    F32 out_r = 0;
    F32 out_g = 0;
    F32 out_b = 0;
    if(out_a > 0)
    {
      out_r = (color.x*src_a + dst_r*dst_a*(1.f - src_a))/out_a;
      out_g = (color.y*src_a + dst_g*dst_a*(1.f - src_a))/out_a;
      out_b = (color.z*src_a + dst_b*dst_a*(1.f - src_a))/out_a;
    }
    dst_pixel[0] = (U8)Clamp(0, (S32)round_f32(out_r*255.f), 255);
    dst_pixel[1] = (U8)Clamp(0, (S32)round_f32(out_g*255.f), 255);
    dst_pixel[2] = (U8)Clamp(0, (S32)round_f32(out_b*255.f), 255);
    dst_pixel[3] = (U8)Clamp(0, (S32)round_f32(out_a*255.f), 255);
  }
}

//- rjf: system font lookup

// Finds `family` in the DirectWrite system font collection and returns the path
// of its regular face's file, or empty if the family is not installed or its
// face is not the first in a local file (fp_font_open opens face 0 by path).
internal String8
fp_dwrite_system_font_path_from_family(Arena *arena, String8 family)
{
  String8 result = {0};
  Temp scratch = scratch_begin(&arena, 1);
  String16 family16 = str16_from_8(scratch.arena, family);
  IDWriteFontCollection *collection = 0;
  IDWriteFontFamily *font_family = 0;
  IDWriteFont *font = 0;
  IDWriteFontFace *face = 0;
  IDWriteFontFile *file = 0;
  IDWriteFontFileLoader *loader = 0;
  IDWriteLocalFontFileLoader *local_loader = 0;
  UINT32 family_idx = 0;
  BOOL family_exists = 0;
  UINT32 file_count = 1;
  if(SUCCEEDED(IDWriteFactory_GetSystemFontCollection(fp_dwrite_state->factory, &collection, 0)) &&
     SUCCEEDED(IDWriteFontCollection_FindFamilyName(collection, (WCHAR *)family16.str, &family_idx, &family_exists)) && family_exists &&
     SUCCEEDED(IDWriteFontCollection_GetFontFamily(collection, family_idx, &font_family)) &&
     SUCCEEDED(IDWriteFontFamily_GetFirstMatchingFont(font_family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)) &&
     SUCCEEDED(IDWriteFont_CreateFontFace(font, &face)) &&
     IDWriteFontFace_GetIndex(face) == 0 &&
     SUCCEEDED(IDWriteFontFace_GetFiles(face, &file_count, &file)) && file != 0 &&
     SUCCEEDED(IDWriteFontFile_GetLoader(file, &loader)) &&
     SUCCEEDED(IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void **)&local_loader)))
  {
    void const *key = 0;
    UINT32 key_size = 0;
    UINT32 path_length = 0;
    if(SUCCEEDED(IDWriteFontFile_GetReferenceKey(file, &key, &key_size)) &&
       SUCCEEDED(IDWriteLocalFontFileLoader_GetFilePathLengthFromKey(local_loader, key, key_size, &path_length)) &&
       path_length != 0)
    {
      WCHAR *path16 = push_array(scratch.arena, WCHAR, path_length + 1);
      if(SUCCEEDED(IDWriteLocalFontFileLoader_GetFilePathFromKey(local_loader, key, key_size, path16, path_length + 1)))
      {
        result = str8_from_16(arena, str16((U16 *)path16, path_length));
      }
    }
  }
  if(local_loader != 0) { IDWriteLocalFontFileLoader_Release(local_loader); }
  if(loader != 0)       { IDWriteFontFileLoader_Release(loader); }
  if(file != 0)         { IDWriteFontFile_Release(file); }
  if(face != 0)         { IDWriteFontFace_Release(face); }
  if(font != 0)         { IDWriteFont_Release(font); }
  if(font_family != 0)  { IDWriteFontFamily_Release(font_family); }
  if(collection != 0)   { IDWriteFontCollection_Release(collection); }
  scratch_end(scratch);
  return result;
}

//- rjf: file stream allocator

internal FP_DWrite_FontFileStreamNode *
fp_dwrite_font_file_stream_node_alloc(String8 *data_ptr)
{
  FP_DWrite_FontFileStreamNode *node = 0;
  for(FP_DWrite_FontFileStreamNode *n = fp_dwrite_state->first_stream_node; n != 0; n = n->next)
  {
    if(n->stream.data == data_ptr)
    {
      node = n;
      break;
    }
  }
  if(node == 0)
  {
    node = fp_dwrite_state->free_stream_node;
    if(node != 0)
    {
      SLLStackPop(fp_dwrite_state->free_stream_node);
    }
    else
    {
      node = push_array_no_zero(fp_dwrite_state->arena, FP_DWrite_FontFileStreamNode, 1);
    }
    MemoryZeroStruct(node);
    node->stream.lpVtbl = &fp_dwrite_static_data_font_file_stream__vtable;
    node->stream.data = data_ptr;
    DLLPushBack(fp_dwrite_state->first_stream_node, fp_dwrite_state->last_stream_node, node);
  }
  return node;
}

internal void
fp_dwrite_font_file_stream_node_release(FP_DWrite_FontFileStreamNode *node)
{
  DLLPushBack(fp_dwrite_state->first_stream_node, fp_dwrite_state->last_stream_node, node);
  SLLStackPush(fp_dwrite_state->free_stream_node, node);
}

//- rjf: iunknown no-op helpers

internal HRESULT
fp_dwrite_iunknown_noop__query_interface(void *obj, REFIID riid, void *ptr_to_object)
{
  return E_NOINTERFACE;
}

internal ULONG
fp_dwrite_iunknown_noop__add_ref(void *obj)
{
  ULONG result = 1;
  return result;
}

internal ULONG
fp_dwrite_iunknown_noop__release(void *obj)
{
  ULONG result = 1;
  return result;
}

//- rjf: font file loader interface function implementations

internal HRESULT
fp_dwrite_static_font_file_loader__stream_from_key(FP_DWrite_FontFileLoader *obj, void const *font_file_ref_key, UINT32 font_file_ref_key_size, IDWriteFontFileStream **stream_out)
{
  HRESULT result = S_OK;
  String8 *key = *(String8 **)font_file_ref_key;
  FP_DWrite_FontFileStreamNode *node = fp_dwrite_font_file_stream_node_alloc(key);
  *stream_out = (IDWriteFontFileStream *)&node->stream;
  return result;
}

//- rjf: font file stream  interface function implementations

internal HRESULT
fp_dwrite_static_font_file_stream__read_file_fragment(FP_DWrite_FontFileStream *obj, void const **fragment_start, UINT64 file_offset, UINT64 fragment_size, void **fragment_context)
{
  HRESULT result = S_OK;
  *fragment_start = obj->data->str + file_offset;
  *fragment_context = 0;
  return result;
}

internal HRESULT
fp_dwrite_static_font_file_stream__release_file_fragment(FP_DWrite_FontFileStream *obj, void *fragment_context)
{
  HRESULT result = S_OK;
  return result;
}

internal HRESULT
fp_dwrite_static_font_file_stream__get_file_size(FP_DWrite_FontFileStream *obj, UINT64 *size_out)
{
  HRESULT result = S_OK;
  *size_out = obj->data->size;
  return result;
}

internal HRESULT
fp_dwrite_static_font_file_stream__get_last_write_time(FP_DWrite_FontFileStream *obj, UINT64 *time_out)
{
  HRESULT result = S_OK;
  *time_out = 0;
  return result;
}

////////////////////////////////
//~ rjf: Backend Implementations

fp_hook void
fp_init(void)
{
  ProfBeginFunction();
  HRESULT error = 0;
  
  //- rjf: initialize main state
  {
    Arena *arena = arena_alloc();
    fp_dwrite_state = push_array(arena, FP_DWrite_State, 1);
    fp_dwrite_state->arena = arena;
  }
  
  //- rjf: make dwrite factory
  error = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2, (void **)&fp_dwrite_state->factory);
  if(error == S_OK)
  {
    fp_dwrite_state->dwrite2_is_supported = 1;
  }
  else
  {
    error = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (void **)&fp_dwrite_state->factory);
  }
  
  //- rjf: register static data font "loader" interface
  error = IDWriteFactory_RegisterFontFileLoader(fp_dwrite_state->factory, (IDWriteFontFileLoader *)&fp_dwrite_static_data_font_file_loader);
  
  //- rjf: make base rendering params
  error = IDWriteFactory_CreateRenderingParams(fp_dwrite_state->factory, &fp_dwrite_state->base_rendering_params);
  
  //- rjf: make sharp-hinted rendering params
  {
    FLOAT gamma = IDWriteRenderingParams_GetGamma(fp_dwrite_state->base_rendering_params);
    gamma = 1.f;
    FLOAT enhanced_contrast = IDWriteRenderingParams_GetEnhancedContrast(fp_dwrite_state->base_rendering_params);
    if(fp_dwrite_state->dwrite2_is_supported)
    {
      error = IDWriteFactory2_CreateCustomRenderingParams2((IDWriteFactory2 *)fp_dwrite_state->factory,
                                                           gamma,
                                                           enhanced_contrast,
                                                           enhanced_contrast,
                                                           0.f,
                                                           DWRITE_PIXEL_GEOMETRY_FLAT,
                                                           DWRITE_RENDERING_MODE_GDI_NATURAL,
                                                           DWRITE_GRID_FIT_MODE_ENABLED,
                                                           (IDWriteRenderingParams2 **)&fp_dwrite_state->rendering_params_sharp_hinted);
    }
    else
    {
      error = IDWriteFactory_CreateCustomRenderingParams(fp_dwrite_state->factory,
                                                         gamma,
                                                         enhanced_contrast,
                                                         0.f,
                                                         DWRITE_PIXEL_GEOMETRY_FLAT,
                                                         DWRITE_RENDERING_MODE_GDI_NATURAL,
                                                         &fp_dwrite_state->rendering_params_sharp_hinted);
    }
  }
  
  //- rjf: make sharp-unhinted rendering params
  {
    FLOAT gamma = IDWriteRenderingParams_GetGamma(fp_dwrite_state->base_rendering_params);
    gamma = 1.f;
    FLOAT enhanced_contrast = IDWriteRenderingParams_GetEnhancedContrast(fp_dwrite_state->base_rendering_params);
    if(fp_dwrite_state->dwrite2_is_supported)
    {
      error = IDWriteFactory2_CreateCustomRenderingParams2((IDWriteFactory2 *)fp_dwrite_state->factory,
                                                           gamma,
                                                           enhanced_contrast,
                                                           enhanced_contrast,
                                                           0.f,
                                                           DWRITE_PIXEL_GEOMETRY_FLAT,
                                                           DWRITE_RENDERING_MODE_GDI_NATURAL,
                                                           DWRITE_GRID_FIT_MODE_DISABLED,
                                                           (IDWriteRenderingParams2 **)&fp_dwrite_state->rendering_params_sharp_unhinted);
    }
    else
    {
      error = IDWriteFactory_CreateCustomRenderingParams(fp_dwrite_state->factory,
                                                         gamma,
                                                         enhanced_contrast,
                                                         0.f,
                                                         DWRITE_PIXEL_GEOMETRY_FLAT,
                                                         DWRITE_RENDERING_MODE_GDI_NATURAL,
                                                         &fp_dwrite_state->rendering_params_sharp_unhinted);
    }
  }
  
  //- rjf: make smooth-hinted rendering params
  {
    FLOAT gamma = IDWriteRenderingParams_GetGamma(fp_dwrite_state->base_rendering_params);
    gamma = 1.f;
    FLOAT enhanced_contrast = IDWriteRenderingParams_GetEnhancedContrast(fp_dwrite_state->base_rendering_params);
    if(fp_dwrite_state->dwrite2_is_supported)
    {
      error = IDWriteFactory2_CreateCustomRenderingParams2((IDWriteFactory2 *)fp_dwrite_state->factory,
                                                           gamma,
                                                           enhanced_contrast,
                                                           enhanced_contrast,
                                                           0.f,
                                                           DWRITE_PIXEL_GEOMETRY_FLAT,
                                                           DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                           DWRITE_GRID_FIT_MODE_ENABLED,
                                                           (IDWriteRenderingParams2 **)&fp_dwrite_state->rendering_params_smooth_hinted);
    }
    else
    {
      error = IDWriteFactory_CreateCustomRenderingParams(fp_dwrite_state->factory,
                                                         gamma,
                                                         enhanced_contrast,
                                                         0.f,
                                                         DWRITE_PIXEL_GEOMETRY_FLAT,
                                                         DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                         &fp_dwrite_state->rendering_params_smooth_hinted);
    }
  }
  
  //- rjf: make smooth rendering params
  {
    FLOAT gamma = 1.f;
    FLOAT enhanced_contrast = 0.f;
    if(fp_dwrite_state->dwrite2_is_supported)
    {
      error = IDWriteFactory2_CreateCustomRenderingParams2((IDWriteFactory2 *)fp_dwrite_state->factory,
                                                           gamma,
                                                           enhanced_contrast,
                                                           enhanced_contrast,
                                                           0.f,
                                                           DWRITE_PIXEL_GEOMETRY_FLAT,
                                                           DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                           DWRITE_GRID_FIT_MODE_DISABLED,
                                                           (IDWriteRenderingParams2 **)&fp_dwrite_state->rendering_params_smooth_unhinted);
    }
    else
    {
      error = IDWriteFactory_CreateCustomRenderingParams(fp_dwrite_state->factory,
                                                         gamma,
                                                         enhanced_contrast,
                                                         0.f,
                                                         DWRITE_PIXEL_GEOMETRY_FLAT,
                                                         DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                         &fp_dwrite_state->rendering_params_smooth_unhinted);
    }
  }
  
  //- rjf: make dwrite gdi interop
  error = IDWriteFactory_GetGdiInterop(fp_dwrite_state->factory, &fp_dwrite_state->gdi_interop);
  
  //- rjf: build render target for rasterization
  fp_dwrite_state->bitmap_render_target_dim = v2s32(2048, 256);
  error = IDWriteGdiInterop_CreateBitmapRenderTarget(fp_dwrite_state->gdi_interop, 0, fp_dwrite_state->bitmap_render_target_dim.x, fp_dwrite_state->bitmap_render_target_dim.y, &fp_dwrite_state->bitmap_render_target);
  IDWriteBitmapRenderTarget_SetPixelsPerDip(fp_dwrite_state->bitmap_render_target, 1.0);
  ProfEnd();
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  FP_DWrite_Font font = {0};
  HRESULT error = 0;
  
  //- rjf: build initial path task
  typedef struct PathTask PathTask;
  struct PathTask
  {
    PathTask *next;
    String8 path;
  };
  PathTask start_task = {0, path};
  PathTask *first_task = &start_task;
  PathTask *last_task = first_task;
  
  //- rjf: try to open font
  for(PathTask *t = first_task; t != 0 && font.file == 0; t = t->next)
  {
    B32 file_exists = (properties_from_file_path(t->path).created != 0);
    String16 path16 = str16_from_8(scratch.arena, t->path);
    if(file_exists)
    {
      error = IDWriteFactory_CreateFontFileReference(fp_dwrite_state->factory, (WCHAR *)path16.str, 0, &font.file);
    }
    if(font.file != 0)
    {
      error = IDWriteFactory_CreateFontFace(fp_dwrite_state->factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &font.file, 0, DWRITE_FONT_SIMULATIONS_NONE, &font.face);
    }
    
    // rjf: failure trying just the normal path? -> generate new tasks that search in system folders
    if(t == first_task && font.file == 0 && t->path.size != 0)
    {
      // rjf: generate task for user-installed fonts
      {
        HKEY reg_key = 0;
        LSTATUS status = 0;
        char name[256] = {0};
        char data[256] = {0};
        DWORD name_size = sizeof(name);
        DWORD data_size = sizeof(data);
        DWORD type = 0;
        status = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders\\Fonts", 0, KEY_QUERY_VALUE, &reg_key);
        status = RegEnumValueA(reg_key, 0, name, &name_size, 0, &type, (unsigned char *)data, &data_size);
        String8 user_fonts_path = str8_cstring(data);
        PathTask *task = push_array(scratch.arena, PathTask, 1);
        task->path = push_str8f(scratch.arena, "%s/%S", user_fonts_path, path);
        SLLQueuePush(first_task, last_task, task);
      }
      
      // rjf: generate task for windows directory (C:/Windows/Fonts, generally)
      {
        char windows_path[256] = {0};
        GetWindowsDirectoryA(windows_path, sizeof(windows_path));
        PathTask *task = push_array(scratch.arena, PathTask, 1);
        task->path = push_str8f(scratch.arena, "%s/Fonts/%S", windows_path, path);
        SLLQueuePush(first_task, last_task, task);
      }
    }
  }
  
  //- rjf: handlify & return
  FP_Handle handle = {0};
  if(font.file != 0)
  {
    handle = fp_dwrite_handle_from_font(font);
  }
  scratch_end(scratch);
  ProfEnd();
  return handle;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  FP_DWrite_Font font = {0};
  HRESULT error = 0;
  
  //- rjf: open font file reference
  error = IDWriteFactory_CreateCustomFontFileReference(fp_dwrite_state->factory, &data_ptr, sizeof(String8 *), (IDWriteFontFileLoader *)&fp_dwrite_static_data_font_file_loader, &font.file);
  
  //- rjf: open font face
  error = IDWriteFactory_CreateFontFace(fp_dwrite_state->factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &font.file, 0, DWRITE_FONT_SIMULATIONS_NONE, &font.face);
  
  //- rjf: handlify & return
  FP_Handle handle = fp_dwrite_handle_from_font(font);
  scratch_end(scratch);
  ProfEnd();
  return handle;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  ProfBeginFunction();
  FP_DWrite_Font font = fp_dwrite_font_from_handle(handle);
  if(font.face != 0)
  {
    IDWriteFontFace_Release(font.face);
  }
  if(font.file != 0)
  {
    IDWriteFontFile_Release(font.file);
  }
  ProfEnd();
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  ProfBeginFunction();
  FP_DWrite_Font font = fp_dwrite_font_from_handle(handle);
  DWRITE_FONT_METRICS metrics = {0};
  if(font.face != 0)
  {
    IDWriteFontFace_GetMetrics(font.face, &metrics);
  }
  FP_Metrics result = {0};
  {
    result.design_units_per_em = (F32)metrics.designUnitsPerEm;
    result.ascent  = (F32)metrics.ascent;
    result.descent = (F32)metrics.descent;
    result.line_gap = (F32)metrics.lineGap;
    result.capital_height = (F32)metrics.capHeight;
  }
  ProfEnd();
  return result;
}

fp_hook B32
fp_font_has_codepoint(FP_Handle font_handle, U32 codepoint)
{
  FP_DWrite_Font font = fp_dwrite_font_from_handle(font_handle);
  B32 result = 0;
  if(font.face != 0)
  {
    U16 glyph_index = 0;
    UINT32 cp = codepoint;
    HRESULT error = IDWriteFontFace_GetGlyphIndices(font.face, &cp, 1, &glyph_index);
    result = (SUCCEEDED(error) && glyph_index != 0);
  }
  return result;
}

fp_hook ASAN_NO_ADDR FP_RasterResult
fp_raster(Arena *arena, FP_Handle font_handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(&arena, 1);
  HRESULT error = 0;
  String32 string32 = str32_from_8(scratch.arena, string);
  FP_DWrite_Font font = fp_dwrite_font_from_handle(font_handle);
  B32 tight_bounds = !!(flags & FP_RasterFlag_TightBounds);
  COLORREF bg_color = RGB(0,   0,   0);
  COLORREF fg_color = RGB(255, 255, 255);
  
  //- rjf: get font metrics
  DWRITE_FONT_METRICS font_metrics = {0};
  if(font.face != 0)
  {
    IDWriteFontFace_GetMetrics(font.face, &font_metrics);
  }
  F32 design_units_per_em = (F32)font_metrics.designUnitsPerEm;
  
  //- rjf: get glyph indices
  U16 *glyph_indices = push_array_no_zero(scratch.arena, U16, string32.size);
  if(font.face != 0)
  {
    error = IDWriteFontFace_GetGlyphIndices(font.face, string32.str, string32.size, glyph_indices);
  }
  
  //- rjf: get metrics info
  U64 glyphs_count = string32.size;
  DWRITE_GLYPH_METRICS *glyphs_metrics = push_array_no_zero(scratch.arena, DWRITE_GLYPH_METRICS, glyphs_count);
  if(font.face != 0)
  {
    error = IDWriteFontFace_GetGdiCompatibleGlyphMetrics(font.face, (96.f/72.f)*size, 1.f, 0, 1, glyph_indices, glyphs_count, glyphs_metrics, 0);
  }
  
  //- rjf: derive info from metrics
  F32 advance = 0;
  Vec2S16 atlas_dim = {0};
  F32 left_side_bearing = 0;
  F32 right_side_bearing = 0;
  if(font.face != 0)
  {
    atlas_dim.y = (S16)round_f32((96.f/72.f) * size * (font_metrics.ascent + font_metrics.descent + font_metrics.lineGap) / design_units_per_em) + 1;
    for(U64 idx = 0; idx < glyphs_count; idx += 1)
    {
      DWRITE_GLYPH_METRICS *glyph_metrics = glyphs_metrics + idx;
      F32 glyph_advance_width         = (96.f/72.f) * size * glyph_metrics->advanceWidth       / design_units_per_em;
      advance += glyph_advance_width;
      atlas_dim.x = Max(atlas_dim.x, (S16)(advance+1));
      if(idx == 0)
      {
        left_side_bearing = (96.f/72.f) * size * glyph_metrics->leftSideBearing    / design_units_per_em;
      }
      if(idx+1 == glyphs_count)
      {
        right_side_bearing = (96.f/72.f) * size * glyph_metrics->rightSideBearing   / design_units_per_em;
      }
    }
    atlas_dim.x -= right_side_bearing;
    atlas_dim.x += 2;
    atlas_dim.x += 7;
    atlas_dim.x -= atlas_dim.x%8;
  }
  
  //- rjf: make dwrite bitmap for rendering
  IDWriteBitmapRenderTarget *render_target = 0;
  if(font.face != 0)
  {
    error = IDWriteGdiInterop_CreateBitmapRenderTarget(fp_dwrite_state->gdi_interop, 0, atlas_dim.x, atlas_dim.y, &render_target);
    IDWriteBitmapRenderTarget_SetPixelsPerDip(render_target, 1.f);
  }
  
  //- rjf: get bitmap & clear
  if(font.face != 0)
  {
    fp_dwrite_bitmap_render_target_clear(render_target, atlas_dim, bg_color);
  }
  
  //- rjf: draw glyph run
  Vec2F32 draw_p = {0, (F32)atlas_dim.y};
  if(font.face != 0)
  {
    F32 descent = round_f32((96.f/72.f)*size * font_metrics.descent / design_units_per_em);
    F32 line_gap = round_f32((96.f/72.f)*size * font_metrics.lineGap / design_units_per_em);
    draw_p.y -= descent;
    draw_p.y -= line_gap;
  }
  DWRITE_GLYPH_RUN glyph_run = {0};
  if(font.face != 0)
  {
    glyph_run.fontFace = font.face;
    glyph_run.fontEmSize = size * 96.f/72.f;
    glyph_run.glyphCount = string32.size;
    glyph_run.glyphIndices = glyph_indices;
  }

  //- rjf: pick rendering params
  IDWriteRenderingParams *rendering_params = fp_dwrite_state->rendering_params_sharp_hinted;
  switch(flags & (FP_RasterFlag_Smooth|FP_RasterFlag_Hinted))
  {
    default:{}break;
    case 0:{rendering_params = fp_dwrite_state->rendering_params_sharp_unhinted;}break;
    case FP_RasterFlag_Hinted:{rendering_params = fp_dwrite_state->rendering_params_sharp_hinted;}break;
    case FP_RasterFlag_Smooth:{rendering_params = fp_dwrite_state->rendering_params_smooth_unhinted;}break;
    case FP_RasterFlag_Smooth|FP_RasterFlag_Hinted:{rendering_params = fp_dwrite_state->rendering_params_smooth_hinted;}break;
  }

  //- rjf: try color layers first
  B32 drew_color_layers = 0;
  U8 *color_atlas = 0;
  RECT color_bounding_box = {0};
  if(font.face != 0 && fp_dwrite_state->dwrite2_is_supported)
  {
    IDWriteColorGlyphRunEnumerator *color_layers = 0;
    HRESULT color_error = IDWriteFactory2_TranslateColorGlyphRun((IDWriteFactory2 *)fp_dwrite_state->factory,
                                                                 draw_p.x,
                                                                 draw_p.y,
                                                                 &glyph_run,
                                                                 0,
                                                                 DWRITE_MEASURING_MODE_NATURAL,
                                                                 0,
                                                                 0,
                                                                 &color_layers);
    if(SUCCEEDED(color_error) && color_layers != 0)
    {
      U64 color_atlas_size = (U64)atlas_dim.x*(U64)atlas_dim.y*4;
      color_atlas = push_array(scratch.arena, U8, color_atlas_size);
      color_bounding_box.left = atlas_dim.x;
      color_bounding_box.top = atlas_dim.y;
      color_bounding_box.right = 0;
      color_bounding_box.bottom = 0;
      for(;;)
      {
        BOOL has_run = 0;
        HRESULT move_error = IDWriteColorGlyphRunEnumerator_MoveNext(color_layers, &has_run);
        if(FAILED(move_error) || !has_run)
        {
          break;
        }
        DWRITE_COLOR_GLYPH_RUN const *color_run = 0;
        HRESULT run_error = IDWriteColorGlyphRunEnumerator_GetCurrentRun(color_layers, &color_run);
        if(FAILED(run_error) || color_run == 0)
        {
          break;
        }

        fp_dwrite_bitmap_render_target_clear(render_target, atlas_dim, bg_color);
        RECT layer_box = {0};
        HRESULT layer_error = IDWriteBitmapRenderTarget_DrawGlyphRun(render_target,
                                                                     color_run->baselineOriginX,
                                                                     color_run->baselineOriginY,
                                                                     DWRITE_MEASURING_MODE_NATURAL,
                                                                     &color_run->glyphRun,
                                                                     rendering_params,
                                                                     fg_color,
                                                                     &layer_box);
        if(FAILED(layer_error))
        {
          continue;
        }

        DIBSECTION layer_dib = fp_dwrite_dib_from_bitmap_render_target(render_target);
        U8 *in_data = (U8 *)layer_dib.dsBm.bmBits;
        if(in_data == 0)
        {
          continue;
        }

        S32 src_x0 = 0;
        S32 src_y0 = 0;
        S32 src_x1 = atlas_dim.x;
        S32 src_y1 = atlas_dim.y;
        if(layer_box.left < layer_box.right && layer_box.top < layer_box.bottom)
        {
          src_x0 = Clamp(0, layer_box.left, atlas_dim.x);
          src_y0 = Clamp(0, layer_box.top, atlas_dim.y);
          src_x1 = Clamp(0, layer_box.right, atlas_dim.x);
          src_y1 = Clamp(0, layer_box.bottom, atlas_dim.y);
        }

        Vec4F32 layer_color = fp_dwrite_rgba_from_color_glyph_run(color_run);
        U64 in_pitch = (U64)layer_dib.dsBm.bmWidthBytes;
        for(S32 y = src_y0; y < src_y1; y += 1)
        {
          U8 *in_pixel = in_data + (U64)y*in_pitch + (U64)src_x0*4;
          U8 *out_pixel = color_atlas + (((U64)y*(U64)atlas_dim.x + (U64)src_x0)*4);
          for(S32 x = src_x0; x < src_x1; x += 1)
          {
            U8 mask = Max(in_pixel[0], Max(in_pixel[1], in_pixel[2]));
            if(mask != 0)
            {
              fp_dwrite_composite_straight_rgba(out_pixel, layer_color, (F32)mask/255.f);
              color_bounding_box.left = Min(color_bounding_box.left, x);
              color_bounding_box.top = Min(color_bounding_box.top, y);
              color_bounding_box.right = Max(color_bounding_box.right, x + 1);
              color_bounding_box.bottom = Max(color_bounding_box.bottom, y + 1);
              drew_color_layers = 1;
            }
            in_pixel += 4;
            out_pixel += 4;
          }
        }
      }
      IDWriteColorGlyphRunEnumerator_Release(color_layers);
    }
  }

  RECT bounding_box = {0};
  if(font.face != 0 && !drew_color_layers)
  {
    fp_dwrite_bitmap_render_target_clear(render_target, atlas_dim, bg_color);
    error = IDWriteBitmapRenderTarget_DrawGlyphRun(render_target, draw_p.x, draw_p.y,
                                                   DWRITE_MEASURING_MODE_NATURAL,
                                                   &glyph_run,
                                                   rendering_params,
                                                   fg_color,
                                                   &bounding_box);
  }
  
  //- rjf: get bitmap
  DIBSECTION dib = {0};
  if(font.face != 0 && !drew_color_layers)
  {
    dib = fp_dwrite_dib_from_bitmap_render_target(render_target);
  }
  
  //- rjf: fill & return
  FP_RasterResult result = {0};
  if(font.face != 0)
  {
    // rjf: fill basics
    S32 src_x0 = 0;
    S32 src_y0 = 0;
    S32 src_x1 = atlas_dim.x;
    S32 src_y1 = atlas_dim.y;
    if(drew_color_layers && tight_bounds && color_bounding_box.left < color_bounding_box.right && color_bounding_box.top < color_bounding_box.bottom)
    {
      src_x0 = Clamp(0, color_bounding_box.left,  atlas_dim.x);
      src_y0 = Clamp(0, color_bounding_box.top,   atlas_dim.y);
      src_x1 = Clamp(0, color_bounding_box.right, atlas_dim.x);
      src_y1 = Clamp(0, color_bounding_box.bottom, atlas_dim.y);
      if(src_x0 >= src_x1 || src_y0 >= src_y1)
      {
        src_x0 = 0;
        src_y0 = 0;
        src_x1 = atlas_dim.x;
        src_y1 = atlas_dim.y;
      }
    }
    else if(!drew_color_layers && tight_bounds && SUCCEEDED(error) && bounding_box.left < bounding_box.right && bounding_box.top < bounding_box.bottom)
    {
      src_x0 = Clamp(0, bounding_box.left,  atlas_dim.x);
      src_y0 = Clamp(0, bounding_box.top,   atlas_dim.y);
      src_x1 = Clamp(0, bounding_box.right, atlas_dim.x);
      src_y1 = Clamp(0, bounding_box.bottom, atlas_dim.y);
      if(src_x0 >= src_x1 || src_y0 >= src_y1)
      {
        src_x0 = 0;
        src_y0 = 0;
        src_x1 = atlas_dim.x;
        src_y1 = atlas_dim.y;
      }
    }
    result.atlas_dim    = v2s16((S16)(src_x1 - src_x0), (S16)(src_y1 - src_y0));
    result.atlas        = push_array_no_zero(arena, U8, result.atlas_dim.x*result.atlas_dim.y*4);
    result.advance      = round_f32(advance);
    result.origin_from_left = draw_p.x - (F32)src_x0;
    result.baseline_from_top = draw_p.y - (F32)src_y0;
    result.face_box_origin_from_left = draw_p.x;
    result.face_box_baseline_from_top = draw_p.y;

    // rjf: fill atlas
    if(drew_color_layers)
    {
      U8 *out_data  = (U8 *)result.atlas;
      U64 out_pitch = result.atlas_dim.x * 4;
      U64 color_sum = 0;
      for(U64 y = 0; y < result.atlas_dim.y; y += 1)
      {
        U8 *in_line = color_atlas + (((U64)src_y0 + y)*(U64)atlas_dim.x + (U64)src_x0)*4;
        U8 *out_line = out_data + y*out_pitch;
        MemoryCopy(out_line, in_line, out_pitch);
        for(U64 x = 0; x < result.atlas_dim.x; x += 1)
        {
          color_sum += out_line[x*4 + 3];
        }
      }
      if(color_sum == 0)
      {
        result.atlas_dim = v2s16(0, 0);
      }
      else
      {
        result.kind = FP_RasterKind_RGBA;
      }
    }
    else
    {
      U8 *in_data   = (U8 *)dib.dsBm.bmBits;
      U64 in_pitch  = (U64)dib.dsBm.bmWidthBytes;
      U8 *out_data  = (U8 *)result.atlas;
      U64 out_pitch = result.atlas_dim.x * 4;
      U64 color_sum = 0;
      U8 *in_line = (U8 *)in_data + (U64)src_y0*in_pitch;
      U8 *out_line = out_data;
      for(U64 y = 0; y < result.atlas_dim.y; y += 1)
      {
        U8 *in_pixel = in_line + (U64)src_x0*4;
        U8 *out_pixel = out_line;
        for(U64 x = 0; x < result.atlas_dim.x; x += 1)
        {
          U8 in_pixel_byte = in_pixel[0];
          out_pixel[0] = 255;
          out_pixel[1] = 255;
          out_pixel[2] = 255;
          out_pixel[3] = in_pixel_byte;
          color_sum += in_pixel_byte;
          in_pixel += 4;
          out_pixel += 4;
        }
        in_line += in_pitch;
        out_line += out_pitch;
      }
      if(color_sum == 0)
      {
        result.atlas_dim = v2s16(0, 0);
      }
    }
    IDWriteBitmapRenderTarget_Release(render_target);
  }
  scratch_end(scratch);
  ProfEnd();
  return result;
}

fp_hook FP_SystemFontArray
fp_system_color_emoji_fonts(void)
{
  if(!fp_dwrite_state->system_color_emoji_fonts_resolved)
  {
    // Segoe UI Emoji is COLR, which fp_raster draws through its colour-layer
    // path; DirectWrite here cannot draw CBDT/PNG emoji fonts.
    String8 families[] = {str8_lit_comp("Segoe UI Emoji")};
    FP_SystemFontArray *fonts = &fp_dwrite_state->system_color_emoji_fonts;
    fonts->v = push_array(fp_dwrite_state->arena, FP_SystemFont, ArrayCount(families));
    fonts->count = ArrayCount(families);
    for EachElement(idx, families)
    {
      fonts->v[idx].family = families[idx];
      fonts->v[idx].path = fp_dwrite_system_font_path_from_family(fp_dwrite_state->arena, families[idx]);
    }
    fp_dwrite_state->system_color_emoji_fonts_resolved = 1;
  }
  return fp_dwrite_state->system_color_emoji_fonts;
}
