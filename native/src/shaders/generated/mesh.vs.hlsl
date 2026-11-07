#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 7 "mesh.slang"
struct MeshUniforms_0
{
    float4  view_projection_0[int(4)];
};


#line 19
cbuffer u_0 : register(b0)
{
    MeshUniforms_0 u_0;
}

#line 48
float4 transform_0(float4 c0_0, float4 c1_0, float4 c2_0, float4 c3_0, float4 v_0)
{
    return v_0.x * c0_0 + v_0.y * c1_0 + v_0.z * c2_0 + v_0.w * c3_0;
}


#line 41
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
    float4 color_0 : TEXCOORD1;
};


#line 24
struct Vertex_0
{
    float3 position_1 : POS;
    float3 normal_0 : NOR;
    float2 texcoord_1 : TEX;
    float3 color_1 : COL;
};

struct Instance_0
{
    float4 xform0_0 : XFORM0;
    float4 xform1_0 : XFORM1;
    float4 xform2_0 : XFORM2;
    float4 xform3_0 : XFORM3;
    float4 albedo_remap_0 : REMAP;
};


#line 54
V2P_0 vs_main(Vertex_0 vtx_0, Instance_0 inst_0)
{

    V2P_0 out_0;
    out_0.position_0 = transform_0(u_0.view_projection_0[int(0)], u_0.view_projection_0[int(1)], u_0.view_projection_0[int(2)], u_0.view_projection_0[int(3)], transform_0(inst_0.xform0_0, inst_0.xform1_0, inst_0.xform2_0, inst_0.xform3_0, float4(vtx_0.position_1, 1.0f)));
    out_0.texcoord_0 = inst_0.albedo_remap_0.xy + vtx_0.texcoord_1 * inst_0.albedo_remap_0.zw;
    out_0.color_0 = float4(vtx_0.color_1, 1.0f);
    return out_0;
}

