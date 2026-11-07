// Composites a Geo3D pass's colour target into the stage or a surface: one
// rect of the target (dst, in clip space) samples the matching rect of the
// Geo3D colour texture (src, in uv). A 4-vertex strip; blending is the
// pipeline's.
import common;

struct CompositeUniforms
{
  float4 dst_ndc; // x0, y0, x1, y1 in clip space (y0 the top edge)
  float4 src_uv;  // x0, y0, x1, y1 in the Geo3D colour texture
}

ConstantBuffer<CompositeUniforms> u;
Texture2D t_color;
SamplerState s_color;

struct V2P
{
  float4 position : SV_Position;
  float2 texcoord : TEXCOORD0;
}

[shader("vertex")]
V2P vs_main(uint vid : SV_VertexID)
{
  float2 pct = corner_pct(vid);
  V2P out;
  out.position = float4(lerp(u.dst_ndc.xy, u.dst_ndc.zw, pct), 0.0, 1.0);
  out.texcoord = lerp(u.src_uv.xy, u.src_uv.zw, pct);
  return out;
}

[shader("fragment")]
float4 fs_main(V2P in) : SV_Target
{
  return t_color.Sample(s_color, in.texcoord);
}
