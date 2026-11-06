#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct RectUniforms
{
    float2 viewport_size;
    float opacity;
    float sample_is_surface;
    float4x4 texture_sample_channel_map;
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

struct _22
{
    RectUniforms _m0;
};

struct fs_main_out
{
    float4 m_324 [[color(0)]];
};

struct fs_main_in
{
    float2 texcoord [[user(locn0)]];
    float2 sdf_sample_pos [[user(locn1)]];
    float4 tint [[user(locn2)]];
    float corner_radius [[user(locn3)]];
    float2 rect_half_size [[user(locn4), flat]];
    float border_thickness [[user(locn5), flat]];
    float softness [[user(locn6), flat]];
    float omit_texture [[user(locn7), flat]];
};

static inline __attribute__((always_inline))
float linear_from_srgb_f32(float x)
{
    return (x < 0.0404482372105121612548828125) ? (x / 12.9200000762939453125) : powr((x + 0.054999999701976776123046875) / 1.05499994754791259765625, 2.400000095367431640625);
}

static inline __attribute__((always_inline))
float4 linear_from_srgba(float4 v)
{
    return float4(linear_from_srgb_f32(v.x), linear_from_srgb_f32(v.y), linear_from_srgb_f32(v.z), v.w);
}

static inline __attribute__((always_inline))
float rect_sdf(float2 sample_pos, float2 rect_half_size, float r)
{
    return length(fast::max((abs(sample_pos) - rect_half_size) + float2(r), float2(0.0))) - r;
}

fragment fs_main_out fs_main(fs_main_in in [[stage_in]], constant _22& u [[buffer(0)]], texture2d<float> t_color [[texture(0)]], sampler s_color [[sampler(0)]], float4 gl_FragCoord [[position]])
{
    float4 result = float4(0.0);
    float border_t = 1.0;
    bool _337 = false;
    float4 albedo = float4(1.0);
    float corner_t = 1.0;
    fs_main_out out = {};
    V2P _303 = V2P{ gl_FragCoord, in.texcoord, in.sdf_sample_pos, in.tint, in.corner_radius, in.rect_half_size, in.border_thickness, in.softness, in.omit_texture };
    float4 _348 = t_color.sample(s_color, _303.texcoord);
    float4 _349 = u._m0.texture_sample_channel_map * _348;
    if (_303.omit_texture < 0.5)
    {
        if (u._m0.sample_is_surface > 0.5)
        {
            albedo = _349;
            if (albedo.w > 9.9999997473787516355514526367188e-05)
            {
                albedo = float4(albedo.xyz / float3(albedo.w), albedo.w);
            }
        }
        else
        {
            albedo = linear_from_srgba(_349);
        }
    }
    float _377 = fast::max(2.0 * _303.softness, 0.001000000047497451305389404296875);
    if (_303.border_thickness > 0.0)
    {
        border_t = smoothstep(0.0, _377, rect_sdf(_303.sdf_sample_pos, (_303.rect_half_size - float2(2.0 * _303.softness)) - float2(_303.border_thickness), fast::max(_303.corner_radius - _303.border_thickness, 0.0)));
    }
    if (border_t < 0.001000000047497451305389404296875)
    {
        discard_fragment();
    }
    if (!(_303.corner_radius > 0.0))
    {
        _337 = _303.softness > 0.75;
    }
    else
    {
        _337 = true;
    }
    if (_337)
    {
        corner_t = 1.0 - smoothstep(0.0, _377, rect_sdf(_303.sdf_sample_pos, _303.rect_half_size - float2(2.0 * _303.softness), _303.corner_radius));
    }
    result = albedo * _303.tint;
    result.w *= ((u._m0.opacity * corner_t) * border_t);
    out.m_324 = result;
    return out;
}

