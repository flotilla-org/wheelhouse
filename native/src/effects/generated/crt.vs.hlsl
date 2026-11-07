#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 37 "effect_prelude.slang"
struct V2P_0
{
    float4 position_0 : SV_Position;
    float2 uv_0 : TEXCOORD0;
};


V2P_0 vs_main(uint vid_0 : SV_VertexID)
{


    float x_0 = float(int(vid_0 & 1U) * int(4) - int(1));
    float y_0 = float(int(vid_0 >> int(1)) * int(4) - int(1));

#line 47
    V2P_0 out_0;


    out_0.position_0 = float4(x_0, y_0, 0.0f, 1.0f);
    out_0.uv_0 = float2((x_0 + 1.0f) * 0.5f, 1.0f - (y_0 + 1.0f) * 0.5f);
    return out_0;
}

