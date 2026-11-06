// UI rectangles: one instance per R_Rect2DInst, drawn as a 4-vertex strip.
//
// What a rect means (checked per backend by --shared_ui_diagnostics, #705):
// - Corner radius, border and softness scale with the transform, as the rect
//   does, so scaled content looks the same, only larger.
// - Group opacity multiplies coverage once (alpha), never colour too.
// - Zero softness gives a hard edge; smoothstep never sees an empty range.
import common;

struct RectUniforms
{
  float2 viewport_size;
  float opacity;
  float sample_is_surface;
  float4 texture_sample_channel_map[4]; // columns: result = sum of column i * sample[i]
  float2 texture_size;
  float2 xform_scale;
  float4 xform[3];                      // rows of the 2D affine transform
  float surface_flip_y;                 // 1 where surface textures are stored bottom-up (OpenGL)
}

ConstantBuffer<RectUniforms> u;
Texture2D t_color;
SamplerState s_color;

struct Instance
{
  float4 dst_rect     : POS;
  float4 src_rect     : TEX;
  float4 color00      : COL0;
  float4 color01      : COL1;
  float4 color10      : COL2;
  float4 color11      : COL3;
  float4 corner_radii : CRAD; // 00, 01, 10, 11
  float4 style        : STY;  // border thickness, softness, omit texture, shear
}

struct V2P
{
  float4 position : SV_Position;
  float2 texcoord : TEXCOORD0;
  float2 sdf_sample_pos : TEXCOORD1;
  float4 tint : TEXCOORD2;
  float corner_radius : TEXCOORD3;
  nointerpolation float2 rect_half_size : TEXCOORD4;
  nointerpolation float border_thickness : TEXCOORD5;
  nointerpolation float softness : TEXCOORD6;
  nointerpolation float omit_texture : TEXCOORD7;
}

[shader("vertex")]
V2P vs_main(uint vid : SV_VertexID, Instance inst)
{
  float2 pct = corner_pct(vid);
  float shear = pct.x > 0.5 ? inst.style.w : 0.0;
  float2 dst = float2(lerp(inst.dst_rect.x, inst.dst_rect.z, pct.x), lerp(inst.dst_rect.y, inst.dst_rect.w, pct.y) + shear);
  float2 src = float2(lerp(inst.src_rect.x, inst.src_rect.z, pct.x), lerp(inst.src_rect.y, inst.src_rect.w, pct.y));
  float radii[4] = { inst.corner_radii.y, inst.corner_radii.x, inst.corner_radii.w, inst.corner_radii.z };
  float4 colors[4] = { inst.color01, inst.color00, inst.color11, inst.color10 };

  float3 p = float3(dst, 1.0);
  float2 xformed = float2(dot(u.xform[0].xyz, p), dot(u.xform[1].xyz, p));
  float style_scale = max(u.xform_scale.x, u.xform_scale.y);
  float2 half_size = abs(inst.dst_rect.zw - inst.dst_rect.xy) * 0.5 * u.xform_scale;

  float2 texcoord = src / u.texture_size;
  if(u.sample_is_surface > 0.5 && u.surface_flip_y > 0.5)
  {
    texcoord.y = 1.0 - texcoord.y;
  }

  V2P out;
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

[shader("fragment")]
float4 fs_main(V2P in) : SV_Target
{
  float4 albedo = 1.0;
  if(in.omit_texture < 0.5)
  {
    float4 s = t_color.Sample(s_color, in.texcoord);
    float4 raw = u.texture_sample_channel_map[0] * s.x + u.texture_sample_channel_map[1] * s.y +
                 u.texture_sample_channel_map[2] * s.z + u.texture_sample_channel_map[3] * s.w;
    if(u.sample_is_surface > 0.5)
    {
      // Surfaces hold premultiplied linear colour; un-premultiply so the
      // source-alpha blend reproduces premultiplied source-over exactly.
      albedo = raw;
      if(albedo.a > 0.0001)
      {
        albedo.rgb /= albedo.a;
      }
    }
    else
    {
      albedo = linear_from_srgb(raw);
    }
  }

  float soft_span = max(2.0 * in.softness, 0.001);
  float border_t = 1.0;
  if(in.border_thickness > 0.0)
  {
    float2 inner = in.rect_half_size - 2.0 * in.softness - in.border_thickness;
    border_t = smoothstep(0.0, soft_span, rect_sdf(in.sdf_sample_pos, inner, max(in.corner_radius - in.border_thickness, 0.0)));
  }
  if(border_t < 0.001)
  {
    discard;
  }
  float corner_t = 1.0;
  if(in.corner_radius > 0.0 || in.softness > 0.75)
  {
    float2 outer = in.rect_half_size - 2.0 * in.softness;
    corner_t = 1.0 - smoothstep(0.0, soft_span, rect_sdf(in.sdf_sample_pos, outer, in.corner_radius));
  }

  float4 result = albedo * in.tint;
  result.a *= u.opacity * corner_t * border_t;
  return result;
}
