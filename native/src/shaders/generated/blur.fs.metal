#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 4 "common.slang"
float rect_sdf_0(float2 sample_pos_0, float2 rect_half_size_0, float r_0)
{
    return length(max(abs(sample_pos_0) - rect_half_size_0 + float2(r_0) , float2(0.0f) )) - r_0;
}


#line 90 "core"
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 90
struct pixelInput_0
{
    float2 texcoord_0 [[user(TEXCOORD)]];
    float2 sdf_sample_pos_0 [[user(TEXCOORD_1)]];
    float corner_radius_0 [[user(TEXCOORD_2)]];
    [[flat]] float2 rect_half_size_1 [[user(TEXCOORD_3)]];
};


#line 9 "blur.slang"
struct BlurUniforms_0
{
    float4 rect_0;
    float4 corner_radii_0;
    float2 direction_0;
    float2 viewport_size_0;
    uint blur_count_0;
    float texcoord_flip_y_0;
    array<float4, int(32)> kernel_0;
};



struct KernelContext_0
{
    BlurUniforms_0 constant* u_0;
    texture2d<float, access::sample> t_stage_0;
    sampler s_stage_0;
};


#line 57
[[fragment]] pixelOutput_0 fs_main(pixelInput_0 _S1 [[stage_in]], float4 position_0 [[position]], BlurUniforms_0 constant* u_1 [[buffer(0)]], texture2d<float, access::sample> t_stage_1 [[texture(0)]], sampler s_stage_1 [[sampler(0)]])
{

#line 57
    thread KernelContext_0 kernelContext_0;

#line 57
    (&kernelContext_0)->u_0 = u_1;

#line 57
    (&kernelContext_0)->t_stage_0 = t_stage_1;

#line 57
    (&kernelContext_0)->s_stage_0 = s_stage_1;

    float3 _S2 = float3(u_1->kernel_0[int(0)].x)  * ((t_stage_1).sample((s_stage_1), (_S1.texcoord_0))).xyz;

#line 59
    uint i_0 = 1U;

#line 59
    float3 color_0 = _S2;
    for(;;)
    {

#line 60
        if(i_0 < ((&kernelContext_0)->u_0->blur_count_0))
        {
        }
        else
        {

#line 60
            break;
        }

        float2 offset_0 = float2(u_1->kernel_0[i_0].y)  * (&kernelContext_0)->u_0->direction_0;

#line 63
        float3 _S3 = float3(u_1->kernel_0[i_0].x) ;

        float3 color_1 = color_0 + _S3 * (((&kernelContext_0)->t_stage_0).sample(((&kernelContext_0)->s_stage_0), (_S1.texcoord_0 - offset_0))).xyz + _S3 * (((&kernelContext_0)->t_stage_0).sample(((&kernelContext_0)->s_stage_0), (_S1.texcoord_0 + offset_0))).xyz;

#line 60
        i_0 = i_0 + 1U;

#line 60
        color_0 = color_1;

#line 60
    }

#line 67
    float corner_t_0 = 1.0f - smoothstep(0.0f, 2.0f, rect_sdf_0(_S1.sdf_sample_pos_0, _S1.rect_half_size_1, _S1.corner_radius_0));

#line 67
    bool _S4;
    if(((&kernelContext_0)->u_0->direction_0.y) != 0.0f)
    {

#line 68
        _S4 = corner_t_0 < 0.89999997615814209f;

#line 68
    }
    else
    {

#line 68
        _S4 = false;

#line 68
    }

#line 68
    if(_S4)
    {
        discard_fragment();

#line 68
    }

#line 68
    pixelOutput_0 _S5 = { float4(color_0, 1.0f) };



    return _S5;
}

