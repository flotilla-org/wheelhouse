// CRT — scanlines, barrel curvature, aperture-grille mask, vignette, rgb shift.
// Indicative reference: gingerbeardman/webgl-crt-shader (MIT).
//
// knobs:
//  params0.x  scanline intensity   (0..1,   try 0.4)
//  params0.y  curvature            (0..0.5, try 0.08)
//  params0.z  vignette strength    (0..2,   try 0.35)
//  params0.w  aperture mask        (0..1,   try 0.5)
//  params1.x  rgb shift, src px    (0..2,   try 0.75)
//  params1.y  brightness           (0.5..1.8, try 1.15)

#include "effect_prelude.slang"

float4 effect(float2 uv01, float2 frag_px)
{
  float scanline_intensity = u.params0.x;
  float curvature          = u.params0.y;
  float vignette_strength  = u.params0.z;
  float mask_strength      = u.params0.w;
  float rgb_shift_px       = u.params1.x;
  float brightness         = u.params1.y;

  // barrel distortion
  float2 centered = uv01 - float2(0.5, 0.5);
  float r2 = dot(centered, centered);
  float2 uv = uv01 + centered * (r2 * curvature * 2.0);
  if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
  {
    return float4(0.0);
  }

  // rgb channel shift (horizontal, in source pixels)
  float4 color = src(uv);
  if(rgb_shift_px > 0.001)
  {
    color.r = src_at(uv, float2( rgb_shift_px, 0.0)).r;
    color.b = src_at(uv, float2(-rgb_shift_px, 0.0)).b;
  }

  // scanlines on the source row grid, so they track content resolution
  float scan_phase = uv.y * u.source_size_px.y * 3.14159265;
  float s = sin(scan_phase);
  float scan = 1.0 - scanline_intensity * (0.35 + 0.65 * s * s);

  // aperture-grille mask on output pixels
  uint mask_sel = uint(frag_px.x) % 3;
  float3 mask_mul = float3(1.0 - mask_strength * 0.45);
  if(mask_sel == 0)      { mask_mul.r = 1.0; }
  else if(mask_sel == 1) { mask_mul.g = 1.0; }
  else                   { mask_mul.b = 1.0; }

  // vignette
  float vig = max(1.0 - vignette_strength * r2 * 1.5, 0.0);

  // premultiplied throughout: scanline & vignette darken color + coverage
  // together (the tube face going dark), mask & brightness shape color only
  float3 rgb = color.rgb * mask_mul * brightness * scan * vig;
  float a    = color.a * scan * vig;
  return float4(rgb, a);
}
