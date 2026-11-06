struct BlurUniforms
{
    float4 rect;
    float4 corner_radii;
    float2 direction;
    float2 viewport_size;
    uint blur_count;
    float texcoord_flip_y;
    float4 kernel[32];
};

struct V2P
{
    float4 position;
    float2 texcoord;
    float2 sdf_sample_pos;
    float corner_radius;
    float2 rect_half_size;
};

static const V2P _127 = { 0.0f.xxxx, 0.0f.xx, 0.0f.xx, 0.0f, 0.0f.xx };

cbuffer u : register(b0)
{
    BlurUniforms u_1_m0 : packoffset(c0);
};

Texture2D<float4> t_stage : register(t1);
SamplerState s_stage : register(s2);

static float4 gl_FragCoord;
static float2 texcoord;
static float2 sdf_sample_pos;
static float corner_radius;
static float2 rect_half_size;
static float4 _218;

struct SPIRV_Cross_Input
{
    float2 texcoord : TEXCOORD0;
    float2 sdf_sample_pos : TEXCOORD1;
    float corner_radius : TEXCOORD2;
    nointerpolation float2 rect_half_size : TEXCOORD3;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 _218 : SV_Target0;
};

float rect_sdf(float2 sample_pos, float2 rect_half_size_1, float r)
{
    return length(max((abs(sample_pos) - rect_half_size_1) + r.xx, 0.0f.xx)) - r;
}

void frag_main()
{
    float3 color = 0.0f.xxx;
    uint i = 1u;
    bool _230 = false;
    uint2 loop_bound = uint2(4294967295u, 4294967295u);
    V2P _204 = { gl_FragCoord, texcoord, sdf_sample_pos, corner_radius, rect_half_size };
    color = t_stage.Sample(s_stage, _204.texcoord).xyz * u_1_m0.kernel[0u].x;
    for (;;)
    {
        if (all(bool2(uint2(0u, 0u).x == loop_bound.x, uint2(0u, 0u).y == loop_bound.y)))
        {
            break;
        }
        loop_bound -= uint2(uint(loop_bound.y == 0u), 1u);
        if (!(i < u_1_m0.blur_count))
        {
            break;
        }
        float2 _282 = u_1_m0.direction * u_1_m0.kernel[i].y;
        color += (t_stage.Sample(s_stage, _204.texcoord - _282).xyz * u_1_m0.kernel[i].x);
        color += (t_stage.Sample(s_stage, _204.texcoord + _282).xyz * u_1_m0.kernel[i].x);
        i++;
        continue;
    }
    if (u_1_m0.direction.y != 0.0f)
    {
        _230 = (1.0f - smoothstep(0.0f, 2.0f, rect_sdf(_204.sdf_sample_pos, _204.rect_half_size, _204.corner_radius))) < 0.89999997615814208984375f;
    }
    else
    {
        _230 = false;
    }
    if (_230)
    {
        discard;
    }
    _218 = float4(color, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    texcoord = stage_input.texcoord;
    sdf_sample_pos = stage_input.sdf_sample_pos;
    corner_radius = stage_input.corner_radius;
    rect_half_size = stage_input.rect_half_size;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._218 = _218;
    return stage_output;
}
