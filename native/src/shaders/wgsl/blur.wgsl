// Separable Gaussian blur of the stage inside a rounded rect, in two passes:
// horizontal (stage -> scratch) then vertical (scratch -> stage). The kernel
// folds each pair of taps into one bilinear sample, so the sampler must be
// linear. The horizontal pass covers the vertical pass's whole input
// footprint (the caller widens its rect); only the vertical pass masks to
// the rounded rect, since masked scratch pixels would be stale inputs.

struct BlurUniforms
{
  rect: vec4f,              // x0, y0, x1, y1 in the pass's points
  corner_radii: vec4f,      // 00, 01, 10, 11
  direction: vec2f,         // one texel along the pass's axis, in uv
  viewport_size: vec2f,
  blur_count: u32,          // kernel entries in use, centre included
  texcoord_flip_y: f32,     // 1 where the stage is stored bottom-up (OpenGL)
  kernel: array<vec4f, 32>, // x: weight, y: offset in texels
}

@group(0) @binding(0) var<uniform> u: BlurUniforms;
@group(0) @binding(1) var t_stage: texture_2d<f32>;
@group(0) @binding(2) var s_stage: sampler;

struct V2P
{
  @builtin(position) position: vec4f,
  @location(0) texcoord: vec2f,
  @location(1) sdf_sample_pos: vec2f,
  @location(2) corner_radius: f32,
  @location(3) @interpolate(flat) rect_half_size: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> V2P
{
  let pct = corner_pct(vid);
  let p = vec2f(mix(u.rect.x, u.rect.z, pct.x), mix(u.rect.y, u.rect.w, pct.y));
  let radii = array<f32, 4>(u.corner_radii.y, u.corner_radii.x, u.corner_radii.w, u.corner_radii.z);
  let half_size = (u.rect.zw - u.rect.xy) * 0.5;

  var texcoord = p / u.viewport_size;
  if(u.texcoord_flip_y > 0.5)
  {
    texcoord.y = 1.0 - texcoord.y;
  }

  var out: V2P;
  out.position = ndc_from_points(p, u.viewport_size);
  out.texcoord = texcoord;
  out.sdf_sample_pos = (2.0 * pct - 1.0) * half_size;
  out.corner_radius = radii[vid];
  out.rect_half_size = half_size - 2.0;
  return out;
}

@fragment
fn fs_main(in: V2P) -> @location(0) vec4f
{
  var color = u.kernel[0].x * textureSample(t_stage, s_stage, in.texcoord).rgb;
  for(var i = 1u; i < u.blur_count; i += 1u)
  {
    let weight = u.kernel[i].x;
    let offset = u.kernel[i].y * u.direction;
    color += weight * textureSample(t_stage, s_stage, in.texcoord - offset).rgb;
    color += weight * textureSample(t_stage, s_stage, in.texcoord + offset).rgb;
  }
  let corner_t = 1.0 - smoothstep(0.0, 2.0, rect_sdf(in.sdf_sample_pos, in.rect_half_size, in.corner_radius));
  if(u.direction.y != 0.0 && corner_t < 0.9)
  {
    discard;
  }
  return vec4f(color, 1.0);
}
