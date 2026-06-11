// language: metal1.0
#include <metal_stdlib>
#include <simd/simd.h>

using metal::uint;

struct Uniforms {
    metal::float2 viewport_size;
    float opacity;
    float _pad0_;
    metal::float2 texture_size;
    metal::float2 _pad1_;
    metal::float4 xform0_;
    metal::float4 xform1_;
    metal::float4 xform2_;
    metal::float4 params0_;
    metal::float4 params1_;
};
struct Inst {
    metal::float4 dst_rect_px;
    metal::float4 src_rect_px;
    metal::float4 color00_;
};
struct V2P {
    metal::float4 position;
    metal::float2 uv01_;
    char _pad2[8];
    metal::float4 src_rect_px;
    metal::float4 tint;
};
struct type_6 {
    metal::float2 inner[4];
};

metal::float4 sample_src(
    metal::float2 uv01_,
    metal::float4 src_rect_px,
    constant Uniforms& u,
    metal::texture2d<float, metal::access::sample> t_surface,
    metal::sampler s_surface
) {
    metal::float2 src_px = metal::mix(src_rect_px.xy, src_rect_px.zw, uv01_);
    metal::float2 _e9 = u.texture_size;
    metal::float4 _e11 = t_surface.sample(s_surface, src_px / _e9);
    return _e11;
}

struct vs_mainInput {
    metal::float4 dst_rect_px [[attribute(0)]];
    metal::float4 src_rect_px [[attribute(1)]];
    metal::float4 color00_ [[attribute(2)]];
};
struct vs_mainOutput {
    metal::float4 position [[position]];
    metal::float2 uv01_ [[user(loc0), center_perspective]];
    metal::float4 src_rect_px [[user(loc1), flat]];
    metal::float4 tint [[user(loc2), flat]];
};
vertex vs_mainOutput vs_main(
  vs_mainInput varyings [[stage_in]]
, uint vid [[vertex_id]]
, constant Uniforms& u [[buffer(1)]]
) {
    const Inst inst = { varyings.dst_rect_px, varyings.src_rect_px, varyings.color00_ };
    type_6 vertices = type_6 {{metal::float2(-1.0, -1.0), metal::float2(-1.0, 1.0), metal::float2(1.0, -1.0), metal::float2(1.0, 1.0)}};
    metal::float2 dst_pos = {};
    V2P out = {};
    metal::float2 dst_half = (inst.dst_rect_px.zw - inst.dst_rect_px.xy) * 0.5;
    metal::float2 dst_center = (inst.dst_rect_px.zw + inst.dst_rect_px.xy) * 0.5;
    metal::float2 _e31 = vertices.inner[vid];
    dst_pos = (_e31 * dst_half) + dst_center;
    float _e38 = u.xform0_.x;
    float _e42 = u.xform0_.y;
    float _e46 = u.xform0_.z;
    float _e51 = u.xform1_.x;
    float _e55 = u.xform1_.y;
    float _e59 = u.xform1_.z;
    float _e64 = u.xform2_.x;
    float _e68 = u.xform2_.y;
    float _e72 = u.xform2_.z;
    metal::float3x3 xform = metal::float3x3(metal::float3(_e38, _e42, _e46), metal::float3(_e51, _e55, _e59), metal::float3(_e64, _e68, _e72));
    metal::float2 _e75 = dst_pos;
    dst_pos = (xform * metal::float3(_e75, 1.0)).xy;
    float _e83 = dst_pos.x;
    float _e89 = u.viewport_size.x;
    float _e94 = dst_pos.y;
    float _e98 = u.viewport_size.y;
    out.position = metal::float4(((2.0 * _e83) / _e89) - 1.0, (2.0 * (1.0 - (_e94 / _e98))) - 1.0, 0.0, 1.0);
    out.uv01_ = metal::float2(((vid & 2u) != 0u) ? 1.0 : 0.0, ((vid & 1u) != 0u) ? 1.0 : 0.0);
    out.src_rect_px = inst.src_rect_px;
    out.tint = inst.color00_;
    V2P _e129 = out;
    const auto _tmp = _e129;
    return vs_mainOutput { _tmp.position, _tmp.uv01_, _tmp.src_rect_px, _tmp.tint };
}

uint naga_f2u32(float value) {
    return static_cast<uint>(metal::clamp(value, 0.0, 4294967000.0));
}

uint naga_mod(uint lhs, uint rhs) {
    return lhs % metal::select(rhs, 1u, rhs == 0u);
}


struct fs_mainInput {
    metal::float2 uv01_ [[user(loc0), center_perspective]];
    metal::float4 src_rect_px [[user(loc1), flat]];
    metal::float4 tint [[user(loc2), flat]];
};
struct fs_mainOutput {
    metal::float4 member_1 [[color(0)]];
};
fragment fs_mainOutput fs_main(
  fs_mainInput varyings_1 [[stage_in]]
, metal::float4 position [[position]]
, constant Uniforms& u [[buffer(1)]]
, metal::texture2d<float, metal::access::sample> t_surface [[texture(0)]]
, metal::sampler s_surface [[sampler(0)]]
) {
    const V2P in = { position, varyings_1.uv01_, {}, varyings_1.src_rect_px, varyings_1.tint };
    metal::float2 uv = {};
    bool local = {};
    bool local_1 = {};
    bool local_2 = {};
    metal::float4 color = {};
    metal::float3 mask_mul = {};
    metal::float3 rgb = {};
    float a = {};
    float scanline_intensity = u.params0_.x;
    float curvature = u.params0_.y;
    float vignette_strength = u.params0_.z;
    float mask_strength = u.params0_.w;
    float rgb_shift_px = u.params1_.x;
    float brightness = u.params1_.y;
    metal::float2 centered = in.uv01_ - metal::float2(0.5, 0.5);
    float r2_ = metal::dot(centered, centered);
    uv = in.uv01_ + (centered * ((r2_ * curvature) * 2.0));
    float _e39 = uv.x;
    if (!((_e39 < 0.0))) {
        float _e46 = uv.x;
        local = _e46 > 1.0;
    } else {
        local = true;
    }
    bool _e50 = local;
    if (!(_e50)) {
        float _e55 = uv.y;
        local_1 = _e55 < 0.0;
    } else {
        local_1 = true;
    }
    bool _e59 = local_1;
    if (!(_e59)) {
        float _e64 = uv.y;
        local_2 = _e64 > 1.0;
    } else {
        local_2 = true;
    }
    bool _e68 = local_2;
    if (_e68) {
        return fs_mainOutput { metal::float4(0.0) };
    }
    metal::float2 shift01_ = metal::float2(rgb_shift_px / metal::max(in.src_rect_px.z - in.src_rect_px.x, 1.0), 0.0);
    metal::float2 _e81 = uv;
    metal::float4 _e83 = sample_src(_e81, in.src_rect_px, u, t_surface, s_surface);
    color = _e83;
    if (rgb_shift_px > 0.001) {
        metal::float2 _e88 = uv;
        metal::float4 _e91 = sample_src(_e88 + shift01_, in.src_rect_px, u, t_surface, s_surface);
        color.x = _e91.x;
        metal::float2 _e94 = uv;
        metal::float4 _e97 = sample_src(_e94 - shift01_, in.src_rect_px, u, t_surface, s_surface);
        color.z = _e97.z;
    }
    float src_rows = in.src_rect_px.w - in.src_rect_px.y;
    float _e105 = uv.y;
    float scan_phase = (_e105 * src_rows) * 3.1415927;
    float scan = 1.0 - (scanline_intensity * (0.35 + ((0.65 * metal::sin(scan_phase)) * metal::sin(scan_phase))));
    uint mask_sel = naga_mod(naga_f2u32(in.position.x), 3u);
    mask_mul = metal::float3(1.0, 1.0, 1.0) - metal::float3(mask_strength * 0.45);
    if (mask_sel == 0u) {
        mask_mul.x = 1.0;
    } else {
        if (mask_sel == 1u) {
            mask_mul.y = 1.0;
        } else {
            mask_mul.z = 1.0;
        }
    }
    float vig = 1.0 - ((vignette_strength * r2_) * 1.5);
    metal::float4 _e148 = color;
    metal::float3 _e150 = mask_mul;
    rgb = (((_e148.xyz * _e150) * brightness) * scan) * metal::max(vig, 0.0);
    float _e159 = color.w;
    a = (_e159 * scan) * metal::max(vig, 0.0);
    metal::float3 _e165 = rgb;
    float _e166 = a;
    float _e172 = u.opacity;
    return fs_mainOutput { (metal::float4(_e165, _e166) * in.tint) * _e172 };
}
