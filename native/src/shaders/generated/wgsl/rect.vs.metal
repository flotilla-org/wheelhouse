#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wmissing-braces"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template<typename T, size_t Num>
struct spvUnsafeArray
{
    T elements[Num ? Num : 1];
    
    thread T& operator [] (size_t pos) thread
    {
        return elements[pos];
    }
    constexpr const thread T& operator [] (size_t pos) const thread
    {
        return elements[pos];
    }
    
    device T& operator [] (size_t pos) device
    {
        return elements[pos];
    }
    constexpr const device T& operator [] (size_t pos) const device
    {
        return elements[pos];
    }
    
    constexpr const constant T& operator [] (size_t pos) const constant
    {
        return elements[pos];
    }
    
    threadgroup T& operator [] (size_t pos) threadgroup
    {
        return elements[pos];
    }
    constexpr const threadgroup T& operator [] (size_t pos) const threadgroup
    {
        return elements[pos];
    }
};

struct RectUniforms
{
    float2 viewport_size;
    float opacity;
    float sample_is_surface;
    float4x4 texture_sample_channel_map;
    float2 texture_size;
    float2 xform_scale;
    float4 xform[3];
    float surface_flip_y;
};

struct Instance
{
    float4 dst_rect;
    float4 src_rect;
    float4 color00;
    float4 color01;
    float4 color10;
    float4 color11;
    float4 corner_radii;
    float4 style;
};

struct V2P
{
    float4 position;
    float2 texcoord;
    float2 sdf_sample_pos;
    float4 tint;
    float corner_radius;
    float2 rect_half_size;
    float border_thickness;
    float softness;
    float omit_texture;
};

struct _22
{
    RectUniforms _m0;
};

struct vs_main_out
{
    float2 texcoord [[user(locn0)]];
    float2 sdf_sample_pos [[user(locn1)]];
    float4 tint [[user(locn2)]];
    float corner_radius [[user(locn3)]];
    float2 rect_half_size [[user(locn4)]];
    float border_thickness [[user(locn5)]];
    float softness [[user(locn6)]];
    float omit_texture [[user(locn7)]];
    float4 gl_Position [[position]];
};

struct vs_main_in
{
    float4 dst_rect [[attribute(0)]];
    float4 src_rect [[attribute(1)]];
    float4 color00 [[attribute(2)]];
    float4 color01 [[attribute(3)]];
    float4 color10 [[attribute(4)]];
    float4 color11 [[attribute(5)]];
    float4 corner_radii [[attribute(6)]];
    float4 style [[attribute(7)]];
};

static inline __attribute__((always_inline))
float2 corner_pct(uint vid)
{
    return float2(float((vid >> 1u) != 0u), ((vid & 1u) != 0u) ? 0.0 : 1.0);
}

static inline __attribute__((always_inline))
float4 ndc_from_points(float2 p, float2 viewport_size)
{
    return float4(((2.0 * p.x) / viewport_size.x) - 1.0, 1.0 - ((2.0 * p.y) / viewport_size.y), 0.0, 1.0);
}

vertex vs_main_out vs_main(vs_main_in in [[stage_in]], constant _22& u [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    float2 texcoord = float2(0.0);
    bool _150 = false;
    V2P _out = V2P{ float4(0.0), float2(0.0), float2(0.0), float4(0.0), 0.0, float2(0.0), 0.0, 0.0, 0.0 };
    vs_main_out out = {};
    Instance _110 = Instance{ in.dst_rect, in.src_rect, in.color00, in.color01, in.color10, in.color11, in.corner_radii, in.style };
    float2 _157 = corner_pct(gl_VertexIndex);
    spvUnsafeArray<float, 4> _198 = spvUnsafeArray<float, 4>({ _110.corner_radii.y, _110.corner_radii.x, _110.corner_radii.w, _110.corner_radii.z });
    spvUnsafeArray<float4, 4> _203 = spvUnsafeArray<float4, 4>({ _110.color01, _110.color00, _110.color11, _110.color10 });
    float3 _204 = float3(float2(mix(_110.dst_rect.x, _110.dst_rect.z, _157.x), mix(_110.dst_rect.y, _110.dst_rect.w, _157.y) + ((_157.x > 0.5) ? _110.style.w : 0.0)), 1.0);
    float _224 = fast::max(u._m0.xform_scale.x, u._m0.xform_scale.y);
    float2 _234 = (abs(_110.dst_rect.zw - _110.dst_rect.xy) * 0.5) * u._m0.xform_scale;
    texcoord = float2(mix(_110.src_rect.x, _110.src_rect.z, _157.x), mix(_110.src_rect.y, _110.src_rect.w, _157.y)) / u._m0.texture_size;
    if (u._m0.sample_is_surface > 0.5)
    {
        _150 = u._m0.surface_flip_y > 0.5;
    }
    else
    {
        _150 = false;
    }
    if (_150)
    {
        texcoord.y = 1.0 - texcoord.y;
    }
    _out.position = ndc_from_points(float2(dot(u._m0.xform[0u].xyz, _204), dot(u._m0.xform[1u].xyz, _204)), u._m0.viewport_size);
    _out.texcoord = texcoord;
    _out.sdf_sample_pos = ((_157 * 2.0) - float2(1.0)) * _234;
    spvUnsafeArray<float4, 4> _269 = _203;
    _out.tint = _269[gl_VertexIndex];
    spvUnsafeArray<float, 4> _274 = _198;
    _out.corner_radius = _274[gl_VertexIndex] * _224;
    _out.rect_half_size = _234;
    _out.border_thickness = _110.style.x * _224;
    _out.softness = _110.style.y * _224;
    _out.omit_texture = _110.style.z;
    out.gl_Position = _out.position;
    out.texcoord = _out.texcoord;
    out.sdf_sample_pos = _out.sdf_sample_pos;
    out.tint = _out.tint;
    out.corner_radius = _out.corner_radius;
    out.rect_half_size = _out.rect_half_size;
    out.border_thickness = _out.border_thickness;
    out.softness = _out.softness;
    out.omit_texture = _out.omit_texture;
    return out;
}

