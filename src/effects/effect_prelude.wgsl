// Effect contract v1 (owned by the renderer; effects never edit this)
//
// An effect is a texture -> texture pass over a view surface: the source
// texture holds premultiplied linear color + coverage, & the effect writes the
// same. Effects compose by chaining passes through intermediate targets; the
// composite into the parent stays the standard rect path, sampling the chain's
// final output. The pass draws one fullscreen triangle with no blending.
//
// An effect file defines exactly one function:
//
//   fn effect(uv: vec2f, frag_px: vec2f) -> vec4f
//
//   uv       0..1 across the pass output (y-down, matching content space)
//   frag_px  output pixel coordinate (for pixel-grid tricks like masks)
//
// & samples the source via src(uv) / src_at(uv_offset_px). Knobs arrive in
// u.params0/u.params1 (meaning documented per effect). u.source_size_px &
// u.output_size_px are the pass dimensions (equal in v1).
//
// Build: `./build.sh effects` concatenates this prelude with each effect &
// translates WGSL -> SPIR-V (naga) -> MSL / HLSL SM5.0 / GLSL 330 (spirv-cross).

struct EffectUniforms
{
  source_size_px: vec2f,
  output_size_px: vec2f,
  params0: vec4f,
  params1: vec4f,
}

@group(0) @binding(0) var<uniform> u: EffectUniforms;
@group(0) @binding(1) var t_source: texture_2d<f32>;
@group(0) @binding(2) var s_source: sampler;

struct V2P
{
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> V2P
{
  // one triangle covering clip space; uv derived so 0..1 spans the output
  var out: V2P;
  let x = f32(i32(vid & 1u) * 4 - 1);   // -1, 3, -1
  let y = f32(i32(vid >> 1u) * 4 - 1);  // -1, -1, 3
  out.position = vec4f(x, y, 0.0, 1.0);
  out.uv = vec2f((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
  return out;
}

fn src(uv: vec2f) -> vec4f
{
  return textureSample(t_source, s_source, uv);
}

fn src_at(uv: vec2f, offset_px: vec2f) -> vec4f
{
  return textureSample(t_source, s_source, uv + offset_px / u.source_size_px);
}

@fragment
fn fs_main(in: V2P) -> @location(0) vec4f
{
  return effect(in.uv, in.position.xy);
}
