#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 90 "core"
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 90
struct pixelInput_0
{
    float2 texcoord_0 [[user(TEXCOORD)]];
};


#line 15 "composite.slang"
struct KernelContext_0
{
    texture2d<float, access::sample> t_color_0;
    sampler s_color_0;
};


#line 34
[[fragment]] pixelOutput_0 fs_main(pixelInput_0 _S1 [[stage_in]], float4 position_0 [[position]], texture2d<float, access::sample> t_color_1 [[texture(0)]], sampler s_color_1 [[sampler(0)]])
{

#line 34
    thread KernelContext_0 kernelContext_0;

#line 34
    (&kernelContext_0)->t_color_0 = t_color_1;

#line 34
    (&kernelContext_0)->s_color_0 = s_color_1;

#line 34
    pixelOutput_0 _S2 = { ((t_color_1).sample((s_color_1), (_S1.texcoord_0))) };

    return _S2;
}

