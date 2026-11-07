#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 27 "common.slang"
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
    float4 tint_0 [[user(TEXCOORD_2)]];
    float corner_radius_0 [[user(TEXCOORD_3)]];
    float2 rect_half_size_0 [[user(TEXCOORD_4)]];
    float border_thickness_0 [[user(TEXCOORD_5)]];
    float softness_0 [[user(TEXCOORD_6)]];
    float omit_texture_0 [[user(TEXCOORD_7)]];
};


#line 90
struct vertexInput_0
{
    float4 dst_rect_0 [[attribute(0)]];
    float4 src_rect_0 [[attribute(1)]];
    float4 color00_0 [[attribute(2)]];
    float4 color01_0 [[attribute(3)]];
    float4 color10_0 [[attribute(4)]];
    float4 color11_0 [[attribute(5)]];
    float4 corner_radii_0 [[attribute(6)]];
    float4 style_0 [[attribute(7)]];
};


#line 10 "rect.slang"
struct RectUniforms_0
{
    float2 viewport_size_1;
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
    RectUniforms_0 constant* u_0;
};


#line 38
struct V2P_0
{
    float4 position_1;
    float2 texcoord_1;
    float2 sdf_sample_pos_1;
    float4 tint_1;
    float corner_radius_1;
    [[flat]] float2 rect_half_size_1;
    [[flat]] float border_thickness_1;
    [[flat]] float softness_1;
    [[flat]] float omit_texture_1;
};


#line 38
[[vertex]] vs_main_Result_0 vs_main(vertexInput_0 _S3 [[stage_in]], uint vid_1 [[vertex_id]], RectUniforms_0 constant* u_1 [[buffer(0)]])
{

#line 38
    thread KernelContext_0 kernelContext_0;

#line 38
    (&kernelContext_0)->u_0 = u_1;

#line 54
    float2 pct_0 = corner_pct_0(vid_1);
    float _S4 = pct_0.x;

#line 55
    float shear_0;

#line 55
    if(_S4 > 0.5f)
    {

#line 55
        shear_0 = _S3.style_0.w;

#line 55
    }
    else
    {

#line 55
        shear_0 = 0.0f;

#line 55
    }
    float _S5 = pct_0.y;

    array<float, int(4)> radii_0 = { { _S3.corner_radii_0.y, _S3.corner_radii_0.x, _S3.corner_radii_0.w, _S3.corner_radii_0.z } };
    array<float4, int(4)> colors_0 = { { _S3.color01_0, _S3.color00_0, _S3.color11_0, _S3.color10_0 } };

    float3 p_1 = float3(float2(mix(_S3.dst_rect_0.x, _S3.dst_rect_0.z, _S4), mix(_S3.dst_rect_0.y, _S3.dst_rect_0.w, _S5) + shear_0), 1.0f);
    float2 xformed_0 = float2(dot((&kernelContext_0)->u_0->xform_0[int(0)].xyz, p_1), dot((&kernelContext_0)->u_0->xform_0[int(1)].xyz, p_1));
    float _S6 = max((&kernelContext_0)->u_0->xform_scale_0.x, (&kernelContext_0)->u_0->xform_scale_0.y);
    float2 half_size_0 = abs(_S3.dst_rect_0.zw - _S3.dst_rect_0.xy) * float2(0.5f)  * (&kernelContext_0)->u_0->xform_scale_0;

    thread float2 texcoord_2 = float2(mix(_S3.src_rect_0.x, _S3.src_rect_0.z, _S4), mix(_S3.src_rect_0.y, _S3.src_rect_0.w, _S5)) / (&kernelContext_0)->u_0->texture_size_0;

#line 66
    bool _S7;
    if(((&kernelContext_0)->u_0->sample_is_surface_0) > 0.5f)
    {

#line 67
        _S7 = ((&kernelContext_0)->u_0->surface_flip_y_0) > 0.5f;

#line 67
    }
    else
    {

#line 67
        _S7 = false;

#line 67
    }

#line 67
    if(_S7)
    {
        texcoord_2.y = 1.0f - texcoord_2.y;

#line 67
    }

#line 72
    thread V2P_0 out_0;
    (&out_0)->position_1 = ndc_from_points_0(xformed_0, (&kernelContext_0)->u_0->viewport_size_1);
    (&out_0)->texcoord_1 = texcoord_2;
    (&out_0)->sdf_sample_pos_1 = (float2(2.0f)  * pct_0 - float2(1.0f) ) * half_size_0;
    (&out_0)->tint_1 = colors_0[vid_1];
    (&out_0)->corner_radius_1 = radii_0[vid_1] * _S6;
    (&out_0)->rect_half_size_1 = half_size_0;
    (&out_0)->border_thickness_1 = _S3.style_0.x * _S6;
    (&out_0)->softness_1 = _S3.style_0.y * _S6;
    (&out_0)->omit_texture_1 = _S3.style_0.z;
    V2P_0 _S8 = out_0;

#line 82
    thread vs_main_Result_0 _S9;

#line 82
    (&_S9)->position_0 = _S8.position_1;

#line 82
    (&_S9)->texcoord_0 = _S8.texcoord_1;

#line 82
    (&_S9)->sdf_sample_pos_0 = _S8.sdf_sample_pos_1;

#line 82
    (&_S9)->tint_0 = _S8.tint_1;

#line 82
    (&_S9)->corner_radius_0 = _S8.corner_radius_1;

#line 82
    (&_S9)->rect_half_size_0 = _S8.rect_half_size_1;

#line 82
    (&_S9)->border_thickness_0 = _S8.border_thickness_1;

#line 82
    (&_S9)->softness_0 = _S8.softness_1;

#line 82
    (&_S9)->omit_texture_0 = _S8.omit_texture_1;

#line 82
    return _S9;
}

