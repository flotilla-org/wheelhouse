#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 25 "effect_prelude.slang"
struct EffectUniforms_0
{
    float2 source_size_px_0;
    float2 output_size_px_0;
    float4 params0_0;
    float4 params1_0;
};



struct KernelContext_0
{
    EffectUniforms_0 constant* u_0;
    texture2d<float, access::sample> t_source_0;
    sampler s_source_0;
};


#line 55
float4 src_0(float2 uv_0, KernelContext_0 thread* kernelContext_0)
{
    return ((kernelContext_0->t_source_0).sample((kernelContext_0->s_source_0), (uv_0)));
}

float4 src_at_0(float2 uv_1, float2 offset_px_0, KernelContext_0 thread* kernelContext_1)
{
    return ((kernelContext_1->t_source_0).sample((kernelContext_1->s_source_0), (uv_1 + offset_px_0 / kernelContext_1->u_0->source_size_px_0)));
}


#line 14 "crt.slang"
float4 effect_0(float2 uv01_0, float2 frag_px_0, KernelContext_0 thread* kernelContext_2)
{
    float scanline_intensity_0 = kernelContext_2->u_0->params0_0.x;

    float vignette_strength_0 = kernelContext_2->u_0->params0_0.z;
    float mask_strength_0 = kernelContext_2->u_0->params0_0.w;
    float rgb_shift_px_0 = kernelContext_2->u_0->params1_0.x;
    float brightness_0 = kernelContext_2->u_0->params1_0.y;


    float2 centered_0 = uv01_0 - float2(0.5f, 0.5f);
    float r2_0 = dot(centered_0, centered_0);
    float2 uv_2 = uv01_0 + centered_0 * float2((r2_0 * kernelContext_2->u_0->params0_0.y * 2.0f)) ;
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
        return float4(0.0f) ;
    }

#line 29
    float4 _S3 = src_0(uv_2, kernelContext_2);



    thread float4 color_0 = _S3;
    if(rgb_shift_px_0 > 0.00100000004749745f)
    {

#line 34
        float4 _S4 = src_at_0(uv_2, float2(rgb_shift_px_0, 0.0f), kernelContext_2);

        color_0.x = _S4.x;

#line 36
        float4 _S5 = src_at_0(uv_2, float2(- rgb_shift_px_0, 0.0f), kernelContext_2);
        color_0.z = _S5.z;

#line 34
    }

#line 42
    float s_0 = sin(uv_2.y * kernelContext_2->u_0->source_size_px_0.y * 3.14159274101257324f);
    float scan_0 = 1.0f - scanline_intensity_0 * (0.34999999403953552f + 0.64999997615814209f * s_0 * s_0);


    uint mask_sel_0 = uint(frag_px_0.x) % 3U;
    thread float3 mask_mul_0 = float3((1.0f - mask_strength_0 * 0.44999998807907104f)) ;
    if(mask_sel_0 == 0U)
    {

#line 48
        mask_mul_0.x = 1.0f;

#line 48
    }
    else
    {

#line 49
        if(mask_sel_0 == 1U)
        {

#line 49
            mask_mul_0.y = 1.0f;

#line 49
        }
        else
        {

#line 50
            mask_mul_0.z = 1.0f;

#line 49
        }

#line 48
    }

#line 53
    float _S6 = max(1.0f - vignette_strength_0 * r2_0 * 1.5f, 0.0f);

#line 59
    return float4(color_0.xyz * mask_mul_0 * float3(brightness_0)  * float3(scan_0)  * float3(_S6) , color_0.w * scan_0 * _S6);
}


#line 59
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 59
struct pixelInput_0
{
    float2 uv_3 [[user(TEXCOORD)]];
};


#line 66 "effect_prelude.slang"
[[fragment]] pixelOutput_0 fs_main(pixelInput_0 _S7 [[stage_in]], float4 position_0 [[position]], EffectUniforms_0 constant* u_1 [[buffer(0)]], texture2d<float, access::sample> t_source_1 [[texture(0)]], sampler s_source_1 [[sampler(0)]])
{

#line 66
    thread KernelContext_0 kernelContext_3;

#line 66
    (&kernelContext_3)->u_0 = u_1;

#line 66
    (&kernelContext_3)->t_source_0 = t_source_1;

#line 66
    (&kernelContext_3)->s_source_0 = s_source_1;

#line 66
    float4 _S8 = effect_0(_S7.uv_3, position_0.xy, &kernelContext_3);

#line 66
    pixelOutput_0 _S9 = { _S8 };

    return _S9;
}

