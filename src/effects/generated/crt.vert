#version 330 core
struct Uniforms {
    vec2 viewport_size;
    float opacity;
    float _pad0_;
    vec2 texture_size;
    vec2 _pad1_;
    vec4 xform0_;
    vec4 xform1_;
    vec4 xform2_;
    vec4 params0_;
    vec4 params1_;
};
struct Inst {
    vec4 dst_rect_px;
    vec4 src_rect_px;
    vec4 color00_;
};
struct V2P {
    vec4 position;
    vec2 uv01_;
    vec4 src_rect_px;
    vec4 tint;
};
layout(std140) uniform Uniforms_block_0Vertex { Uniforms _group_0_binding_0_vs; };

layout(location = 0) in vec4 _p2vs_location0;
layout(location = 1) in vec4 _p2vs_location1;
layout(location = 2) in vec4 _p2vs_location2;
smooth out vec2 _vs2fs_location0;
flat out vec4 _vs2fs_location1;
flat out vec4 _vs2fs_location2;

void main() {
    Inst inst = Inst(_p2vs_location0, _p2vs_location1, _p2vs_location2);
    uint vid = uint(gl_VertexID);
    vec2 vertices[4] = vec2[4](vec2(-1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));
    vec2 dst_pos = vec2(0.0);
    V2P out_ = V2P(vec4(0.0), vec2(0.0), vec4(0.0), vec4(0.0));
    vec2 dst_half = ((inst.dst_rect_px.zw - inst.dst_rect_px.xy) * 0.5);
    vec2 dst_center = ((inst.dst_rect_px.zw + inst.dst_rect_px.xy) * 0.5);
    vec2 _e31 = vertices[vid];
    dst_pos = ((_e31 * dst_half) + dst_center);
    float _e38 = _group_0_binding_0_vs.xform0_.x;
    float _e42 = _group_0_binding_0_vs.xform0_.y;
    float _e46 = _group_0_binding_0_vs.xform0_.z;
    float _e51 = _group_0_binding_0_vs.xform1_.x;
    float _e55 = _group_0_binding_0_vs.xform1_.y;
    float _e59 = _group_0_binding_0_vs.xform1_.z;
    float _e64 = _group_0_binding_0_vs.xform2_.x;
    float _e68 = _group_0_binding_0_vs.xform2_.y;
    float _e72 = _group_0_binding_0_vs.xform2_.z;
    mat3x3 xform = mat3x3(vec3(_e38, _e42, _e46), vec3(_e51, _e55, _e59), vec3(_e64, _e68, _e72));
    vec2 _e75 = dst_pos;
    dst_pos = (xform * vec3(_e75, 1.0)).xy;
    float _e83 = dst_pos.x;
    float _e89 = _group_0_binding_0_vs.viewport_size.x;
    float _e94 = dst_pos.y;
    float _e98 = _group_0_binding_0_vs.viewport_size.y;
    out_.position = vec4((((2.0 * _e83) / _e89) - 1.0), ((2.0 * (1.0 - (_e94 / _e98))) - 1.0), 0.0, 1.0);
    out_.uv01_ = vec2((((vid & 2u) != 0u) ? 1.0 : 0.0), (((vid & 1u) != 0u) ? 1.0 : 0.0));
    out_.src_rect_px = inst.src_rect_px;
    out_.tint = inst.color00_;
    V2P _e129 = out_;
    gl_Position = _e129.position;
    _vs2fs_location0 = _e129.uv01_;
    _vs2fs_location1 = _e129.src_rect_px;
    _vs2fs_location2 = _e129.tint;
    return;
}

