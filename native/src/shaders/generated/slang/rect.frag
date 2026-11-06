#version 330

struct _Array_std140_vector_float_4_4
{
    vec4 data[4];
};

struct _Array_std140_vector_float_4_3
{
    vec4 data[3];
};

layout(std140) uniform RectUniforms_std140
{
    vec2 viewport_size;
    float opacity;
    float sample_is_surface;
    _Array_std140_vector_float_4_4 texture_sample_channel_map;
    vec2 texture_size;
    vec2 xform_scale;
    _Array_std140_vector_float_4_3 xform;
    float surface_flip_y;
} u;

uniform sampler2D SPIRV_Cross_Combinedt_colors_color;

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
flat in float v7;
in vec2 v0;
flat in float v6;
flat in float v5;
flat in vec2 v4;
in vec2 v1;
in float v3;
in vec4 v2;
layout(location = 0) out vec4 entryPointParam_fs_main;

void main()
{
    vec4 albedo = vec4(1.0);
    if (v7 < 0.5)
    {
        vec4 _sampled = texture(SPIRV_Cross_Combinedt_colors_color, v0);
        vec4 raw = (((u.texture_sample_channel_map.data[0] * _sampled.x) + (u.texture_sample_channel_map.data[1] * _sampled.y)) + (u.texture_sample_channel_map.data[2] * _sampled.z)) + (u.texture_sample_channel_map.data[3] * _sampled.w);
        if (u.sample_is_surface > 0.5)
        {
            albedo = raw;
            if (raw.w > 9.9999997473787516355514526367188e-05)
            {
                vec4 _387 = albedo;
                vec3 _392 = _387.xyz / vec3(_387.w);
                vec4 _659 = _387;
                _659.x = _392.x;
                _659.y = _392.y;
                _659.z = _392.z;
                albedo = _659;
            }
        }
        else
        {
            float _572 = raw.x;
            float _580;
            if (_572 < 0.0404482372105121612548828125)
            {
                _580 = _572 * 0.077399380505084991455078125;
            }
            else
            {
                _580 = pow((_572 + 0.054999999701976776123046875) * 0.947867333889007568359375, 2.400000095367431640625);
            }
            float _574 = raw.y;
            float _592;
            if (_574 < 0.0404482372105121612548828125)
            {
                _592 = _574 * 0.077399380505084991455078125;
            }
            else
            {
                _592 = pow((_574 + 0.054999999701976776123046875) * 0.947867333889007568359375, 2.400000095367431640625);
            }
            float _576 = raw.z;
            float _604;
            if (_576 < 0.0404482372105121612548828125)
            {
                _604 = _576 * 0.077399380505084991455078125;
            }
            else
            {
                _604 = pow((_576 + 0.054999999701976776123046875) * 0.947867333889007568359375, 2.400000095367431640625);
            }
            albedo = vec4(_580, _592, _604, raw.w);
        }
    }
    float _447 = max(2.0 * v6, 0.001000000047497451305389404296875);
    float border_t;
    if (v5 > 0.0)
    {
        float _468 = max(v3 - v5, 0.0);
        border_t = smoothstep(0.0, _447, length(max((abs(v1) - ((v4 - vec2(2.0 * v6)) - vec2(v5))) + vec2(_468), vec2(0.0))) - _468);
    }
    else
    {
        border_t = 1.0;
    }
    if (border_t < 0.001000000047497451305389404296875)
    {
        discard;
    }
    bool _312;
    if (v3 > 0.0)
    {
        _312 = true;
    }
    else
    {
        _312 = v6 > 0.75;
    }
    float corner_t;
    if (_312)
    {
        corner_t = 1.0 - smoothstep(0.0, _447, length(max((abs(v1) - (v4 - vec2(2.0 * v6))) + vec2(v3), vec2(0.0))) - v3);
    }
    else
    {
        corner_t = 1.0;
    }
    vec4 _524 = albedo * v2;
    _524.w = _524.w * ((u.opacity * corner_t) * border_t);
    entryPointParam_fs_main = _524;
}

