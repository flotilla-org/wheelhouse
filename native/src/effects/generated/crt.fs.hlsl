#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 25 "effect_prelude.slang"
struct EffectUniforms_0
{
    float2 source_size_px_0;
    float2 output_size_px_0;
    float4 params0_0;
    float4 params1_0;
};

cbuffer u_0 : register(b0)
{
    EffectUniforms_0 u_0;
}

#line 34
Texture2D<float4 > t_source_0 : register(t0);


#line 35
SamplerState s_source_0 : register(s0);


#line 57
float4 src_0(float2 uv_0)
{
    return t_source_0.Sample(s_source_0, uv_0);
}

float4 src_at_0(float2 uv_1, float2 offset_px_0)
{
    return t_source_0.Sample(s_source_0, uv_1 + offset_px_0 / u_0.source_size_px_0);
}


#line 14 "crt.slang"
float4 effect_0(float2 uv01_0, float2 frag_px_0)
{
    float scanline_intensity_0 = u_0.params0_0.x;

    float vignette_strength_0 = u_0.params0_0.z;
    float mask_strength_0 = u_0.params0_0.w;
    float rgb_shift_px_0 = u_0.params1_0.x;
    float brightness_0 = u_0.params1_0.y;


    float2 centered_0 = uv01_0 - float2(0.5f, 0.5f);
    float r2_0 = dot(centered_0, centered_0);
    float2 uv_2 = uv01_0 + centered_0 * (r2_0 * u_0.params0_0.y * 2.0f);
    float _S1 = uv_2.x;

#line 27
    bool _S2;

#line 27
    if(_S1 < 0.0f)
    {

#line 27
        _S2 = true;

#line 27
    }
    else
    {

#line 27
        _S2 = _S1 > 1.0f;

#line 27
    }

#line 27
    if(_S2)
    {

#line 27
        _S2 = true;

#line 27
    }
    else
    {

#line 27
        _S2 = (uv_2.y) < 0.0f;

#line 27
    }

#line 27
    if(_S2)
    {

#line 27
        _S2 = true;

#line 27
    }
    else
    {

#line 27
        _S2 = (uv_2.y) > 1.0f;

#line 27
    }

#line 27
    if(_S2)
    {
        return (float4)0.0f;
    }


    float4 color_0 = src_0(uv_2);
    if(rgb_shift_px_0 > 0.00100000004749745f)
    {
        color_0[int(0)] = src_at_0(uv_2, float2(rgb_shift_px_0, 0.0f)).x;
        color_0[int(2)] = src_at_0(uv_2, float2(- rgb_shift_px_0, 0.0f)).z;

#line 34
    }

#line 42
    float s_0 = sin(uv_2.y * u_0.source_size_px_0.y * 3.14159274101257324f);
    float scan_0 = 1.0f - scanline_intensity_0 * (0.34999999403953552f + 0.64999997615814209f * s_0 * s_0);


    uint mask_sel_0 = uint(frag_px_0.x) % 3U;
    float3 mask_mul_0 = (float3)(1.0f - mask_strength_0 * 0.44999998807907104f);
    if(mask_sel_0 == 0U)
    {

#line 48
        mask_mul_0[int(0)] = 1.0f;

#line 48
    }
    else
    {

#line 49
        if(mask_sel_0 == 1U)
        {

#line 49
            mask_mul_0[int(1)] = 1.0f;

#line 49
        }
        else
        {

#line 50
            mask_mul_0[int(2)] = 1.0f;

#line 49
        }

#line 48
    }

#line 53
    float _S3 = max(1.0f - vignette_strength_0 * r2_0 * 1.5f, 0.0f);

#line 59
    return float4(color_0.xyz * mask_mul_0 * brightness_0 * scan_0 * _S3, color_0.w * scan_0 * _S3);
}


#line 37 "effect_prelude.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 uv_3 : TEXCOORD0;
};


#line 68
float4 fs_main(V2P_0 in_0) : SV_TARGET
{
    return effect_0(in_0.uv_3, in_0.position_0.xy);
}

