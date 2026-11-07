#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 23 "rect.slang"
Texture2D<float4 > t_color_0 : register(t0);


#line 24
SamplerState s_color_0 : register(s0);


#line 10
struct RectUniforms_0
{
    float2 viewport_size_0;
    float opacity_0;
    float sample_is_surface_0;
    float4  texture_sample_channel_map_0[int(4)];
    float2 texture_size_0;
    float2 xform_scale_0;
    float4  xform_0[int(3)];
    float surface_flip_y_0;
};

cbuffer u_0 : register(b0)
{
    RectUniforms_0 u_0;
}

#line 9 "common.slang"
float linear_from_srgb_0(float x_0)
{

#line 9
    float _S1;

    if(x_0 < 0.04044823721051216f)
    {

#line 11
        _S1 = x_0 / 12.92000007629394531f;

#line 11
    }
    else
    {

#line 11
        _S1 = pow((x_0 + 0.05499999970197678f) / 1.0549999475479126f, 2.40000009536743164f);

#line 11
    }

#line 11
    return _S1;
}

float4 linear_from_srgb_1(float4 v_0)
{
    return float4(linear_from_srgb_0(v_0.x), linear_from_srgb_0(v_0.y), linear_from_srgb_0(v_0.z), v_0.w);
}


#line 4
float rect_sdf_0(float2 sample_pos_0, float2 rect_half_size_0, float r_0)
{
    return length(max(abs(sample_pos_0) - rect_half_size_0 + r_0, (float2)0.0f)) - r_0;
}


#line 38 "rect.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
    float2 sdf_sample_pos_0 : TEXCOORD1;
    float4 tint_0 : TEXCOORD2;
    float corner_radius_0 : TEXCOORD3;
    nointerpolation float2 rect_half_size_1 : TEXCOORD4;
    nointerpolation float border_thickness_0 : TEXCOORD5;
    nointerpolation float softness_0 : TEXCOORD6;
    nointerpolation float omit_texture_0 : TEXCOORD7;
};


#line 86
float4 fs_main(V2P_0 in_0) : SV_TARGET
{
    float4 albedo_0 = (float4)1.0f;
    if((in_0.omit_texture_0) < 0.5f)
    {
        float4 s_0 = t_color_0.Sample(s_color_0, in_0.texcoord_0);

        float4 raw_0 = u_0.texture_sample_channel_map_0[int(0)] * s_0.x + u_0.texture_sample_channel_map_0[int(1)] * s_0.y + u_0.texture_sample_channel_map_0[int(2)] * s_0.z + u_0.texture_sample_channel_map_0[int(3)] * s_0.w;
        if((u_0.sample_is_surface_0) > 0.5f)
        {


            albedo_0 = raw_0;
            if((raw_0.w) > 0.00009999999747379f)
            {
                albedo_0.xyz = albedo_0.xyz / albedo_0.w;

#line 99
            }

#line 94
        }
        else
        {

#line 106
            albedo_0 = linear_from_srgb_1(raw_0);

#line 94
        }

#line 89
    }

#line 110
    float _S2 = max(2.0f * in_0.softness_0, 0.00100000004749745f);

#line 110
    float border_t_0;

    if((in_0.border_thickness_0) > 0.0f)
    {

#line 112
        border_t_0 = smoothstep(0.0f, _S2, rect_sdf_0(in_0.sdf_sample_pos_0, in_0.rect_half_size_1 - 2.0f * in_0.softness_0 - in_0.border_thickness_0, max(in_0.corner_radius_0 - in_0.border_thickness_0, 0.0f)));

#line 112
    }
    else
    {

#line 112
        border_t_0 = 1.0f;

#line 112
    }

#line 117
    if(border_t_0 < 0.00100000004749745f)
    {
        discard;

#line 117
    }

#line 117
    bool _S3;

#line 122
    if((in_0.corner_radius_0) > 0.0f)
    {

#line 122
        _S3 = true;

#line 122
    }
    else
    {

#line 122
        _S3 = (in_0.softness_0) > 0.75f;

#line 122
    }

#line 122
    float corner_t_0;

#line 122
    if(_S3)
    {

#line 122
        corner_t_0 = 1.0f - smoothstep(0.0f, _S2, rect_sdf_0(in_0.sdf_sample_pos_0, in_0.rect_half_size_1 - 2.0f * in_0.softness_0, in_0.corner_radius_0));

#line 122
    }
    else
    {

#line 122
        corner_t_0 = 1.0f;

#line 122
    }

#line 128
    float4 result_0 = albedo_0 * in_0.tint_0;
    result_0[int(3)] = result_0[int(3)] * (u_0.opacity_0 * corner_t_0 * border_t_0);
    return result_0;
}

