// Shared by every Slang core shader, imported as a module.
module common;

public float rect_sdf(float2 sample_pos, float2 rect_half_size, float r)
{
  return length(max(abs(sample_pos) - rect_half_size + r, 0.0)) - r;
}

public float linear_from_srgb(float x)
{
  return x < 0.0404482362771082 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

public float4 linear_from_srgb(float4 v)
{
  return float4(linear_from_srgb(v.x), linear_from_srgb(v.y), linear_from_srgb(v.z), v.w);
}

// Clip space from a position in the pass's points, y down.
public float4 ndc_from_points(float2 p, float2 viewport_size)
{
  return float4(2.0 * p.x / viewport_size.x - 1.0, 1.0 - 2.0 * p.y / viewport_size.y, 0.0, 1.0);
}

// The quad's corners in triangle-strip order, as fractions of the rect:
// (x0,y1), (x0,y0), (x1,y1), (x1,y0).
public float2 corner_pct(uint vid)
{
  return float2((vid >> 1) != 0 ? 1.0 : 0.0, (vid & 1) != 0 ? 0.0 : 1.0);
}
