// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

C_LINKAGE_BEGIN
String8* r_d3d11_g_vshad_kind_source_table[5] =
{
&r_core_rect_vs_hlsl,
&r_core_blur_vs_hlsl,
&r_core_mesh_vs_hlsl,
&r_core_composite_vs_hlsl,
&r_core_finalize_vs_hlsl,
};

String8 r_d3d11_g_vshad_kind_source_name_table[5] =
{
str8_lit_comp("r_core_rect_vs_hlsl"),
str8_lit_comp("r_core_blur_vs_hlsl"),
str8_lit_comp("r_core_mesh_vs_hlsl"),
str8_lit_comp("r_core_composite_vs_hlsl"),
str8_lit_comp("r_core_finalize_vs_hlsl"),
};

D3D11_INPUT_ELEMENT_DESC * r_d3d11_g_vshad_kind_elements_ptr_table[5] =
{
r_d3d11_g_rect_ilay_elements,
0,
r_d3d11_g_mesh_ilay_elements,
0,
0,
};

U64 r_d3d11_g_vshad_kind_elements_count_table[5] =
{
ArrayCount(r_d3d11_g_rect_ilay_elements) ,
 0,
ArrayCount(r_d3d11_g_mesh_ilay_elements) ,
 0,
 0,
};

String8* r_d3d11_g_pshad_kind_source_table[5] =
{
&r_core_rect_fs_hlsl,
&r_core_blur_fs_hlsl,
&r_core_mesh_fs_hlsl,
&r_core_composite_fs_hlsl,
&r_core_finalize_fs_hlsl,
};

String8 r_d3d11_g_pshad_kind_source_name_table[5] =
{
str8_lit_comp("r_core_rect_fs_hlsl"),
str8_lit_comp("r_core_blur_fs_hlsl"),
str8_lit_comp("r_core_mesh_fs_hlsl"),
str8_lit_comp("r_core_composite_fs_hlsl"),
str8_lit_comp("r_core_finalize_fs_hlsl"),
};

C_LINKAGE_END
