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
layout(std140) uniform Uniforms_block_0Fragment { Uniforms _group_0_binding_0_fs; };

uniform sampler2D _group_0_binding_1_fs;

smooth in vec2 _vs2fs_location0;
flat in vec4 _vs2fs_location1;
flat in vec4 _vs2fs_location2;
layout(location = 0) out vec4 _fs2p_location0;

vec4 sample_src(vec2 uv01_, vec4 src_rect_px) {
    vec2 src_px = mix(src_rect_px.xy, src_rect_px.zw, uv01_);
    vec2 _e9 = _group_0_binding_0_fs.texture_size;
    vec4 _e11 = texture(_group_0_binding_1_fs, vec2((src_px / _e9)));
    return _e11;
}

void main() {
    V2P in_ = V2P(gl_FragCoord, _vs2fs_location0, _vs2fs_location1, _vs2fs_location2);
    vec2 uv = vec2(0.0);
    bool local = false;
    bool local_1 = false;
    bool local_2 = false;
    vec4 color = vec4(0.0);
    vec3 mask_mul = vec3(0.0);
    vec3 rgb = vec3(0.0);
    float a = 0.0;
    float scanline_intensity = _group_0_binding_0_fs.params0_.x;
    float curvature = _group_0_binding_0_fs.params0_.y;
    float vignette_strength = _group_0_binding_0_fs.params0_.z;
    float mask_strength = _group_0_binding_0_fs.params0_.w;
    float rgb_shift_px = _group_0_binding_0_fs.params1_.x;
    float brightness = _group_0_binding_0_fs.params1_.y;
    vec2 centered = (in_.uv01_ - vec2(0.5, 0.5));
    float r2_ = dot(centered, centered);
    uv = (in_.uv01_ + (centered * ((r2_ * curvature) * 2.0)));
    float _e39 = uv.x;
    if (!((_e39 < 0.0))) {
        float _e46 = uv.x;
        local = (_e46 > 1.0);
    } else {
        local = true;
    }
    bool _e50 = local;
    if (!(_e50)) {
        float _e55 = uv.y;
        local_1 = (_e55 < 0.0);
    } else {
        local_1 = true;
    }
    bool _e59 = local_1;
    if (!(_e59)) {
        float _e64 = uv.y;
        local_2 = (_e64 > 1.0);
    } else {
        local_2 = true;
    }
    bool _e68 = local_2;
    if (_e68) {
        _fs2p_location0 = vec4(0.0);
        return;
    }
    vec2 shift01_ = vec2((rgb_shift_px / max((in_.src_rect_px.z - in_.src_rect_px.x), 1.0)), 0.0);
    vec2 _e81 = uv;
    vec4 _e83 = sample_src(_e81, in_.src_rect_px);
    color = _e83;
    if ((rgb_shift_px > 0.001)) {
        vec2 _e88 = uv;
        vec4 _e91 = sample_src((_e88 + shift01_), in_.src_rect_px);
        color.x = _e91.x;
        vec2 _e94 = uv;
        vec4 _e97 = sample_src((_e94 - shift01_), in_.src_rect_px);
        color.z = _e97.z;
    }
    float src_rows = (in_.src_rect_px.w - in_.src_rect_px.y);
    float _e105 = uv.y;
    float scan_phase = ((_e105 * src_rows) * 3.1415927);
    float scan = (1.0 - (scanline_intensity * (0.35 + ((0.65 * sin(scan_phase)) * sin(scan_phase)))));
    uint mask_sel = (uint(in_.position.x) % 3u);
    mask_mul = (vec3(1.0, 1.0, 1.0) - vec3((mask_strength * 0.45)));
    if ((mask_sel == 0u)) {
        mask_mul.x = 1.0;
    } else {
        if ((mask_sel == 1u)) {
            mask_mul.y = 1.0;
        } else {
            mask_mul.z = 1.0;
        }
    }
    float vig = (1.0 - ((vignette_strength * r2_) * 1.5));
    vec4 _e148 = color;
    vec3 _e150 = mask_mul;
    rgb = ((((_e148.xyz * _e150) * brightness) * scan) * max(vig, 0.0));
    float _e159 = color.w;
    a = ((_e159 * scan) * max(vig, 0.0));
    vec3 _e165 = rgb;
    float _e166 = a;
    float _e172 = _group_0_binding_0_fs.opacity;
    _fs2p_location0 = ((vec4(_e165, _e166) * in_.tint) * _e172);
    return;
}

