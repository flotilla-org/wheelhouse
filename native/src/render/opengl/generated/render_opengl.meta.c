// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

C_LINKAGE_BEGIN
String8 r_ogl_shader_kind_name_table[4] =
{
str8_lit_comp("rect"),
str8_lit_comp("blur"),
str8_lit_comp("mesh"),
str8_lit_comp("composite"),
};

String8 * r_ogl_shader_kind_vshad_src_table[4] =
{
&r_core_rect_vs_glsl,
&r_core_blur_vs_glsl,
&r_core_mesh_vs_glsl,
&r_core_composite_vs_glsl,
};

String8 * r_ogl_shader_kind_pshad_src_table[4] =
{
&r_core_rect_fs_glsl,
&r_core_blur_fs_glsl,
&r_core_mesh_fs_glsl,
&r_core_composite_fs_glsl,
};

R_OGL_AttributeArray r_ogl_shader_kind_input_attributes_table[4] =
{
{ r_ogl_rect_input_attributes, ArrayCount(r_ogl_rect_input_attributes) },
{ 0,  },
{ 0,  },
{ 0,  },
};

R_OGL_AttributeArray r_ogl_shader_kind_output_attributes_table[4] =
{
{ r_ogl_single_color_output_attributes, ArrayCount(r_ogl_single_color_output_attributes) },
{ r_ogl_single_color_output_attributes, ArrayCount(r_ogl_single_color_output_attributes) },
{ r_ogl_single_color_output_attributes, ArrayCount(r_ogl_single_color_output_attributes) },
{ r_ogl_single_color_output_attributes, ArrayCount(r_ogl_single_color_output_attributes) },
};

C_LINKAGE_END
