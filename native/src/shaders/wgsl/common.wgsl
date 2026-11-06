// Shared by every WGSL core shader. WGSL has no includes: `native/build.sh
// shaders` concatenates this file in front of each shader, as the effects
// prelude is.

fn rect_sdf(sample_pos: vec2f, rect_half_size: vec2f, r: f32) -> f32
{
  return length(max(abs(sample_pos) - rect_half_size + r, vec2f(0.0))) - r;
}

fn linear_from_srgb_f32(x: f32) -> f32
{
  return select(pow((x + 0.055) / 1.055, 2.4), x / 12.92, x < 0.0404482362771082);
}

fn linear_from_srgba(v: vec4f) -> vec4f
{
  return vec4f(linear_from_srgb_f32(v.x), linear_from_srgb_f32(v.y), linear_from_srgb_f32(v.z), v.w);
}

// Clip space from a position in the pass's points, y down.
fn ndc_from_points(p: vec2f, viewport_size: vec2f) -> vec4f
{
  return vec4f(2.0 * p.x / viewport_size.x - 1.0, 1.0 - 2.0 * p.y / viewport_size.y, 0.0, 1.0);
}

// The quad's corners in triangle-strip order, as fractions of the rect:
// (x0,y1), (x0,y0), (x1,y1), (x1,y0).
fn corner_pct(vid: u32) -> vec2f
{
  return vec2f(select(0.0, 1.0, (vid >> 1u) != 0u), select(1.0, 0.0, (vid & 1u) != 0u));
}
