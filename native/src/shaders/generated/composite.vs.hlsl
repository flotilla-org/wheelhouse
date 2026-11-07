#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 7 "composite.slang"
struct CompositeUniforms_0
{
    float4 dst_ndc_0;
    float4 src_uv_0;
};

cbuffer u_0 : register(b0)
{
    CompositeUniforms_0 u_0;
}

#line 27 "common.slang"
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


#line 17 "composite.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
};


V2P_0 vs_main(uint vid_1 : SV_VertexID)
{
    float2 pct_0 = corner_pct_0(vid_1);
    V2P_0 out_0;
    out_0.position_0 = float4(lerp(u_0.dst_ndc_0.xy, u_0.dst_ndc_0.zw, pct_0), 0.0f, 1.0f);
    out_0.texcoord_0 = lerp(u_0.src_uv_0.xy, u_0.src_uv_0.zw, pct_0);
    return out_0;
}

