// Separable Gaussian blur of the stage inside a rounded rect, in two passes:
// horizontal (stage -> scratch) then vertical (scratch -> stage). The kernel
// folds each pair of taps into one bilinear sample, so the sampler must be
// linear. The horizontal pass covers the vertical pass's whole input
// footprint (the caller widens its rect); only the vertical pass masks to
// the rounded rect, since masked scratch pixels would be stale inputs.
import common;

struct BlurUniforms
{
  float4 rect;         // x0, y0, x1, y1 in the pass's points
  float4 corner_radii; // 00, 01, 10, 11
  float2 direction;    // one texel along the pass's axis, in uv
  float2 viewport_size;
  uint blur_count;     // kernel entries in use, centre included
  float texcoord_flip_y; // 1 where the stage is stored bottom-up (OpenGL)
  float4 kernel[32];   // x: weight, y: offset in texels
}

ConstantBuffer<BlurUniforms> u;
Texture2D t_stage;
SamplerState s_stage;

struct V2P
{
  float4 position : SV_Position;
  float2 texcoord : TEXCOORD0;
  float2 sdf_sample_pos : TEXCOORD1;
  float corner_radius : TEXCOORD2;
  nointerpolation float2 rect_half_size : TEXCOORD3;
}

[shader("vertex")]
V2P vs_main(uint vid : SV_VertexID)
{
  float2 pct = corner_pct(vid);
  float2 p = float2(lerp(u.rect.x, u.rect.z, pct.x), lerp(u.rect.y, u.rect.w, pct.y));
  float radii[4] = { u.corner_radii.y, u.corner_radii.x, u.corner_radii.w, u.corner_radii.z };
  float2 half_size = (u.rect.zw - u.rect.xy) * 0.5;

  float2 texcoord = p / u.viewport_size;
  if(u.texcoord_flip_y > 0.5)
  {
    texcoord.y = 1.0 - texcoord.y;
  }

  V2P out;
  out.position = ndc_from_points(p, u.viewport_size);
  out.texcoord = texcoord;
  out.sdf_sample_pos = (2.0 * pct - 1.0) * half_size;
  out.corner_radius = radii[vid];
  out.rect_half_size = half_size - 2.0;
  return out;
}

[shader("fragment")]
float4 fs_main(V2P in) : SV_Target
{
  float3 color = u.kernel[0].x * t_stage.Sample(s_stage, in.texcoord).rgb;
  for(uint i = 1; i < u.blur_count; i += 1)
  {
    float weight = u.kernel[i].x;
    float2 offset = u.kernel[i].y * u.direction;
    color += weight * t_stage.Sample(s_stage, in.texcoord - offset).rgb;
    color += weight * t_stage.Sample(s_stage, in.texcoord + offset).rgb;
  }
  float corner_t = 1.0 - smoothstep(0.0, 2.0, rect_sdf(in.sdf_sample_pos, in.rect_half_size, in.corner_radius));
  if(u.direction.y != 0.0 && corner_t < 0.9)
  {
    discard;
  }
  return float4(color, 1.0);
}
