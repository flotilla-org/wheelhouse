// Effect contract (owned by the renderer; effects never edit this)
//
// An effect is a texture -> texture pass over a view surface: the source
// texture holds premultiplied linear color + coverage, & the effect writes the
// same. Effects compose by chaining passes through intermediate targets; the
// composite into the parent stays the standard rect path, sampling the chain's
// final output. The pass draws one fullscreen triangle with no blending.
//
// An effect file includes this prelude & defines exactly one function:
//
//   #include "effect_prelude.slang"
//   float4 effect(float2 uv, float2 frag_px)
//
//   uv       0..1 across the pass output (y-down, matching content space)
//   frag_px  output pixel coordinate (for pixel-grid tricks like masks)
//
// & samples the source via src(uv) / src_at(uv, offset_px). Knobs arrive in
// u.params0/u.params1 (meaning documented per effect). u.source_size_px &
// u.output_size_px are the pass dimensions (equal for now).
//
// Build: tools/generate-shaders.sh translates each effect to MSL and HLSL SM5
// (slangc) and GLSL 330 (slangc -> SPIR-V -> spirv-cross), into
// native/src/effects/generated, embedded per backend by effects_embed.h.

struct EffectUniforms
{
  float2 source_size_px;
  float2 output_size_px;
  float4 params0;
  float4 params1;
}

ConstantBuffer<EffectUniforms> u;
Texture2D t_source;
SamplerState s_source;

struct V2P
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
}

[shader("vertex")]
V2P vs_main(uint vid : SV_VertexID)
{
  // one triangle covering clip space; uv derived so 0..1 spans the output.
  // Wound clockwise in clip space, like the rect quad: D3D11's rasterizer
  // culls back faces, & a counter-clockwise triangle drew nothing there.
  V2P out;
  float x = float(int(vid >> 1) * 4 - 1);  // -1, -1, 3
  float y = float(int(vid & 1) * 4 - 1);   // -1, 3, -1
  out.position = float4(x, y, 0.0, 1.0);
  out.uv = float2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
  return out;
}

float4 src(float2 uv)
{
  return t_source.Sample(s_source, uv);
}

float4 src_at(float2 uv, float2 offset_px)
{
  return t_source.Sample(s_source, uv + offset_px / u.source_size_px);
}

[shader("fragment")]
float4 fs_main(V2P in) : SV_Target
{
  return effect(in.uv, in.position.xy);
}
