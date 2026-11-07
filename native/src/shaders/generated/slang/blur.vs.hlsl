#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 9 "/home/user/wheelhouse/native/src/shaders/slang/blur.slang"
struct BlurUniforms_0
{
    float4 rect_0;
    float4 corner_radii_0;
    float2 direction_0;
    float2 viewport_size_0;
    uint blur_count_0;
    float texcoord_flip_y_0;
    float4  kernel_0[int(32)];
};

cbuffer u_0 : register(b0)
{
    BlurUniforms_0 u_0;
}

#line 27 "/home/user/wheelhouse/native/src/shaders/slang/common.slang"
float2 corner_pct_0(uint vid_0)
{

#line 27
    float _S1;

    if((vid_0 >> int(1)) != 0U)
    {

#line 29
        _S1 = 1.0f;

#line 29
    }
    else
    {

#line 29
        _S1 = 0.0f;

#line 29
    }

#line 29
    float _S2;

#line 29
    if((vid_0 & 1U) != 0U)
    {

#line 29
        _S2 = 0.0f;

#line 29
    }
    else
    {

#line 29
        _S2 = 1.0f;

#line 29
    }

#line 29
    return float2(_S1, _S2);
}


#line 20
float4 ndc_from_points_0(float2 p_0, float2 viewport_size_1)
{
    return float4(2.0f * p_0.x / viewport_size_1.x - 1.0f, 1.0f - 2.0f * p_0.y / viewport_size_1.y, 0.0f, 1.0f);
}


#line 24 "/home/user/wheelhouse/native/src/shaders/slang/blur.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
    float2 sdf_sample_pos_0 : TEXCOORD1;
    float corner_radius_0 : TEXCOORD2;
    nointerpolation float2 rect_half_size_0 : TEXCOORD3;
};


V2P_0 vs_main(uint vid_1 : SV_VertexID)
{
    float2 pct_0 = corner_pct_0(vid_1);
    float2 p_1 = float2(lerp(u_0.rect_0.x, u_0.rect_0.z, pct_0.x), lerp(u_0.rect_0.y, u_0.rect_0.w, pct_0.y));
    float  radii_0[int(4)] = { u_0.corner_radii_0.y, u_0.corner_radii_0.x, u_0.corner_radii_0.w, u_0.corner_radii_0.z };
    float2 half_size_0 = (u_0.rect_0.zw - u_0.rect_0.xy) * 0.5f;

    float2 texcoord_1 = p_1 / u_0.viewport_size_0;
    if((u_0.texcoord_flip_y_0) > 0.5f)
    {
        texcoord_1[int(1)] = 1.0f - texcoord_1.y;

#line 42
    }

#line 47
    V2P_0 out_0;
    out_0.position_0 = ndc_from_points_0(p_1, u_0.viewport_size_0);
    out_0.texcoord_0 = texcoord_1;
    out_0.sdf_sample_pos_0 = (2.0f * pct_0 - 1.0f) * half_size_0;
    out_0.corner_radius_0 = radii_0[vid_1];
    out_0.rect_half_size_0 = half_size_0 - 2.0f;
    return out_0;
}

