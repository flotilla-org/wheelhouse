#pragma clang diagnostic ignored "-Wmissing-prototypes"

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

struct fs_main_out
{
    float4 m_247 [[color(0)]];
};

struct fs_main_in
{
    float2 uv [[user(locn0)]];
};

static inline __attribute__((always_inline))
float4 src(float2 uv, texture2d<float> t_source, sampler s_source)
{
    return t_source.sample(s_source, uv);
}

static inline __attribute__((always_inline))
float4 src_at(float2 uv, float2 offset_px, constant _15& u, texture2d<float> t_source, sampler s_source)
{
    return t_source.sample(s_source, (uv + (offset_px / u._m0.source_size_px)));
}

static inline __attribute__((always_inline))
uint naga_mod(uint lhs, uint rhs)
{
    return lhs % ((rhs == 0u) ? 1u : rhs);
}

static inline __attribute__((always_inline))
float4 effect(float2 uv01, float2 frag_px, constant _15& u, texture2d<float> t_source, sampler s_source)
{
    float3 mask_mul = float3(0.0);
    bool _82 = false;
    float4 color = float4(0.0);
    bool _79 = false;
    bool _84 = false;
    float2 _108 = uv01 - float2(0.5);
    float _109 = dot(_108, _108);
    float2 _113 = uv01 + (_108 * ((_109 * u._m0.params0.y) * 2.0));
    if (!(_113.x < 0.0))
    {
        _79 = _113.x > 1.0;
    }
    else
    {
        _79 = true;
    }
    if (!_79)
    {
        _82 = _113.y < 0.0;
    }
    else
    {
        _82 = true;
    }
    if (!_82)
    {
        _84 = _113.y > 1.0;
    }
    else
    {
        _84 = true;
    }
    if (_84)
    {
        return float4(0.0);
    }
    color = src(_113, t_source, s_source);
    if (u._m0.params1.x > 0.001000000047497451305389404296875)
    {
        color.x = src_at(_113, float2(u._m0.params1.x, 0.0), u, t_source, s_source).x;
        color.z = src_at(_113, float2(-u._m0.params1.x, 0.0), u, t_source, s_source).z;
    }
    float _158 = sin((_113.y * u._m0.source_size_px.y) * 3.1415927410125732421875);
    float _163 = 1.0 - (u._m0.params0.x * (0.3499999940395355224609375 + ((0.64999997615814208984375 * _158) * _158)));
    uint _168 = naga_mod(uint(fast::clamp(frag_px.x, 0.0, 4294967040.0)), 3u);
    mask_mul = float3(1.0 - (u._m0.params0.w * 0.449999988079071044921875));
    if (_168 == 0u)
    {
        mask_mul.x = 1.0;
    }
    else
    {
        if (_168 == 1u)
        {
            mask_mul.y = 1.0;
        }
        else
        {
            mask_mul.z = 1.0;
        }
    }
    float _186 = fast::max(1.0 - ((u._m0.params0.z * _109) * 1.5), 0.0);
    return float4((((color.xyz * mask_mul) * u._m0.params1.y) * _163) * _186, (color.w * _163) * _186);
}

fragment fs_main_out fs_main(fs_main_in in [[stage_in]], constant _15& u [[buffer(0)]], texture2d<float> t_source [[texture(0)]], sampler s_source [[sampler(0)]], float4 gl_FragCoord [[position]])
{
    fs_main_out out = {};
    V2P _240 = V2P{ gl_FragCoord, in.uv };
    out.m_247 = effect(_240.uv, _240.position.xy, u, t_source, s_source);
    return out;
}

