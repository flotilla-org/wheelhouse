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

static float4 gl_Position;
static int gl_VertexIndex;
static float2 texcoord;
static float2 sdf_sample_pos;
static float corner_radius;
static float2 rect_half_size;

struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 texcoord : TEXCOORD0;
    float2 sdf_sample_pos : TEXCOORD1;
    float corner_radius : TEXCOORD2;
    nointerpolation float2 rect_half_size : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

float2 corner_pct(uint vid)
{
    return float2(float((vid >> 1u) != 0u), ((vid & 1u) != 0u) ? 0.0f : 1.0f);
}

float4 ndc_from_points(float2 p, float2 viewport_size)
{
    return float4(((2.0f * p.x) / viewport_size.x) - 1.0f, 1.0f - ((2.0f * p.y) / viewport_size.y), 0.0f, 1.0f);
}

void vert_main()
{
    float2 texcoord_1 = 0.0f.xx;
    V2P _out = _127;
    float2 _129 = corner_pct(uint(gl_VertexIndex));
    float2 _146 = float2(lerp(u_1_m0.rect.x, u_1_m0.rect.z, _129.x), lerp(u_1_m0.rect.y, u_1_m0.rect.w, _129.y));
    float _155[4] = { u_1_m0.corner_radii.y, u_1_m0.corner_radii.x, u_1_m0.corner_radii.w, u_1_m0.corner_radii.z };
    float2 _163 = (u_1_m0.rect.zw - u_1_m0.rect.xy) * 0.5f;
    texcoord_1 = _146 / u_1_m0.viewport_size;
    if (u_1_m0.texcoord_flip_y > 0.5f)
    {
        texcoord_1.y = 1.0f - texcoord_1.y;
    }
    _out.position = ndc_from_points(_146, u_1_m0.viewport_size);
    _out.texcoord = texcoord_1;
    _out.sdf_sample_pos = ((_129 * 2.0f) - 1.0f.xx) * _163;
    float _191[4] = _155;
    _out.corner_radius = _191[uint(gl_VertexIndex)];
    _out.rect_half_size = _163 - 2.0f.xx;
    gl_Position = _out.position;
    texcoord = _out.texcoord;
    sdf_sample_pos = _out.sdf_sample_pos;
    corner_radius = _out.corner_radius;
    rect_half_size = _out.rect_half_size;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.texcoord = texcoord;
    stage_output.sdf_sample_pos = sdf_sample_pos;
    stage_output.corner_radius = corner_radius;
    stage_output.rect_half_size = rect_half_size;
    return stage_output;
}
