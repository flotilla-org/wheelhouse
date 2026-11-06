#version 330

struct BlurUniforms
{
    vec4 rect;
    vec4 corner_radii;
    vec2 direction;
    vec2 viewport_size;
    uint blur_count;
    float texcoord_flip_y;
    vec4 kernel[32];
};

struct V2P
{
    vec4 position;
    vec2 texcoord;
    vec2 sdf_sample_pos;
    float corner_radius;
    vec2 rect_half_size;
};

layout(std140) uniform u
{
    BlurUniforms _m0;
} u_1;

out vec2 v0;
out vec2 v1;
out float v2;
flat out vec2 v3;

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
    V2P _out = V2P(vec4(0.0), vec2(0.0), vec2(0.0), 0.0, vec2(0.0));
    vec2 _129 = corner_pct(uint(gl_VertexID));
    vec2 _146 = vec2(mix(u_1._m0.rect.x, u_1._m0.rect.z, _129.x), mix(u_1._m0.rect.y, u_1._m0.rect.w, _129.y));
    vec2 _163 = (u_1._m0.rect.zw - u_1._m0.rect.xy) * 0.5;
    texcoord = _146 / u_1._m0.viewport_size;
    if (u_1._m0.texcoord_flip_y > 0.5)
    {
        texcoord.y = 1.0 - texcoord.y;
    }
    _out.position = ndc_from_points(_146, u_1._m0.viewport_size);
    _out.texcoord = texcoord;
    _out.sdf_sample_pos = ((_129 * 2.0) - vec2(1.0)) * _163;
    float _191[4] = float[](u_1._m0.corner_radii.y, u_1._m0.corner_radii.x, u_1._m0.corner_radii.w, u_1._m0.corner_radii.z);
    _out.corner_radius = _191[uint(gl_VertexID)];
    _out.rect_half_size = _163 - vec2(2.0);
    gl_Position = _out.position;
    v0 = _out.texcoord;
    v1 = _out.sdf_sample_pos;
    v2 = _out.corner_radius;
    v3 = _out.rect_half_size;
}

