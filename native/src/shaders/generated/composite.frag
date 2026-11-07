#version 330

uniform sampler2D SPIRV_Cross_Combinedt_colors_color;

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
in vec2 v0;
layout(location = 0) out vec4 entryPointParam_fs_main;

void main()
{
    entryPointParam_fs_main = texture(SPIRV_Cross_Combinedt_colors_color, v0);
}

