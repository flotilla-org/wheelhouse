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

uniform sampler2D SPIRV_Cross_Combinedt_stages_stage;

in vec2 v0;
in vec2 v1;
in float v2;
flat in vec2 v3;
layout(location = 0) out vec4 _218;

float rect_sdf(vec2 sample_pos, vec2 rect_half_size, float r)
{
    return length(max((abs(sample_pos) - rect_half_size) + vec2(r), vec2(0.0))) - r;
}

void main()
{
    vec3 color = vec3(0.0);
    uint i = 1u;
    bool _230 = false;
    uvec2 loop_bound = uvec2(4294967295u);
    V2P _204 = V2P(gl_FragCoord, v0, v1, v2, v3);
    color = texture(SPIRV_Cross_Combinedt_stages_stage, _204.texcoord).xyz * u_1._m0.kernel[0u].x;
    for (;;)
    {
        if (all(equal(uvec2(0u), loop_bound)))
        {
            break;
        }
        loop_bound -= uvec2(uint(loop_bound.y == 0u), 1u);
        if (!(i < u_1._m0.blur_count))
        {
            break;
        }
        vec2 _282 = u_1._m0.direction * u_1._m0.kernel[i].y;
        color += (texture(SPIRV_Cross_Combinedt_stages_stage, _204.texcoord - _282).xyz * u_1._m0.kernel[i].x);
        color += (texture(SPIRV_Cross_Combinedt_stages_stage, _204.texcoord + _282).xyz * u_1._m0.kernel[i].x);
        i++;
        continue;
    }
    if (u_1._m0.direction.y != 0.0)
    {
        _230 = (1.0 - smoothstep(0.0, 2.0, rect_sdf(_204.sdf_sample_pos, _204.rect_half_size, _204.corner_radius))) < 0.89999997615814208984375;
    }
    else
    {
        _230 = false;
    }
    if (_230)
    {
        discard;
    }
    _218 = vec4(color, 1.0);
}

