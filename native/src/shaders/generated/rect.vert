#version 330
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

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

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
layout(location = 7) in vec4 inst_style;
layout(location = 0) in vec4 inst_dst_rect;
layout(location = 1) in vec4 inst_src_rect;
layout(location = 6) in vec4 inst_corner_radii;
layout(location = 3) in vec4 inst_color01;
layout(location = 2) in vec4 inst_color00;
layout(location = 5) in vec4 inst_color11;
layout(location = 4) in vec4 inst_color10;
out vec2 v0;
out vec2 v1;
out vec4 v2;
out float v3;
flat out vec2 v4;
flat out float v5;
flat out float v6;
flat out float v7;

void main()
{
    uint _41 = uint(gl_VertexID - SPIRV_Cross_BaseVertex);
    float _540;
    if ((_41 >> uint(1)) != 0u)
    {
        _540 = 1.0;
    }
    else
    {
        _540 = 0.0;
    }
    float _541;
    if ((_41 & 1u) != 0u)
    {
        _541 = 0.0;
    }
    else
    {
        _541 = 1.0;
    }
    vec2 _556 = vec2(_540, _541);
    float _78 = _556.x;
    float shear;
    if (_78 > 0.5)
    {
        shear = inst_style.w;
    }
    else
    {
        shear = 0.0;
    }
    float _101 = _556.y;
    float _12[4] = float[](inst_corner_radii.y, inst_corner_radii.x, inst_corner_radii.w, inst_corner_radii.z);
    vec4 _16[4] = vec4[](inst_color01, inst_color00, inst_color11, inst_color10);
    vec3 p = vec3(vec2(mix(inst_dst_rect.x, inst_dst_rect.z, _78), mix(inst_dst_rect.y, inst_dst_rect.w, _101) + shear), 1.0);
    float _172 = max(u.xform_scale.x, u.xform_scale.y);
    vec2 half_size = (abs(inst_dst_rect.zw - inst_dst_rect.xy) * 0.5) * u.xform_scale;
    vec2 texcoord = vec2(mix(inst_src_rect.x, inst_src_rect.z, _78), mix(inst_src_rect.y, inst_src_rect.w, _101)) / u.texture_size;
    bool _22;
    if (u.sample_is_surface > 0.5)
    {
        _22 = u.surface_flip_y > 0.5;
    }
    else
    {
        _22 = false;
    }
    if (_22)
    {
        vec2 _204 = texcoord;
        _204.y = 1.0 - _204.y;
        texcoord = _204;
    }
    gl_Position = vec4(((2.0 * dot(u.xform.data[0].xyz, p)) / u.viewport_size.x) - 1.0, 1.0 - ((2.0 * dot(u.xform.data[1].xyz, p)) / u.viewport_size.y), 0.0, 1.0);
    v0 = texcoord;
    v1 = ((_556 * 2.0) - vec2(1.0)) * half_size;
    v2 = _16[uint(gl_VertexID - SPIRV_Cross_BaseVertex)];
    v3 = _12[uint(gl_VertexID - SPIRV_Cross_BaseVertex)] * _172;
    v4 = half_size;
    v5 = inst_style.x * _172;
    v6 = inst_style.y * _172;
    v7 = inst_style.z;
}

