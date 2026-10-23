#version 330
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif

struct EffectUniforms
{
    vec2 source_size_px;
    vec2 output_size_px;
    vec4 params0;
    vec4 params1;
};

struct V2P
{
    vec4 position;
    vec2 uv;
};

layout(binding = 0, std140) uniform u
{
    EffectUniforms _m0;
} u_1;

uniform sampler2D SPIRV_Cross_Combinedt_sources_source;

in vec2 uv;
layout(location = 0) out vec4 _247;

vec4 src(vec2 uv_1)
{
    return texture(SPIRV_Cross_Combinedt_sources_source, uv_1);
}

vec4 src_at(vec2 uv_1, vec2 offset_px)
{
    return texture(SPIRV_Cross_Combinedt_sources_source, uv_1 + (offset_px / u_1._m0.source_size_px));
}

uint naga_mod(uint lhs, uint rhs)
{
    return lhs % ((rhs == 0u) ? 1u : rhs);
}

vec4 effect(vec2 uv01, vec2 frag_px)
{
    vec3 mask_mul = vec3(0.0);
    bool _82 = false;
    vec4 color = vec4(0.0);
    bool _79 = false;
    bool _84 = false;
    vec2 _108 = uv01 - vec2(0.5);
    float _109 = dot(_108, _108);
    vec2 _113 = uv01 + (_108 * ((_109 * u_1._m0.params0.y) * 2.0));
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
        return vec4(0.0);
    }
    color = src(_113);
    if (u_1._m0.params1.x > 0.001000000047497451305389404296875)
    {
        color.x = src_at(_113, vec2(u_1._m0.params1.x, 0.0)).x;
        color.z = src_at(_113, vec2(-u_1._m0.params1.x, 0.0)).z;
    }
    float _158 = sin((_113.y * u_1._m0.source_size_px.y) * 3.1415927410125732421875);
    float _163 = 1.0 - (u_1._m0.params0.x * (0.3499999940395355224609375 + ((0.64999997615814208984375 * _158) * _158)));
    uint _168 = naga_mod(uint(clamp(frag_px.x, 0.0, 4294967040.0)), 3u);
    mask_mul = vec3(1.0 - (u_1._m0.params0.w * 0.449999988079071044921875));
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
    float _186 = max(1.0 - ((u_1._m0.params0.z * _109) * 1.5), 0.0);
    return vec4((((color.xyz * mask_mul) * u_1._m0.params1.y) * _163) * _186, (color.w * _163) * _186);
}

void main()
{
    V2P _240 = V2P(gl_FragCoord, uv);
    _247 = effect(_240.uv, _240.position.xy);
}

