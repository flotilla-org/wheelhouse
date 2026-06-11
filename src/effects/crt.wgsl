// CRT view-surface effect — the first composite-time effect, & the template for
// the effect contract (v1):
//
//  - an effect is a whole pipeline (vertex + fragment) authored in WGSL,
//    translated per-backend offline by naga (`build.sh effects`)
//  - it draws the standard rect-instance stream (R_Rect2DInst layout, locations
//    pinned below); a triangle-strip of 4 vertices per instance
//  - uniforms at group(0) binding(0), surface texture at binding(1), sampler at
//    binding(2); params are 2 vec4s of effect-specific knobs
//  - the surface texture holds premultiplied linear color + coverage; effects
//    work in premultiplied space & output premultiplied, composited with
//    (One, OneMinusSrcAlpha) on both color & alpha
//
// knobs:
//  params0.x  scanline intensity   (0..1)
//  params0.y  curvature            (0..0.5)
//  params0.z  vignette strength    (0..2)
//  params0.w  aperture mask        (0..1)
//  params1.x  rgb shift, src px    (0..2)
//  params1.y  brightness           (0.5..1.8)

struct Uniforms
{
  viewport_size: vec2f,
  opacity: f32,
  _pad0: f32,
  texture_size: vec2f,
  _pad1: vec2f,
  xform0: vec4f,
  xform1: vec4f,
  xform2: vec4f,
  params0: vec4f,
  params1: vec4f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var t_surface: texture_2d<f32>;
@group(0) @binding(2) var s_surface: sampler;

struct Inst
{
  @location(0) dst_rect_px: vec4f,
  @location(1) src_rect_px: vec4f,
  @location(2) color00: vec4f,
}

struct V2P
{
  @builtin(position) position: vec4f,
  @location(0) uv01: vec2f,
  @location(1) @interpolate(flat) src_rect_px: vec4f,
  @location(2) @interpolate(flat) tint: vec4f,
}

@vertex
fn vs_main(inst: Inst, @builtin(vertex_index) vid: u32) -> V2P
{
  var vertices = array<vec2f, 4>(vec2f(-1.0, -1.0), vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0));
  let dst_half = (inst.dst_rect_px.zw - inst.dst_rect_px.xy) * 0.5;
  let dst_center = (inst.dst_rect_px.zw + inst.dst_rect_px.xy) * 0.5;
  var dst_pos = vertices[vid] * dst_half + dst_center;
  let xform = mat3x3f(vec3f(u.xform0.x, u.xform0.y, u.xform0.z),
                      vec3f(u.xform1.x, u.xform1.y, u.xform1.z),
                      vec3f(u.xform2.x, u.xform2.y, u.xform2.z));
  dst_pos = (xform * vec3f(dst_pos, 1.0)).xy;

  var out: V2P;
  out.position = vec4f(2.0*dst_pos.x/u.viewport_size.x - 1.0,
                       2.0*(1.0 - dst_pos.y/u.viewport_size.y) - 1.0,
                       0.0, 1.0);
  out.uv01 = vec2f(select(0.0, 1.0, (vid & 2u) != 0u),
                   select(0.0, 1.0, (vid & 1u) != 0u));
  out.src_rect_px = inst.src_rect_px;
  out.tint = inst.color00;
  return out;
}

fn sample_src(uv01: vec2f, src_rect_px: vec4f) -> vec4f
{
  let src_px = mix(src_rect_px.xy, src_rect_px.zw, uv01);
  return textureSample(t_surface, s_surface, src_px / u.texture_size);
}

@fragment
fn fs_main(in: V2P) -> @location(0) vec4f
{
  let scanline_intensity = u.params0.x;
  let curvature          = u.params0.y;
  let vignette_strength  = u.params0.z;
  let mask_strength      = u.params0.w;
  let rgb_shift_px       = u.params1.x;
  let brightness         = u.params1.y;

  // barrel distortion in rect-local 0..1 space
  let centered = in.uv01 - vec2f(0.5, 0.5);
  let r2 = dot(centered, centered);
  var uv = in.uv01 + centered * (r2 * curvature * 2.0);
  if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
  {
    return vec4f(0.0);
  }

  // rgb channel shift (in source pixels, horizontal)
  let shift01 = vec2f(rgb_shift_px / max(in.src_rect_px.z - in.src_rect_px.x, 1.0), 0.0);
  var color = sample_src(uv, in.src_rect_px);
  if(rgb_shift_px > 0.001)
  {
    color.r = sample_src(uv + shift01, in.src_rect_px).r;
    color.b = sample_src(uv - shift01, in.src_rect_px).b;
  }

  // scanlines, on the source row grid so they track content resolution
  let src_rows = (in.src_rect_px.w - in.src_rect_px.y);
  let scan_phase = uv.y * src_rows * 3.14159265;
  let scan = 1.0 - scanline_intensity * (0.35 + 0.65 * sin(scan_phase) * sin(scan_phase));

  // aperture-grille mask on output pixels
  let mask_sel = u32(in.position.x) % 3u;
  var mask_mul = vec3f(1.0, 1.0, 1.0) - vec3f(mask_strength * 0.45);
  if(mask_sel == 0u)      { mask_mul.r = 1.0; }
  else if(mask_sel == 1u) { mask_mul.g = 1.0; }
  else                    { mask_mul.b = 1.0; }

  // vignette
  let vig = 1.0 - vignette_strength * r2 * 1.5;

  // premultiplied throughout: scale color & coverage together where the tube
  // darkens (scanline/vignette), color-only for the mask & brightness
  var rgb = color.rgb * mask_mul * brightness * scan * max(vig, 0.0);
  var a   = color.a * scan * max(vig, 0.0);
  return vec4f(rgb, a) * in.tint * u.opacity;
}
