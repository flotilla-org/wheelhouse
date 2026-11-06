// UI rectangles: one instance per R_Rect2DInst, drawn as a 4-vertex strip.
//
// What a rect means (checked per backend by --shared_ui_diagnostics, #705):
// - Corner radius, border and softness scale with the transform, as the rect
//   does, so scaled content looks the same, only larger.
// - Group opacity multiplies coverage once (alpha), never colour too.
// - Zero softness gives a hard edge; smoothstep never sees an empty range.

struct RectUniforms
{
  viewport_size: vec2f,
  opacity: f32,
  sample_is_surface: f32,
  texture_sample_channel_map: mat4x4f,
  texture_size: vec2f,
  xform_scale: vec2f,
  xform: array<vec4f, 3>,   // rows of the 2D affine transform
  surface_flip_y: f32,      // 1 where surface textures are stored bottom-up (OpenGL)
}

@group(0) @binding(0) var<uniform> u: RectUniforms;
@group(0) @binding(1) var t_color: texture_2d<f32>;
@group(0) @binding(2) var s_color: sampler;

struct Instance
{
  @location(0) dst_rect: vec4f,
  @location(1) src_rect: vec4f,
  @location(2) color00: vec4f,
  @location(3) color01: vec4f,
  @location(4) color10: vec4f,
  @location(5) color11: vec4f,
  @location(6) corner_radii: vec4f,  // 00, 01, 10, 11
  @location(7) style: vec4f,         // border thickness, softness, omit texture, shear
}

struct V2P
{
  @builtin(position) position: vec4f,
  @location(0) texcoord: vec2f,
  @location(1) sdf_sample_pos: vec2f,
  @location(2) tint: vec4f,
  @location(3) corner_radius: f32,
  @location(4) @interpolate(flat) rect_half_size: vec2f,
  @location(5) @interpolate(flat) border_thickness: f32,
  @location(6) @interpolate(flat) softness: f32,
  @location(7) @interpolate(flat) omit_texture: f32,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32, inst: Instance) -> V2P
{
  let pct = corner_pct(vid);
  let shear = select(0.0, inst.style.w, pct.x > 0.5);
  let dst = vec2f(mix(inst.dst_rect.x, inst.dst_rect.z, pct.x), mix(inst.dst_rect.y, inst.dst_rect.w, pct.y) + shear);
  let src = vec2f(mix(inst.src_rect.x, inst.src_rect.z, pct.x), mix(inst.src_rect.y, inst.src_rect.w, pct.y));
  let radii = array<f32, 4>(inst.corner_radii.y, inst.corner_radii.x, inst.corner_radii.w, inst.corner_radii.z);
  let colors = array<vec4f, 4>(inst.color01, inst.color00, inst.color11, inst.color10);

  let p = vec3f(dst, 1.0);
  let xformed = vec2f(dot(u.xform[0].xyz, p), dot(u.xform[1].xyz, p));
  let style_scale = max(u.xform_scale.x, u.xform_scale.y);
  let half_size = abs(inst.dst_rect.zw - inst.dst_rect.xy) * 0.5 * u.xform_scale;

  var texcoord = src / u.texture_size;
  if(u.sample_is_surface > 0.5 && u.surface_flip_y > 0.5)
  {
    texcoord.y = 1.0 - texcoord.y;
  }

  var out: V2P;
  out.position = ndc_from_points(xformed, u.viewport_size);
  out.texcoord = texcoord;
  out.sdf_sample_pos = (2.0 * pct - 1.0) * half_size;
  out.tint = colors[vid];
  out.corner_radius = radii[vid] * style_scale;
  out.rect_half_size = half_size;
  out.border_thickness = inst.style.x * style_scale;
  out.softness = inst.style.y * style_scale;
  out.omit_texture = inst.style.z;
  return out;
}

@fragment
fn fs_main(in: V2P) -> @location(0) vec4f
{
  // Sampled unconditionally: WGSL allows implicit-derivative sampling only in
  // uniform control flow, and omit_texture varies per instance.
  let raw = u.texture_sample_channel_map * textureSample(t_color, s_color, in.texcoord);
  var albedo = vec4f(1.0);
  if(in.omit_texture < 0.5)
  {
    if(u.sample_is_surface > 0.5)
    {
      // Surfaces hold premultiplied linear colour; un-premultiply so the
      // source-alpha blend reproduces premultiplied source-over exactly.
      albedo = raw;
      if(albedo.a > 0.0001)
      {
        albedo = vec4f(albedo.rgb / albedo.a, albedo.a);
      }
    }
    else
    {
      albedo = linear_from_srgba(raw);
    }
  }

  let soft_span = max(2.0 * in.softness, 0.001);
  var border_t = 1.0;
  if(in.border_thickness > 0.0)
  {
    let inner = in.rect_half_size - vec2f(2.0 * in.softness) - in.border_thickness;
    border_t = smoothstep(0.0, soft_span, rect_sdf(in.sdf_sample_pos, inner, max(in.corner_radius - in.border_thickness, 0.0)));
  }
  if(border_t < 0.001)
  {
    discard;
  }
  var corner_t = 1.0;
  if(in.corner_radius > 0.0 || in.softness > 0.75)
  {
    let outer = in.rect_half_size - vec2f(2.0 * in.softness);
    corner_t = 1.0 - smoothstep(0.0, soft_span, rect_sdf(in.sdf_sample_pos, outer, in.corner_radius));
  }

  var result = albedo * in.tint;
  result.a *= u.opacity * corner_t * border_t;
  return result;
}
