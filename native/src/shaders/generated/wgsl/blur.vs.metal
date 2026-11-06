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

struct BlurUniforms
{
    float4 rect;
    float4 corner_radii;
    float2 direction;
    float2 viewport_size;
    uint blur_count;
    float texcoord_flip_y;
    float4 kernel0[32];
};

struct V2P
{
    float4 position;
    float2 texcoord;
    float2 sdf_sample_pos;
    float corner_radius;
    float2 rect_half_size;
};

struct _19
{
    BlurUniforms _m0;
};

struct vs_main_out
{
    float2 texcoord [[user(locn0)]];
    float2 sdf_sample_pos [[user(locn1)]];
    float corner_radius [[user(locn2)]];
    float2 rect_half_size [[user(locn3)]];
    float4 gl_Position [[position]];
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

vertex vs_main_out vs_main(constant _19& u [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    float2 texcoord = float2(0.0);
    V2P _out = V2P{ float4(0.0), float2(0.0), float2(0.0), 0.0, float2(0.0) };
    vs_main_out out = {};
    float2 _129 = corner_pct(gl_VertexIndex);
    float2 _146 = float2(mix(u._m0.rect.x, u._m0.rect.z, _129.x), mix(u._m0.rect.y, u._m0.rect.w, _129.y));
    spvUnsafeArray<float, 4> _155 = spvUnsafeArray<float, 4>({ u._m0.corner_radii.y, u._m0.corner_radii.x, u._m0.corner_radii.w, u._m0.corner_radii.z });
    float2 _163 = (u._m0.rect.zw - u._m0.rect.xy) * 0.5;
    texcoord = _146 / u._m0.viewport_size;
    if (u._m0.texcoord_flip_y > 0.5)
    {
        texcoord.y = 1.0 - texcoord.y;
    }
    _out.position = ndc_from_points(_146, u._m0.viewport_size);
    _out.texcoord = texcoord;
    _out.sdf_sample_pos = ((_129 * 2.0) - float2(1.0)) * _163;
    spvUnsafeArray<float, 4> _191 = _155;
    _out.corner_radius = _191[gl_VertexIndex];
    _out.rect_half_size = _163 - float2(2.0);
    out.gl_Position = _out.position;
    out.texcoord = _out.texcoord;
    out.sdf_sample_pos = _out.sdf_sample_pos;
    out.corner_radius = _out.corner_radius;
    out.rect_half_size = _out.rect_half_size;
    return out;
}

