#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 48 "mesh.slang"
float4 transform_0(float4 c0_0, float4 c1_0, float4 c2_0, float4 c3_0, float4 v_0)
{
    return float4(v_0.x)  * c0_0 + float4(v_0.y)  * c1_0 + float4(v_0.z)  * c2_0 + float4(v_0.w)  * c3_0;
}


#line 24
struct vs_main_Result_0
{
    float4 position_0 [[position]];
    float2 texcoord_0 [[user(TEXCOORD)]];
    float4 color_0 [[user(TEXCOORD_1)]];
};


#line 3332 "core.meta.slang"
struct vertexInput_0
{
    float3 position_1 [[attribute(0)]];
    float3 normal_0 [[attribute(1)]];
    float2 texcoord_1 [[attribute(2)]];
    float3 color_1 [[attribute(3)]];
    float4 xform0_0 [[attribute(4)]];
    float4 xform1_0 [[attribute(5)]];
    float4 xform2_0 [[attribute(6)]];
    float4 xform3_0 [[attribute(7)]];
    float4 albedo_remap_0 [[attribute(8)]];
};


#line 7 "mesh.slang"
struct MeshUniforms_0
{
    array<float4, int(4)> view_projection_0;
};


#line 41
struct V2P_0
{
    float4 position_2;
    float2 texcoord_2;
    float4 color_2;
};


#line 41
[[vertex]] vs_main_Result_0 vs_main(vertexInput_0 _S1 [[stage_in]], MeshUniforms_0 constant* u_0 [[buffer(0)]])
{

#line 57
    thread V2P_0 out_0;
    (&out_0)->position_2 = transform_0(u_0->view_projection_0[int(0)], u_0->view_projection_0[int(1)], u_0->view_projection_0[int(2)], u_0->view_projection_0[int(3)], transform_0(_S1.xform0_0, _S1.xform1_0, _S1.xform2_0, _S1.xform3_0, float4(_S1.position_1, 1.0f)));
    (&out_0)->texcoord_2 = _S1.albedo_remap_0.xy + _S1.texcoord_1 * _S1.albedo_remap_0.zw;
    (&out_0)->color_2 = float4(_S1.color_1, 1.0f);

#line 60
    thread vs_main_Result_0 _S2;

#line 60
    (&_S2)->position_0 = out_0.position_2;

#line 60
    (&_S2)->texcoord_0 = out_0.texcoord_2;

#line 60
    (&_S2)->color_0 = out_0.color_2;

#line 60
    return _S2;
}

