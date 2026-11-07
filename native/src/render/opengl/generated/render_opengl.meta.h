// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

#ifndef RENDER_OPENGL_META_H
#define RENDER_OPENGL_META_H

typedef enum R_OGL_ShaderKind
{
R_OGL_ShaderKind_Rect,
R_OGL_ShaderKind_Blur,
R_OGL_ShaderKind_COUNT,
} R_OGL_ShaderKind;

C_LINKAGE_BEGIN
extern String8 r_ogl_shader_kind_name_table[2];
extern String8 * r_ogl_shader_kind_vshad_src_table[2];
extern String8 * r_ogl_shader_kind_pshad_src_table[2];
extern R_OGL_AttributeArray r_ogl_shader_kind_input_attributes_table[2];
extern R_OGL_AttributeArray r_ogl_shader_kind_output_attributes_table[2];

C_LINKAGE_END

#endif // RENDER_OPENGL_META_H
