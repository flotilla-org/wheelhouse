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

static const V2P _214 = { 0.0f.xxxx, 0.0f.xx };

cbuffer u : register(b0)
{
    EffectUniforms u_1_m0 : packoffset(c0);
};

Texture2D<float4> t_source : register(t1);
SamplerState s_source : register(s2);

static float4 gl_FragCoord;
static float2 uv;
static float4 _247;

struct SPIRV_Cross_Input
{
    float2 uv : TEXCOORD0;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 _247 : SV_Target0;
};

float4 src(float2 uv_1)
{
    return t_source.Sample(s_source, uv_1);
}

float4 src_at(float2 uv_1, float2 offset_px)
{
    return t_source.Sample(s_source, uv_1 + (offset_px / u_1_m0.source_size_px));
}

uint naga_mod(uint lhs, uint rhs)
{
    return lhs % ((rhs == 0u) ? 1u : rhs);
}

float4 effect(float2 uv01, float2 frag_px)
{
    float3 mask_mul = 0.0f.xxx;
    bool _82 = false;
    float4 color = 0.0f.xxxx;
    bool _79 = false;
    bool _84 = false;
    float2 _108 = uv01 - 0.5f.xx;
    float _109 = dot(_108, _108);
    float2 _113 = uv01 + (_108 * ((_109 * u_1_m0.params0.y) * 2.0f));
    if (!(_113.x < 0.0f))
    {
        _79 = _113.x > 1.0f;
    }
    else
    {
        _79 = true;
    }
    if (!_79)
    {
        _82 = _113.y < 0.0f;
    }
    else
    {
        _82 = true;
    }
    if (!_82)
    {
        _84 = _113.y > 1.0f;
    }
    else
    {
        _84 = true;
    }
    if (_84)
    {
        return 0.0f.xxxx;
    }
    color = src(_113);
    if (u_1_m0.params1.x > 0.001000000047497451305389404296875f)
    {
        color.x = src_at(_113, float2(u_1_m0.params1.x, 0.0f)).x;
        color.z = src_at(_113, float2(-u_1_m0.params1.x, 0.0f)).z;
    }
    float _158 = sin((_113.y * u_1_m0.source_size_px.y) * 3.1415927410125732421875f);
    float _163 = 1.0f - (u_1_m0.params0.x * (0.3499999940395355224609375f + ((0.64999997615814208984375f * _158) * _158)));
    uint _168 = naga_mod(uint(clamp(frag_px.x, 0.0f, 4294967040.0f)), 3u);
    mask_mul = (1.0f - (u_1_m0.params0.w * 0.449999988079071044921875f)).xxx;
    if (_168 == 0u)
    {
        mask_mul.x = 1.0f;
    }
    else
    {
        if (_168 == 1u)
        {
            mask_mul.y = 1.0f;
        }
        else
        {
            mask_mul.z = 1.0f;
        }
    }
    float _186 = max(1.0f - ((u_1_m0.params0.z * _109) * 1.5f), 0.0f);
    return float4((((color.xyz * mask_mul) * u_1_m0.params1.y) * _163) * _186, (color.w * _163) * _186);
}

void frag_main()
{
    V2P _240 = { gl_FragCoord, uv };
    _247 = effect(_240.uv, _240.position.xy);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    uv = stage_input.uv;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._247 = _247;
    return stage_output;
}
