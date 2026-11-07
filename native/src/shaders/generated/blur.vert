#version 330
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

struct _Array_std140_vector_float_4_32
{
    vec4 data[32];
};

layout(std140) uniform BlurUniforms_std140
{
    vec4 rect;
    vec4 corner_radii;
    vec2 direction;
    vec2 viewport_size;
    uint blur_count;
    float texcoord_flip_y;
    _Array_std140_vector_float_4_32 kernel;
} u;

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
out vec2 v0;
out vec2 v1;
out float v2;
flat out vec2 v3;

void main()
{
    uint _27 = uint(gl_VertexID - SPIRV_Cross_BaseVertex);
    float _330;
    if ((_27 >> uint(1)) != 0u)
    {
        _330 = 1.0;
    }
    else
    {
        _330 = 0.0;
    }
    float _331;
    if ((_27 & 1u) != 0u)
    {
        _331 = 0.0;
    }
    else
    {
        _331 = 1.0;
    }
    vec2 _346 = vec2(_330, _331);
    float _80 = mix(u.rect.x, u.rect.z, _346.x);
    float _87 = mix(u.rect.y, u.rect.w, _346.y);
    float _10[4] = float[](u.corner_radii.y, u.corner_radii.x, u.corner_radii.w, u.corner_radii.z);
    vec2 half_size = (u.rect.zw - u.rect.xy) * 0.5;
    vec2 texcoord = vec2(_80, _87) / u.viewport_size;
    if (u.texcoord_flip_y > 0.5)
    {
        vec2 _120 = texcoord;
        _120.y = 1.0 - _120.y;
        texcoord = _120;
    }
    gl_Position = vec4(((2.0 * _80) / u.viewport_size.x) - 1.0, 1.0 - ((2.0 * _87) / u.viewport_size.y), 0.0, 1.0);
    v0 = texcoord;
    v1 = ((_346 * 2.0) - vec2(1.0)) * half_size;
    v2 = _10[uint(gl_VertexID - SPIRV_Cross_BaseVertex)];
    v3 = half_size - vec2(2.0);
}

