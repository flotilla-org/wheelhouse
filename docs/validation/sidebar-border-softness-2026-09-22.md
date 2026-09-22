# Native border softness investigation

The sidebar's outlines looked softer than the browser composition study even
with adjacent borders separated and row outlines inset from their containers.
The shell used the same softness of one logical point for backgrounds and
one-point border strokes. Metal scales softness and stroke thickness by the draw
transform, then applies overlapping inner and outer smoothstep ramps. On the
current 2x display that softness produces a four-pixel transition span, wider
than the nominal two-pixel stroke.

A comparison build changed only the shared softness constant from 1 to 0.25.
Colours and sidebar layout were unchanged. Full-resolution 2200-by-1560 captures
of a straight segment of the Andamento selector's upper blue border showed six
pixel rows with blue exceeding red by more than 25 in the original capture,
and three in the trial. The visual comparison likewise showed sharper outlines.
The constant also affects backgrounds, so this global trial was replaced.

The final change keeps background softness unchanged and gives ordinary UI
border strokes a softness of `0.5 / backing_scale`. Border rectangle padding
uses the same softness to retain the intended edge position rather than growing
the outline when sharpening it. The border hover overlay uses the same geometry
and softness. Focus rendering and selection colours are unchanged.

The final capture also measured three blue pixel rows at the sampled edge.
The macOS native build passes. This validates the current 2x Metal path, not all
monitor scales or other render backends. Rounded corners and one-point thickness
remain; this is not a font or sidebar-colour workaround. The retained test window
runs the corrected build; the daily driver was not restarted.
