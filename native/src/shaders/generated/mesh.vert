#version 330

struct _Array_std140_vector_float_4_4
{
    vec4 data[4];
};

layout(std140) uniform MeshUniforms_std140
{
    _Array_std140_vector_float_4_4 view_projection;
} u;

layout(location = 4) in vec4 inst_xform0;
layout(location = 5) in vec4 inst_xform1;
layout(location = 6) in vec4 inst_xform2;
layout(location = 7) in vec4 inst_xform3;
layout(location = 0) in vec3 vtx_position;
layout(location = 8) in vec4 inst_albedo_remap;
layout(location = 2) in vec2 vtx_texcoord;
layout(location = 3) in vec3 vtx_color;
out vec2 v0;
out vec4 v1;

void main()
{
    vec4 _195 = (((inst_xform0 * vtx_position.x) + (inst_xform1 * vtx_position.y)) + (inst_xform2 * vtx_position.z)) + (inst_xform3 * 1.0);
    gl_Position = (((u.view_projection.data[0] * _195.x) + (u.view_projection.data[1] * _195.y)) + (u.view_projection.data[2] * _195.z)) + (u.view_projection.data[3] * _195.w);
    v0 = inst_albedo_remap.xy + (vtx_texcoord * inst_albedo_remap.zw);
    v1 = vec4(vtx_color, 1.0);
}

