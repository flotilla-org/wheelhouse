#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 37 "effect_prelude.slang"
struct vs_main_Result_0
{
    float4 position_0 [[position]];
    float2 uv_0 [[user(TEXCOORD)]];
};


#line 37
struct V2P_0
{
    float4 position_1;
    float2 uv_1;
};


#line 37
[[vertex]] vs_main_Result_0 vs_main(uint vid_0 [[vertex_id]])
{

#line 48
    float x_0 = float(int(vid_0 & 1U) * int(4) - int(1));
    float y_0 = float(int(vid_0 >> 1U) * int(4) - int(1));

#line 47
    thread V2P_0 out_0;


    (&out_0)->position_1 = float4(x_0, y_0, 0.0f, 1.0f);
    (&out_0)->uv_1 = float2((x_0 + 1.0f) * 0.5f, 1.0f - (y_0 + 1.0f) * 0.5f);

#line 51
    thread vs_main_Result_0 _S1;

#line 51
    (&_S1)->position_0 = out_0.position_1;

#line 51
    (&_S1)->uv_0 = out_0.uv_1;

#line 51
    return _S1;
}

