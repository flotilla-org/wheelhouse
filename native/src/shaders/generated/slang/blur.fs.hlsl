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

#line 21
Texture2D<float4 > t_stage_0 : register(t0);


#line 22
SamplerState s_stage_0 : register(s0);


#line 4 "/home/user/wheelhouse/native/src/shaders/slang/common.slang"
float rect_sdf_0(float2 sample_pos_0, float2 rect_half_size_0, float r_0)
{
    return length(max(abs(sample_pos_0) - rect_half_size_0 + r_0, (float2)0.0f)) - r_0;
}


#line 24 "/home/user/wheelhouse/native/src/shaders/slang/blur.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
    float2 sdf_sample_pos_0 : TEXCOORD1;
    float corner_radius_0 : TEXCOORD2;
    nointerpolation float2 rect_half_size_1 : TEXCOORD3;
};


#line 57
float4 fs_main(V2P_0 in_0) : SV_TARGET
{
    float3 _S1 = u_0.kernel_0[int(0)].x * t_stage_0.Sample(s_stage_0, in_0.texcoord_0).xyz;

#line 59
    uint i_0 = 1U;

#line 59
    float3 color_0 = _S1;
    for(;;)
    {

#line 60
        if(i_0 < (u_0.blur_count_0))
        {
        }
        else
        {

#line 60
            break;
        }
        float weight_0 = u_0.kernel_0[i_0].x;
        float2 offset_0 = u_0.kernel_0[i_0].y * u_0.direction_0;

        float3 color_1 = color_0 + weight_0 * t_stage_0.Sample(s_stage_0, in_0.texcoord_0 - offset_0).xyz + weight_0 * t_stage_0.Sample(s_stage_0, in_0.texcoord_0 + offset_0).xyz;

#line 60
        i_0 = i_0 + 1U;

#line 60
        color_0 = color_1;

#line 60
    }

#line 67
    float corner_t_0 = 1.0f - smoothstep(0.0f, 2.0f, rect_sdf_0(in_0.sdf_sample_pos_0, in_0.rect_half_size_1, in_0.corner_radius_0));

#line 67
    bool _S2;
    if((u_0.direction_0.y) != 0.0f)
    {

#line 68
        _S2 = corner_t_0 < 0.89999997615814209f;

#line 68
    }
    else
    {

#line 68
        _S2 = false;

#line 68
    }

#line 68
    if(_S2)
    {
        discard;

#line 68
    }



    return float4(color_0, 1.0f);
}

