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

static float4 gl_FragCoord;
static float2 texcoord;
static float2 sdf_sample_pos;
static float4 tint;
static float corner_radius;
static float2 rect_half_size;
static float border_thickness;
static float softness;
static float omit_texture;
static float4 _324;

struct SPIRV_Cross_Input
{
    float2 texcoord : TEXCOORD0;
    float2 sdf_sample_pos : TEXCOORD1;
    float4 tint : TEXCOORD2;
    float corner_radius : TEXCOORD3;
    nointerpolation float2 rect_half_size : TEXCOORD4;
    nointerpolation float border_thickness : TEXCOORD5;
    nointerpolation float softness : TEXCOORD6;
    nointerpolation float omit_texture : TEXCOORD7;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 _324 : SV_Target0;
};

float linear_from_srgb_f32(float x)
{
    return (x < 0.0404482372105121612548828125f) ? (x / 12.9200000762939453125f) : pow((x + 0.054999999701976776123046875f) / 1.05499994754791259765625f, 2.400000095367431640625f);
}

float4 linear_from_srgba(float4 v)
{
    return float4(linear_from_srgb_f32(v.x), linear_from_srgb_f32(v.y), linear_from_srgb_f32(v.z), v.w);
}

float rect_sdf(float2 sample_pos, float2 rect_half_size_1, float r)
{
    return length(max((abs(sample_pos) - rect_half_size_1) + r.xx, 0.0f.xx)) - r;
}

void frag_main()
{
    float4 result = 0.0f.xxxx;
    float border_t = 1.0f;
    bool _337 = false;
    float4 albedo = 1.0f.xxxx;
    float corner_t = 1.0f;
    V2P _303 = { gl_FragCoord, texcoord, sdf_sample_pos, tint, corner_radius, rect_half_size, border_thickness, softness, omit_texture };
    float4 _348 = t_color.Sample(s_color, _303.texcoord);
    float4 _349 = mul(_348, u_1_m0.texture_sample_channel_map);
    if (_303.omit_texture < 0.5f)
    {
        if (u_1_m0.sample_is_surface > 0.5f)
        {
            albedo = _349;
            if (albedo.w > 9.9999997473787516355514526367188e-05f)
            {
                albedo = float4(albedo.xyz / albedo.w.xxx, albedo.w);
            }
        }
        else
        {
            albedo = linear_from_srgba(_349);
        }
    }
    float _377 = max(2.0f * _303.softness, 0.001000000047497451305389404296875f);
    if (_303.border_thickness > 0.0f)
    {
        border_t = smoothstep(0.0f, _377, rect_sdf(_303.sdf_sample_pos, (_303.rect_half_size - (2.0f * _303.softness).xx) - _303.border_thickness.xx, max(_303.corner_radius - _303.border_thickness, 0.0f)));
    }
    if (border_t < 0.001000000047497451305389404296875f)
    {
        discard;
    }
    if (!(_303.corner_radius > 0.0f))
    {
        _337 = _303.softness > 0.75f;
    }
    else
    {
        _337 = true;
    }
    if (_337)
    {
        corner_t = 1.0f - smoothstep(0.0f, _377, rect_sdf(_303.sdf_sample_pos, _303.rect_half_size - (2.0f * _303.softness).xx, _303.corner_radius));
    }
    result = albedo * _303.tint;
    result.w *= ((u_1_m0.opacity * corner_t) * border_t);
    _324 = result;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    texcoord = stage_input.texcoord;
    sdf_sample_pos = stage_input.sdf_sample_pos;
    tint = stage_input.tint;
    corner_radius = stage_input.corner_radius;
    rect_half_size = stage_input.rect_half_size;
    border_thickness = stage_input.border_thickness;
    softness = stage_input.softness;
    omit_texture = stage_input.omit_texture;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._324 = _324;
    return stage_output;
}
