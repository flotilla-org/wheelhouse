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

static float4 gl_Position;
static int gl_VertexIndex;
static float2 uv;

struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 uv : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    V2P _out = _214;
    float _220 = float((int(uint(gl_VertexIndex) & 1u) * 4) - 1);
    float _225 = float((int(uint(gl_VertexIndex) >> 1u) * 4) - 1);
    _out.position = float4(_220, _225, 0.0f, 1.0f);
    _out.uv = float2((_220 + 1.0f) * 0.5f, 1.0f - ((_225 + 1.0f) * 0.5f));
    gl_Position = _out.position;
    uv = _out.uv;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.uv = uv;
    return stage_output;
}
