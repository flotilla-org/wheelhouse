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

layout(location = 0) in vec4 dst_rect;
layout(location = 1) in vec4 src_rect;
layout(location = 2) in vec4 color00;
layout(location = 3) in vec4 color01;
layout(location = 4) in vec4 color10;
layout(location = 5) in vec4 color11;
layout(location = 6) in vec4 corner_radii;
layout(location = 7) in vec4 style;
out vec2 v0;
out vec2 v1;
out vec4 v2;
out float v3;
flat out vec2 v4;
flat out float v5;
flat out float v6;
flat out float v7;

vec2 corner_pct(uint vid)
{
    return vec2(float((vid >> 1u) != 0u), ((vid & 1u) != 0u) ? 0.0 : 1.0);
}

vec4 ndc_from_points(vec2 p, vec2 viewport_size)
{
    return vec4(((2.0 * p.x) / viewport_size.x) - 1.0, 1.0 - ((2.0 * p.y) / viewport_size.y), 0.0, 1.0);
}

void main()
{
    vec2 texcoord = vec2(0.0);
    bool _150 = false;
    V2P _out = V2P(vec4(0.0), vec2(0.0), vec2(0.0), vec4(0.0), 0.0, vec2(0.0), 0.0, 0.0, 0.0);
    Instance _110 = Instance(dst_rect, src_rect, color00, color01, color10, color11, corner_radii, style);
    vec2 _157 = corner_pct(uint(gl_VertexID));
    vec3 _204 = vec3(vec2(mix(_110.dst_rect.x, _110.dst_rect.z, _157.x), mix(_110.dst_rect.y, _110.dst_rect.w, _157.y) + ((_157.x > 0.5) ? _110.style.w : 0.0)), 1.0);
    float _224 = max(u_1._m0.xform_scale.x, u_1._m0.xform_scale.y);
    vec2 _234 = (abs(_110.dst_rect.zw - _110.dst_rect.xy) * 0.5) * u_1._m0.xform_scale;
    texcoord = vec2(mix(_110.src_rect.x, _110.src_rect.z, _157.x), mix(_110.src_rect.y, _110.src_rect.w, _157.y)) / u_1._m0.texture_size;
    if (u_1._m0.sample_is_surface > 0.5)
    {
        _150 = u_1._m0.surface_flip_y > 0.5;
    }
    else
    {
        _150 = false;
    }
    if (_150)
    {
        texcoord.y = 1.0 - texcoord.y;
    }
    _out.position = ndc_from_points(vec2(dot(u_1._m0.xform[0u].xyz, _204), dot(u_1._m0.xform[1u].xyz, _204)), u_1._m0.viewport_size);
    _out.texcoord = texcoord;
    _out.sdf_sample_pos = ((_157 * 2.0) - vec2(1.0)) * _234;
    vec4 _269[4] = vec4[](_110.color01, _110.color00, _110.color11, _110.color10);
    _out.tint = _269[uint(gl_VertexID)];
    float _274[4] = float[](_110.corner_radii.y, _110.corner_radii.x, _110.corner_radii.w, _110.corner_radii.z);
    _out.corner_radius = _274[uint(gl_VertexID)] * _224;
    _out.rect_half_size = _234;
    _out.border_thickness = _110.style.x * _224;
    _out.softness = _110.style.y * _224;
    _out.omit_texture = _110.style.z;
    gl_Position = _out.position;
    v0 = _out.texcoord;
    v1 = _out.sdf_sample_pos;
    v2 = _out.tint;
    v3 = _out.corner_radius;
    v4 = _out.rect_half_size;
    v5 = _out.border_thickness;
    v6 = _out.softness;
    v7 = _out.omit_texture;
}

