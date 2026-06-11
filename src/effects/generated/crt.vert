#version 330
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif

struct EffectUniforms
{
    vec2 source_size_px;
    vec2 output_size_px;
    vec4 params0;
    vec4 params1;
};

struct V2P
{
    vec4 position;
    vec2 uv;
};

layout(binding = 0, std140) uniform u
{
    EffectUniforms _m0;
} u_1;

out vec2 uv;

void main()
{
    V2P _out = V2P(vec4(0.0), vec2(0.0));
    float _220 = float((int(uint(gl_VertexID) & 1u) * 4) - 1);
    float _225 = float((int(uint(gl_VertexID) >> 1u) * 4) - 1);
    _out.position = vec4(_220, _225, 0.0, 1.0);
    _out.uv = vec2((_220 + 1.0) * 0.5, 1.0 - ((_225 + 1.0) * 0.5));
    gl_Position = _out.position;
    uv = _out.uv;
}

