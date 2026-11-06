#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

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

struct fs_main_out
{
    float4 m_218 [[color(0)]];
};

struct fs_main_in
{
    float2 texcoord [[user(locn0)]];
    float2 sdf_sample_pos [[user(locn1)]];
    float corner_radius [[user(locn2)]];
    float2 rect_half_size [[user(locn3), flat]];
};

static inline __attribute__((always_inline))
float rect_sdf(float2 sample_pos, float2 rect_half_size, float r)
{
    return length(fast::max((abs(sample_pos) - rect_half_size) + float2(r), float2(0.0))) - r;
}

fragment fs_main_out fs_main(fs_main_in in [[stage_in]], constant _19& u [[buffer(0)]], texture2d<float> t_stage [[texture(0)]], sampler s_stage [[sampler(0)]], float4 gl_FragCoord [[position]])
{
    float3 color = float3(0.0);
    uint i = 1u;
    bool _230 = false;
    uint2 loop_bound = uint2(4294967295u);
    fs_main_out out = {};
    V2P _204 = V2P{ gl_FragCoord, in.texcoord, in.sdf_sample_pos, in.corner_radius, in.rect_half_size };
    color = t_stage.sample(s_stage, _204.texcoord).xyz * u._m0.kernel0[0u].x;
    for (;;)
    {
        if (all(uint2(0u) == loop_bound))
        {
            break;
        }
        loop_bound -= uint2(uint(loop_bound.y == 0u), 1u);
        if (!(i < u._m0.blur_count))
        {
            break;
        }
        float2 _282 = u._m0.direction * u._m0.kernel0[i].y;
        color += (t_stage.sample(s_stage, (_204.texcoord - _282)).xyz * u._m0.kernel0[i].x);
        color += (t_stage.sample(s_stage, (_204.texcoord + _282)).xyz * u._m0.kernel0[i].x);
        i++;
        continue;
    }
    if (u._m0.direction.y != 0.0)
    {
        _230 = (1.0 - smoothstep(0.0, 2.0, rect_sdf(_204.sdf_sample_pos, _204.rect_half_size, _204.corner_radius))) < 0.89999997615814208984375;
    }
    else
    {
        _230 = false;
    }
    if (_230)
    {
        discard_fragment();
    }
    out.m_218 = float4(color, 1.0);
    return out;
}

