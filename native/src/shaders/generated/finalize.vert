#version 330
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
out vec2 v0;

void main()
{
    uint _19 = uint(gl_VertexID - SPIRV_Cross_BaseVertex);
    float _106;
    if ((_19 >> uint(1)) != 0u)
    {
        _106 = 1.0;
    }
    else
    {
        _106 = 0.0;
    }
    float _107;
    if ((_19 & 1u) != 0u)
    {
        _107 = 0.0;
    }
    else
    {
        _107 = 1.0;
    }
    vec2 _122 = vec2(_106, _107);
    gl_Position = vec4((2.0 * _122.x) - 1.0, 1.0 - (2.0 * _122.y), 0.0, 1.0);
    v0 = _122;
}

