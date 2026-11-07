// Copies the stage to the window's back buffer as opaque colour; the target's
// sRGB format does the encoding. A 4-vertex strip covering the target.
// (OpenGL doesn't use this: it presents by blitting the stage.)
import common;

Texture2D t_stage;
SamplerState s_stage;

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
  out.position = float4(2.0 * pct.x - 1.0, 1.0 - 2.0 * pct.y, 0.0, 1.0);
  out.texcoord = pct;
  return out;
}

[shader("fragment")]
float4 fs_main(V2P in) : SV_Target
{
  return float4(t_stage.Sample(s_stage, in.texcoord).rgb, 1.0);
}
