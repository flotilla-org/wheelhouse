#if defined(WHEELHOUSE_JACKSTAY)
internal B32
uishell_jackstay_geometry_diagnostics(void)
{
  // The same logical page must keep its size across display conventions.
  // macOS uses points plus a backing scale; Windows scales layout coordinates.
  struct { char *name; F32 layout,backing; U32 width,height; F32 scale; } cases[]=
  {
    {"1x",1,1,800,600,1},
    {"Retina",1,2,800,600,2},
    {"Windows 150%",1.5f,1,800,600,1.5f},
    {"Windows 200%",2,1,800,600,2},
    {"fractional backing",1,1.5f,800,600,1.5f},
    {"layout and backing",1.5f,2,800,600,3},
  };
  B32 ok=1;
  for(U32 i=0;i<ArrayCount(cases);i++)
  {
    Vec2F32 canvas=v2f32(800*cases[i].layout,600*cases[i].layout);
    UIShell_JackstayViewport viewport=uishell_jackstay_viewport(canvas,cases[i].layout,cases[i].backing);
    // A producer honoring both hints must deliver one texel per backing pixel.
    B32 match=viewport.width==cases[i].width && viewport.height==cases[i].height &&
      viewport.scale==cases[i].scale &&
      viewport.width*viewport.scale==canvas.x*cases[i].backing &&
      viewport.height*viewport.scale==canvas.y*cases[i].backing;
    if(!match)fprintf(stderr,"FAIL Jackstay %s: requested %ux%u @ %.2fx for %.0fx%.0f canvas\n",
      cases[i].name,viewport.width,viewport.height,viewport.scale,canvas.x,canvas.y);
    ok=match && ok;
  }
  UIShell_JackstayViewport empty=uishell_jackstay_viewport(v2f32(0,0),1,2);
  ok=empty.width==0 && empty.height==0 && empty.scale==2 && ok;
  // Truncate preferred logical size, rather than requesting outside the pane.
  UIShell_JackstayViewport fractional=uishell_jackstay_viewport(v2f32(1200.75f,900.75f),1.5f,1);
  ok=fractional.width==800 && fractional.height==600 && fractional.scale==1.5f && ok;
  fprintf(stderr,"Jackstay geometry diagnostics: %s\n",ok?"passed":"FAILED");
  return ok;
}
#endif
