#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 27 "/home/user/wheelhouse/native/src/shaders/slang/common.slang"
float2 corner_pct_0(uint vid_0)
{

#line 27
    float _S1;

    if((vid_0 >> 1U) != 0U)
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
float4 ndc_from_points_0(float2 p_0, float2 viewport_size_0)
{
    return float4(2.0f * p_0.x / viewport_size_0.x - 1.0f, 1.0f - 2.0f * p_0.y / viewport_size_0.y, 0.0f, 1.0f);
}


#line 90 "core"
struct vs_main_Result_0
{
    float4 position_0 [[position]];
    float2 texcoord_0 [[user(TEXCOORD)]];
    float2 sdf_sample_pos_0 [[user(TEXCOORD_1)]];
    float corner_radius_0 [[user(TEXCOORD_2)]];
    float2 rect_half_size_0 [[user(TEXCOORD_3)]];
};


#line 9 "/home/user/wheelhouse/native/src/shaders/slang/blur.slang"
struct BlurUniforms_0
{
    float4 rect_0;
    float4 corner_radii_0;
    float2 direction_0;
    float2 viewport_size_1;
    uint blur_count_0;
    float texcoord_flip_y_0;
    array<float4, int(32)> kernel_0;
};


#line 24
struct V2P_0
{
    float4 position_1;
    float2 texcoord_1;
    float2 sdf_sample_pos_1;
    float corner_radius_1;
    [[flat]] float2 rect_half_size_1;
};


#line 24
[[vertex]] vs_main_Result_0 vs_main(uint vid_1 [[vertex_id]], BlurUniforms_0 constant* u_0 [[buffer(0)]])
{

#line 36
    float2 pct_0 = corner_pct_0(vid_1);
    float2 p_1 = float2(mix(u_0->rect_0.x, u_0->rect_0.z, pct_0.x), mix(u_0->rect_0.y, u_0->rect_0.w, pct_0.y));
    array<float, int(4)> radii_0 = { { u_0->corner_radii_0.y, u_0->corner_radii_0.x, u_0->corner_radii_0.w, u_0->corner_radii_0.z } };
    float2 half_size_0 = (u_0->rect_0.zw - u_0->rect_0.xy) * float2(0.5f) ;

    thread float2 texcoord_2 = p_1 / u_0->viewport_size_1;
    if((u_0->texcoord_flip_y_0) > 0.5f)
    {
        texcoord_2.y = 1.0f - texcoord_2.y;

#line 42
    }

#line 47
    thread V2P_0 out_0;
    (&out_0)->position_1 = ndc_from_points_0(p_1, u_0->viewport_size_1);
    (&out_0)->texcoord_1 = texcoord_2;

#line 49
    float2 _S3 = float2(2.0f) ;
    (&out_0)->sdf_sample_pos_1 = (_S3 * pct_0 - float2(1.0f) ) * half_size_0;
    (&out_0)->corner_radius_1 = radii_0[vid_1];
    (&out_0)->rect_half_size_1 = half_size_0 - _S3;
    V2P_0 _S4 = out_0;

#line 53
    thread vs_main_Result_0 _S5;

#line 53
    (&_S5)->position_0 = _S4.position_1;

#line 53
    (&_S5)->texcoord_0 = _S4.texcoord_1;

#line 53
    (&_S5)->sdf_sample_pos_0 = _S4.sdf_sample_pos_1;

#line 53
    (&_S5)->corner_radius_0 = _S4.corner_radius_1;

#line 53
    (&_S5)->rect_half_size_0 = _S4.rect_half_size_1;

#line 53
    return _S5;
}

