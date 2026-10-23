#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct EffectUniforms
{
    float2 source_size_px;
    float2 output_size_px;
    float4 params0;
    float4 params1;
};

struct V2P
{
    float4 position;
    float2 uv;
};

struct _15
{
    EffectUniforms _m0;
};

struct vs_main_out
{
    float2 uv [[user(locn0)]];
    float4 gl_Position [[position]];
};

vertex vs_main_out vs_main(uint gl_VertexIndex [[vertex_id]])
{
    V2P _out = V2P{ float4(0.0), float2(0.0) };
    vs_main_out out = {};
    float _220 = float((int(gl_VertexIndex & 1u) * 4) - 1);
    float _225 = float((int(gl_VertexIndex >> 1u) * 4) - 1);
    _out.position = float4(_220, _225, 0.0, 1.0);
    _out.uv = float2((_220 + 1.0) * 0.5, 1.0 - ((_225 + 1.0) * 0.5));
    out.gl_Position = _out.position;
    out.uv = _out.uv;
    return out;
}

