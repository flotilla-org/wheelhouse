#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

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
    float4 tint_0 [[user(TEXCOORD_2)]];
    float corner_radius_0 [[user(TEXCOORD_3)]];
    [[flat]] float2 rect_half_size_1 [[user(TEXCOORD_4)]];
    [[flat]] float border_thickness_0 [[user(TEXCOORD_5)]];
    [[flat]] float softness_0 [[user(TEXCOORD_6)]];
    [[flat]] float omit_texture_0 [[user(TEXCOORD_7)]];
};


#line 10 "rect.slang"
struct RectUniforms_0
{
    float2 viewport_size_0;
    float opacity_0;
    float sample_is_surface_0;
    array<float4, int(4)> texture_sample_channel_map_0;
    float2 texture_size_0;
    float2 xform_scale_0;
    array<float4, int(3)> xform_0;
    float surface_flip_y_0;
};

struct KernelContext_0
{
    texture2d<float, access::sample> t_color_0;
    sampler s_color_0;
    RectUniforms_0 constant* u_0;
};


#line 86
[[fragment]] pixelOutput_0 fs_main(pixelInput_0 _S2 [[stage_in]], float4 position_0 [[position]], texture2d<float, access::sample> t_color_1 [[texture(0)]], sampler s_color_1 [[sampler(0)]], RectUniforms_0 constant* u_1 [[buffer(0)]])
{

#line 86
    thread KernelContext_0 kernelContext_0;

#line 86
    (&kernelContext_0)->t_color_0 = t_color_1;

#line 86
    (&kernelContext_0)->s_color_0 = s_color_1;

#line 86
    (&kernelContext_0)->u_0 = u_1;

    thread float4 albedo_0 = float4(1.0f) ;
    if((_S2.omit_texture_0) < 0.5f)
    {
        float4 s_0 = (((&kernelContext_0)->t_color_0).sample(((&kernelContext_0)->s_color_0), (_S2.texcoord_0)));

        float4 raw_0 = (&kernelContext_0)->u_0->texture_sample_channel_map_0[int(0)] * float4(s_0.x)  + (&kernelContext_0)->u_0->texture_sample_channel_map_0[int(1)] * float4(s_0.y)  + (&kernelContext_0)->u_0->texture_sample_channel_map_0[int(2)] * float4(s_0.z)  + (&kernelContext_0)->u_0->texture_sample_channel_map_0[int(3)] * float4(s_0.w) ;
        if(((&kernelContext_0)->u_0->sample_is_surface_0) > 0.5f)
        {


            albedo_0 = raw_0;
            if((raw_0.w) > 0.00009999999747379f)
            {
                albedo_0.xyz = albedo_0.xyz / float3(albedo_0.w) ;

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
    float _S3 = 2.0f * _S2.softness_0;

#line 110
    float _S4 = max(_S3, 0.00100000004749745f);

#line 110
    float border_t_0;

    if((_S2.border_thickness_0) > 0.0f)
    {

#line 112
        border_t_0 = smoothstep(0.0f, _S4, rect_sdf_0(_S2.sdf_sample_pos_0, _S2.rect_half_size_1 - float2(_S3)  - float2(_S2.border_thickness_0) , max(_S2.corner_radius_0 - _S2.border_thickness_0, 0.0f)));

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
        discard_fragment();

#line 117
    }

#line 117
    bool _S5;

#line 122
    if((_S2.corner_radius_0) > 0.0f)
    {

#line 122
        _S5 = true;

#line 122
    }
    else
    {

#line 122
        _S5 = (_S2.softness_0) > 0.75f;

#line 122
    }

#line 122
    float corner_t_0;

#line 122
    if(_S5)
    {

#line 122
        corner_t_0 = 1.0f - smoothstep(0.0f, _S4, rect_sdf_0(_S2.sdf_sample_pos_0, _S2.rect_half_size_1 - float2(_S3) , _S2.corner_radius_0));

#line 122
    }
    else
    {

#line 122
        corner_t_0 = 1.0f;

#line 122
    }

#line 128
    thread float4 result_0 = albedo_0 * _S2.tint_0;
    result_0.w = result_0.w * ((&kernelContext_0)->u_0->opacity_0 * corner_t_0 * border_t_0);

#line 129
    pixelOutput_0 _S6 = { result_0 };
    return _S6;
}

