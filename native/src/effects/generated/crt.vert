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
    float x = float((int(uint(gl_VertexID - SPIRV_Cross_BaseVertex) & 1u) * 4) - 1);
    float y = float((int(uint(gl_VertexID - SPIRV_Cross_BaseVertex) >> uint(1)) * 4) - 1);
    gl_Position = vec4(x, y, 0.0, 1.0);
    v0 = vec2((x + 1.0) * 0.5, 1.0 - ((y + 1.0) * 0.5));
    gl_Position.y = -gl_Position.y;
}

