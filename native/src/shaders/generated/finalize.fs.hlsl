#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 6 "finalize.slang"
Texture2D<float4 > t_stage_0 : register(t0);


#line 7
SamplerState s_stage_0 : register(s0);

struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 texcoord_0 : TEXCOORD0;
};


#line 26
float4 fs_main(V2P_0 in_0) : SV_TARGET
{
    return float4(t_stage_0.Sample(s_stage_0, in_0.texcoord_0).xyz, 1.0f);
}

