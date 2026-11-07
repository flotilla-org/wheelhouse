// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

#ifndef RENDER_D3D11_META_H
#define RENDER_D3D11_META_H

typedef enum R_D3D11_VShadKind
{
R_D3D11_VShadKind_Rect,
R_D3D11_VShadKind_Blur,
R_D3D11_VShadKind_Mesh,
R_D3D11_VShadKind_Geo3DComposite,
R_D3D11_VShadKind_Finalize,
R_D3D11_VShadKind_COUNT,
} R_D3D11_VShadKind;

typedef enum R_D3D11_PShadKind
{
R_D3D11_PShadKind_Rect,
R_D3D11_PShadKind_Blur,
R_D3D11_PShadKind_Mesh,
R_D3D11_PShadKind_Geo3DComposite,
R_D3D11_PShadKind_Finalize,
R_D3D11_PShadKind_COUNT,
} R_D3D11_PShadKind;

typedef enum R_D3D11_UniformTypeKind
{
R_D3D11_UniformTypeKind_Mesh,
R_D3D11_UniformTypeKind_COUNT,
} R_D3D11_UniformTypeKind;

C_LINKAGE_BEGIN
read_only global String8 r_d3d11_g_mesh_shader_src =
str8_lit_comp(
""
"\n"
"cbuffer Uniforms : register(b0)\n"
"{\n"
"  row_major float4x4 xform;\n"
"}\n"
"\n"
"struct CPU2Vertex\n"
"{\n"
"  float3 position : POS;\n"
"  float3 normal   : NOR;\n"
"  float2 texcoord : TEX;\n"
"  float3 color    : COL;\n"
"};\n"
"\n"
"struct Vertex2Pixel\n"
"{\n"
"  float4 position : SV_POSITION;\n"
"  float2 texcoord : TEX;\n"
"  float4 color    : COL;\n"
"};\n"
"\n"
"Vertex2Pixel vs_main(CPU2Vertex c2v)\n"
"{\n"
"  Vertex2Pixel v2p;\n"
"  v2p.position = mul(float4(c2v.position, 1.f), xform);\n"
"  v2p.texcoord = c2v.texcoord;\n"
"  v2p.color    = float4(c2v.color, 1.f);\n"
"  return v2p;\n"
"}\n"
"\n"
"float4 ps_main(Vertex2Pixel v2p) : SV_TARGET\n"
"{\n"
"  return v2p.color;\n"
"}\n"
""
);

read_only global String8 r_d3d11_g_geo3dcomposite_shader_src =
str8_lit_comp(
""
"\n"
"struct CPU2Vertex\n"
"{\n"
"  uint vertex_id         : SV_VertexID;\n"
"};\n"
"\n"
"struct Vertex2Pixel\n"
"{\n"
"  float4 position        : SV_POSITION;\n"
"  float2 texcoord        : TEX;\n"
"};\n"
"\n"
"Texture2D    stage_t2d     : register(t0);\n"
"SamplerState stage_sampler : register(s0);\n"
"\n"
"//- rjf: vertex shader\n"
"\n"
"Vertex2Pixel\n"
"vs_main(CPU2Vertex c2v)\n"
"{\n"
"  float4 vertex_positions__modl[] =\n"
"  {\n"
"    float4(0, 0, 0, 1),\n"
"    float4(0, 1, 0, 1),\n"
"    float4(1, 0, 0, 1),\n"
"    float4(1, 1, 0, 1),\n"
"  };\n"
"  float4 vertex_position__modl = vertex_positions__modl[c2v.vertex_id];\n"
"  float4 vertex_position__clip = float4(2*vertex_position__modl.x - 1, 2*vertex_position__modl.y - 1, 0, 1);\n"
"  float2 texcoord              = float2(vertex_position__modl.x, vertex_position__modl.y);\n"
"  texcoord.y = 1-texcoord.y;\n"
"  Vertex2Pixel v2p;\n"
"  {\n"
"    v2p.position = vertex_position__clip;\n"
"    v2p.texcoord = texcoord;\n"
"  }\n"
"  return v2p;\n"
"}\n"
"\n"
"//- rjf: pixel shader\n"
"\n"
"float4\n"
"ps_main(Vertex2Pixel v2p) : SV_TARGET\n"
"{\n"
"  float4 final_color = stage_t2d.Sample(stage_sampler, v2p.texcoord);\n"
"  return final_color;\n"
"}\n"
""
);

read_only global String8 r_d3d11_g_finalize_shader_src =
str8_lit_comp(
""
"\n"
"struct CPU2Vertex\n"
"{\n"
"  uint vertex_id         : SV_VertexID;\n"
"};\n"
"\n"
"struct Vertex2Pixel\n"
"{\n"
"  float4 position        : SV_POSITION;\n"
"  float2 texcoord        : TEX;\n"
"};\n"
"\n"
"Texture2D    stage_t2d     : register(t0);\n"
"SamplerState stage_sampler : register(s0);\n"
"\n"
"//- rjf: vertex shader\n"
"\n"
"Vertex2Pixel\n"
"vs_main(CPU2Vertex c2v)\n"
"{\n"
"  float4 vertex_positions__modl[] =\n"
"  {\n"
"    float4(0, 0, 0, 1),\n"
"    float4(0, 1, 0, 1),\n"
"    float4(1, 0, 0, 1),\n"
"    float4(1, 1, 0, 1),\n"
"  };\n"
"  float4 vertex_position__modl = vertex_positions__modl[c2v.vertex_id];\n"
"  float4 vertex_position__clip = float4(2*vertex_position__modl.x - 1, 2*vertex_position__modl.y - 1, 0, 1);\n"
"  float2 texcoord              = float2(vertex_position__modl.x, vertex_position__modl.y);\n"
"  texcoord.y = 1-texcoord.y;\n"
"  Vertex2Pixel v2p;\n"
"  {\n"
"    v2p.position = vertex_position__clip;\n"
"    v2p.texcoord = texcoord;\n"
"  }\n"
"  return v2p;\n"
"}\n"
"\n"
"//- rjf: pixel shader\n"
"\n"
"float4\n"
"ps_main(Vertex2Pixel v2p) : SV_TARGET\n"
"{\n"
"  float4 final_color = stage_t2d.Sample(stage_sampler, v2p.texcoord);\n"
"  final_color.a = 1;\n"
"  return final_color;\n"
"}\n"
""
);


C_LINKAGE_END

#endif // RENDER_D3D11_META_H
