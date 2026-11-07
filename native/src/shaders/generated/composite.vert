#version 330
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

layout(std140) uniform CompositeUniforms_std140
{
    vec4 dst_ndc;
    vec4 src_uv;
} u;

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
out vec2 v0;

void main()
{
    uint _19 = uint(gl_VertexID - SPIRV_Cross_BaseVertex);
    float _113;
    if ((_19 >> uint(1)) != 0u)
    {
        _113 = 1.0;
    }
    else
    {
        _113 = 0.0;
    }
    float _114;
    if ((_19 & 1u) != 0u)
    {
        _114 = 0.0;
    }
    else
    {
        _114 = 1.0;
    }
    vec2 _129 = vec2(_113, _114);
    gl_Position = vec4(mix(u.dst_ndc.xy, u.dst_ndc.zw, _129), 0.0, 1.0);
    v0 = mix(u.src_uv.xy, u.src_uv.zw, _129);
}

