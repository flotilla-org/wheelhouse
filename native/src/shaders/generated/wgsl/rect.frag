#version 330

struct RectUniforms
{
    vec2 viewport_size;
    float opacity;
    float sample_is_surface;
    mat4 texture_sample_channel_map;
    vec2 texture_size;
    vec2 xform_scale;
    vec4 xform[3];
    float surface_flip_y;
};

struct Instance
{
    vec4 dst_rect;
    vec4 src_rect;
    vec4 color00;
    vec4 color01;
    vec4 color10;
    vec4 color11;
    vec4 corner_radii;
    vec4 style;
};

struct V2P
{
    vec4 position;
    vec2 texcoord;
    vec2 sdf_sample_pos;
    vec4 tint;
    float corner_radius;
    vec2 rect_half_size;
    float border_thickness;
    float softness;
    float omit_texture;
};

layout(std140) uniform u
{
    RectUniforms _m0;
} u_1;

uniform sampler2D SPIRV_Cross_Combinedt_colors_color;

in vec2 v0;
in vec2 v1;
in vec4 v2;
in float v3;
flat in vec2 v4;
flat in float v5;
flat in float v6;
flat in float v7;
layout(location = 0) out vec4 _324;

float linear_from_srgb_f32(float x)
{
    return (x < 0.0404482372105121612548828125) ? (x / 12.9200000762939453125) : pow((x + 0.054999999701976776123046875) / 1.05499994754791259765625, 2.400000095367431640625);
}

vec4 linear_from_srgba(vec4 v)
{
    return vec4(linear_from_srgb_f32(v.x), linear_from_srgb_f32(v.y), linear_from_srgb_f32(v.z), v.w);
}

float rect_sdf(vec2 sample_pos, vec2 rect_half_size, float r)
{
    return length(max((abs(sample_pos) - rect_half_size) + vec2(r), vec2(0.0))) - r;
}

void main()
{
    vec4 result = vec4(0.0);
    float border_t = 1.0;
    bool _337 = false;
    vec4 albedo = vec4(1.0);
    float corner_t = 1.0;
    V2P _303 = V2P(gl_FragCoord, v0, v1, v2, v3, v4, v5, v6, v7);
    vec4 _348 = texture(SPIRV_Cross_Combinedt_colors_color, _303.texcoord);
    vec4 _349 = u_1._m0.texture_sample_channel_map * _348;
    if (_303.omit_texture < 0.5)
    {
        if (u_1._m0.sample_is_surface > 0.5)
        {
            albedo = _349;
            if (albedo.w > 9.9999997473787516355514526367188e-05)
            {
                albedo = vec4(albedo.xyz / vec3(albedo.w), albedo.w);
            }
        }
        else
        {
            albedo = linear_from_srgba(_349);
        }
    }
    float _377 = max(2.0 * _303.softness, 0.001000000047497451305389404296875);
    if (_303.border_thickness > 0.0)
    {
        border_t = smoothstep(0.0, _377, rect_sdf(_303.sdf_sample_pos, (_303.rect_half_size - vec2(2.0 * _303.softness)) - vec2(_303.border_thickness), max(_303.corner_radius - _303.border_thickness, 0.0)));
    }
    if (border_t < 0.001000000047497451305389404296875)
    {
        discard;
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
        corner_t = 1.0 - smoothstep(0.0, _377, rect_sdf(_303.sdf_sample_pos, _303.rect_half_size - vec2(2.0 * _303.softness), _303.corner_radius));
    }
    result = albedo * _303.tint;
    result.w *= ((u_1._m0.opacity * corner_t) * border_t);
    _324 = result;
}

