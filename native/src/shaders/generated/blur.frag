#version 330

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

uniform sampler2D SPIRV_Cross_Combinedt_stages_stage;

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
in vec2 v0;
in vec2 v1;
flat in vec2 v3;
in float v2;
layout(location = 0) out vec4 entryPointParam_fs_main;

void main()
{
    uint i = 1u;
    vec3 color = texture(SPIRV_Cross_Combinedt_stages_stage, v0).xyz * u.kernel.data[0].x;
    for (;;)
    {
        if (!(i < u.blur_count))
        {
            break;
        }
        uint _247 = i;
        vec2 offset = u.direction * u.kernel.data[_247].y;
        i++;
        color = (color + (texture(SPIRV_Cross_Combinedt_stages_stage, v0 - offset).xyz * u.kernel.data[_247].x)) + (texture(SPIRV_Cross_Combinedt_stages_stage, v0 + offset).xyz * u.kernel.data[_247].x);
        continue;
    }
    bool _198;
    if (u.direction.y != 0.0)
    {
        _198 = (1.0 - smoothstep(0.0, 2.0, length(max((abs(v1) - v3) + vec2(v2), vec2(0.0))) - v2)) < 0.89999997615814208984375;
    }
    else
    {
        _198 = false;
    }
    if (_198)
    {
        discard;
    }
    entryPointParam_fs_main = vec4(color, 1.0);
}

