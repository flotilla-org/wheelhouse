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


#line 17 "composite.slang"
struct vs_main_Result_0
{
    float4 position_0 [[position]];
    float2 texcoord_0 [[user(TEXCOORD)]];
};


#line 7
struct CompositeUniforms_0
{
    float4 dst_ndc_0;
    float4 src_uv_0;
};


#line 17
struct V2P_0
{
    float4 position_1;
    float2 texcoord_1;
};


#line 17
[[vertex]] vs_main_Result_0 vs_main(uint vid_1 [[vertex_id]], CompositeUniforms_0 constant* u_0 [[buffer(0)]])
{

#line 26
    float2 pct_0 = corner_pct_0(vid_1);
    thread V2P_0 out_0;
    (&out_0)->position_1 = float4(mix(u_0->dst_ndc_0.xy, u_0->dst_ndc_0.zw, pct_0), 0.0f, 1.0f);
    (&out_0)->texcoord_1 = mix(u_0->src_uv_0.xy, u_0->src_uv_0.zw, pct_0);

#line 29
    thread vs_main_Result_0 _S3;

#line 29
    (&_S3)->position_0 = out_0.position_1;

#line 29
    (&_S3)->texcoord_0 = out_0.texcoord_1;

#line 29
    return _S3;
}

