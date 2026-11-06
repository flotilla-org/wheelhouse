#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 10 "/home/user/wheelhouse/native/src/shaders/slang/rect.slang"
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


#line 38 "/home/user/wheelhouse/native/src/shaders/slang/rect.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
    float2 sdf_sample_pos_0 : TEXCOORD1;
    float4 tint_0 : TEXCOORD2;
    float corner_radius_0 : TEXCOORD3;
    nointerpolation float2 rect_half_size_0 : TEXCOORD4;
    nointerpolation float border_thickness_0 : TEXCOORD5;
    nointerpolation float softness_0 : TEXCOORD6;
    nointerpolation float omit_texture_0 : TEXCOORD7;
};


#line 26
struct Instance_0
{
    float4 dst_rect_0 : POS;
    float4 src_rect_0 : TEX;
    float4 color00_0 : COL0;
    float4 color01_0 : COL1;
    float4 color10_0 : COL2;
    float4 color11_0 : COL3;
    float4 corner_radii_0 : CRAD;
    float4 style_0 : STY;
};


#line 52
V2P_0 vs_main(uint vid_1 : SV_VertexID, Instance_0 inst_0)
{
    float2 pct_0 = corner_pct_0(vid_1);
    float _S3 = pct_0.x;

#line 55
    float shear_0;

#line 55
    if(_S3 > 0.5f)
    {

#line 55
        shear_0 = inst_0.style_0.w;

#line 55
    }
    else
    {

#line 55
        shear_0 = 0.0f;

#line 55
    }
    float _S4 = pct_0.y;

    float  radii_0[int(4)] = { inst_0.corner_radii_0.y, inst_0.corner_radii_0.x, inst_0.corner_radii_0.w, inst_0.corner_radii_0.z };
    float4  colors_0[int(4)] = { inst_0.color01_0, inst_0.color00_0, inst_0.color11_0, inst_0.color10_0 };

    float3 p_1 = float3(float2(lerp(inst_0.dst_rect_0.x, inst_0.dst_rect_0.z, _S3), lerp(inst_0.dst_rect_0.y, inst_0.dst_rect_0.w, _S4) + shear_0), 1.0f);
    float2 xformed_0 = float2(dot(u_0.xform_0[int(0)].xyz, p_1), dot(u_0.xform_0[int(1)].xyz, p_1));
    float _S5 = max(u_0.xform_scale_0.x, u_0.xform_scale_0.y);
    float2 half_size_0 = abs(inst_0.dst_rect_0.zw - inst_0.dst_rect_0.xy) * 0.5f * u_0.xform_scale_0;

    float2 texcoord_1 = float2(lerp(inst_0.src_rect_0.x, inst_0.src_rect_0.z, _S3), lerp(inst_0.src_rect_0.y, inst_0.src_rect_0.w, _S4)) / u_0.texture_size_0;

#line 66
    bool _S6;
    if((u_0.sample_is_surface_0) > 0.5f)
    {

#line 67
        _S6 = (u_0.surface_flip_y_0) > 0.5f;

#line 67
    }
    else
    {

#line 67
        _S6 = false;

#line 67
    }

#line 67
    if(_S6)
    {
        texcoord_1[int(1)] = 1.0f - texcoord_1.y;

#line 67
    }

#line 72
    V2P_0 out_0;
    out_0.position_0 = ndc_from_points_0(xformed_0, u_0.viewport_size_0);
    out_0.texcoord_0 = texcoord_1;
    out_0.sdf_sample_pos_0 = (2.0f * pct_0 - 1.0f) * half_size_0;
    out_0.tint_0 = colors_0[vid_1];
    out_0.corner_radius_0 = radii_0[vid_1] * _S5;
    out_0.rect_half_size_0 = half_size_0;
    out_0.border_thickness_0 = inst_0.style_0.x * _S5;
    out_0.softness_0 = inst_0.style_0.y * _S5;
    out_0.omit_texture_0 = inst_0.style_0.z;
    return out_0;
}

