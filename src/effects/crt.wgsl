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

fn effect(uv01: vec2f, frag_px: vec2f) -> vec4f
{
  let scanline_intensity = u.params0.x;
  let curvature          = u.params0.y;
  let vignette_strength  = u.params0.z;
  let mask_strength      = u.params0.w;
  let rgb_shift_px       = u.params1.x;
  let brightness         = u.params1.y;

  // barrel distortion
  let centered = uv01 - vec2f(0.5, 0.5);
  let r2 = dot(centered, centered);
  let uv = uv01 + centered * (r2 * curvature * 2.0);
  if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
  {
    return vec4f(0.0);
  }

  // rgb channel shift (horizontal, in source pixels)
  var color = src(uv);
  if(rgb_shift_px > 0.001)
  {
    color.r = src_at(uv, vec2f( rgb_shift_px, 0.0)).r;
    color.b = src_at(uv, vec2f(-rgb_shift_px, 0.0)).b;
  }

  // scanlines on the source row grid, so they track content resolution
  let scan_phase = uv.y * u.source_size_px.y * 3.14159265;
  let s = sin(scan_phase);
  let scan = 1.0 - scanline_intensity * (0.35 + 0.65 * s * s);

  // aperture-grille mask on output pixels
  let mask_sel = u32(frag_px.x) % 3u;
  var mask_mul = vec3f(1.0 - mask_strength * 0.45);
  if(mask_sel == 0u)      { mask_mul.r = 1.0; }
  else if(mask_sel == 1u) { mask_mul.g = 1.0; }
  else                    { mask_mul.b = 1.0; }

  // vignette
  let vig = max(1.0 - vignette_strength * r2 * 1.5, 0.0);

  // premultiplied throughout: scanline & vignette darken color + coverage
  // together (the tube face going dark), mask & brightness shape color only
  let rgb = color.rgb * mask_mul * brightness * scan * vig;
  let a   = color.a * scan * vig;
  return vec4f(rgb, a);
}
