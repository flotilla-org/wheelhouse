#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 12 "mesh.slang"
struct MeshGroupUniforms_0
{
    float has_albedo_0;
    float albedo_sample_is_surface_0;
    float2 _padding0_0;
};


cbuffer g_0 : register(b1)
{
    MeshGroupUniforms_0 g_0;
}

#line 21
Texture2D<float4 > t_albedo_0 : register(t0);


#line 22
SamplerState s_albedo_0 : register(s0);


#line 41
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
    float4 color_0 : TEXCOORD1;
};


#line 65
float4 fs_main(V2P_0 in_0) : SV_TARGET
{
    float4 albedo_0 = (float4)1.0f;
    if((g_0.has_albedo_0) > 0.5f)
    {
        albedo_0 = t_albedo_0.Sample(s_albedo_0, in_0.texcoord_0);

#line 70
        bool _S1;


        if((g_0.albedo_sample_is_surface_0) > 0.5f)
        {

#line 73
            _S1 = (albedo_0.w) > 0.00009999999747379f;

#line 73
        }
        else
        {

#line 73
            _S1 = false;

#line 73
        }

#line 73
        if(_S1)
        {
            albedo_0.xyz = albedo_0.xyz / albedo_0.w;

#line 73
        }

#line 68
    }

#line 78
    return albedo_0 * in_0.color_0;
}

