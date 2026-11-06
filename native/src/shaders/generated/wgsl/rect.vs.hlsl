struct RectUniforms
{
    float2 viewport_size;
    float opacity;
    float sample_is_surface;
    row_major float4x4 texture_sample_channel_map;
    float2 texture_size;
    float2 xform_scale;
    float4 xform[3];
    float surface_flip_y;
};

struct Instance
{
    float4 dst_rect;
    float4 src_rect;
    float4 color00;
    float4 color01;
    float4 color10;
    float4 color11;
    float4 corner_radii;
    float4 style;
};

struct V2P
{
    float4 position;
    float2 texcoord;
    float2 sdf_sample_pos;
    float4 tint;
    float corner_radius;
    float2 rect_half_size;
    float border_thickness;
    float softness;
    float omit_texture;
};

static const V2P _155 = { 0.0f.xxxx, 0.0f.xx, 0.0f.xx, 0.0f.xxxx, 0.0f, 0.0f.xx, 0.0f, 0.0f, 0.0f };

cbuffer u : register(b0)
{
    RectUniforms u_1_m0 : packoffset(c0);
};

Texture2D<float4> t_color : register(t1);
SamplerState s_color : register(s2);

static float4 gl_Position;
static int gl_VertexIndex;
static float4 dst_rect;
static float4 src_rect;
static float4 color00;
static float4 color01;
static float4 color10;
static float4 color11;
static float4 corner_radii;
static float4 style;
static float2 texcoord;
static float2 sdf_sample_pos;
static float4 tint;
static float corner_radius;
static float2 rect_half_size;
static float border_thickness;
static float softness;
static float omit_texture;

struct SPIRV_Cross_Input
{
    float4 dst_rect : TEXCOORD0;
    float4 src_rect : TEXCOORD1;
    float4 color00 : TEXCOORD2;
    float4 color01 : TEXCOORD3;
    float4 color10 : TEXCOORD4;
    float4 color11 : TEXCOORD5;
    float4 corner_radii : TEXCOORD6;
    float4 style : TEXCOORD7;
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 texcoord : TEXCOORD0;
    float2 sdf_sample_pos : TEXCOORD1;
    float4 tint : TEXCOORD2;
    float corner_radius : TEXCOORD3;
    nointerpolation float2 rect_half_size : TEXCOORD4;
    nointerpolation float border_thickness : TEXCOORD5;
    nointerpolation float softness : TEXCOORD6;
    nointerpolation float omit_texture : TEXCOORD7;
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
    bool _150 = false;
    V2P _out = _155;
    Instance _110 = { dst_rect, src_rect, color00, color01, color10, color11, corner_radii, style };
    float2 _157 = corner_pct(uint(gl_VertexIndex));
    float _198[4] = { _110.corner_radii.y, _110.corner_radii.x, _110.corner_radii.w, _110.corner_radii.z };
    float4 _203[4] = { _110.color01, _110.color00, _110.color11, _110.color10 };
    float3 _204 = float3(float2(lerp(_110.dst_rect.x, _110.dst_rect.z, _157.x), lerp(_110.dst_rect.y, _110.dst_rect.w, _157.y) + ((_157.x > 0.5f) ? _110.style.w : 0.0f)), 1.0f);
    float _224 = max(u_1_m0.xform_scale.x, u_1_m0.xform_scale.y);
    float2 _234 = (abs(_110.dst_rect.zw - _110.dst_rect.xy) * 0.5f) * u_1_m0.xform_scale;
    texcoord_1 = float2(lerp(_110.src_rect.x, _110.src_rect.z, _157.x), lerp(_110.src_rect.y, _110.src_rect.w, _157.y)) / u_1_m0.texture_size;
    if (u_1_m0.sample_is_surface > 0.5f)
    {
        _150 = u_1_m0.surface_flip_y > 0.5f;
    }
    else
    {
        _150 = false;
    }
    if (_150)
    {
        texcoord_1.y = 1.0f - texcoord_1.y;
    }
    _out.position = ndc_from_points(float2(dot(u_1_m0.xform[0u].xyz, _204), dot(u_1_m0.xform[1u].xyz, _204)), u_1_m0.viewport_size);
    _out.texcoord = texcoord_1;
    _out.sdf_sample_pos = ((_157 * 2.0f) - 1.0f.xx) * _234;
    float4 _269[4] = _203;
    _out.tint = _269[uint(gl_VertexIndex)];
    float _274[4] = _198;
    _out.corner_radius = _274[uint(gl_VertexIndex)] * _224;
    _out.rect_half_size = _234;
    _out.border_thickness = _110.style.x * _224;
    _out.softness = _110.style.y * _224;
    _out.omit_texture = _110.style.z;
    gl_Position = _out.position;
    texcoord = _out.texcoord;
    sdf_sample_pos = _out.sdf_sample_pos;
    tint = _out.tint;
    corner_radius = _out.corner_radius;
    rect_half_size = _out.rect_half_size;
    border_thickness = _out.border_thickness;
    softness = _out.softness;
    omit_texture = _out.omit_texture;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    dst_rect = stage_input.dst_rect;
    src_rect = stage_input.src_rect;
    color00 = stage_input.color00;
    color01 = stage_input.color01;
    color10 = stage_input.color10;
    color11 = stage_input.color11;
    corner_radii = stage_input.corner_radii;
    style = stage_input.style;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.texcoord = texcoord;
    stage_output.sdf_sample_pos = sdf_sample_pos;
    stage_output.tint = tint;
    stage_output.corner_radius = corner_radius;
    stage_output.rect_half_size = rect_half_size;
    stage_output.border_thickness = border_thickness;
    stage_output.softness = softness;
    stage_output.omit_texture = omit_texture;
    return stage_output;
}
