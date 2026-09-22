# Green edges on blurred popups

Opening the command palette produced saturated green strips along its top and
bottom edges. The build saved before the sidebar border-smoothing change also
reproduced it. Disabling `background_blur` removed the strips.

The Metal horizontal blur pass wrote only the final popup rectangle, clipped to
its rounded shape and UI scissor. The vertical pass sampled beyond those written
pixels into the scratch texture. Those texels could contain undefined contents or
pixels left by an earlier blur. This also affects other blurred floating UI.

The horizontal pass now covers the vertical kernel's complete input footprint,
including bilinear neighbours, and does not apply the final scissor or rounded
mask. The vertical pass retains both. The intermediate draw remains bounded to
the popup plus its sampling margin, rather than blurring the whole window. No
change to border smoothing or theme colours is needed.

## Native verification

Built on macOS with `bash build.sh meta wheelhouse`, using the provider paths
recorded in the sidebar validation notes. Opened the command palette with
Cmd+Shift+P in isolated native instances, captured the window with `screencapture`,
and counted green-dominant pixels in the palette's top and bottom edge bands.
The test region excluded the green text caret.

| Build/settings | Green edge pixels |
| --- | ---: |
| Current sidebar build before this fix | 6,847 |
| Saved build before the smoothing change | 13,700 |
| Saved build with background blur disabled | 0 |
| Fixed build, background blur enabled | 0 |

Temporary reproduction artifacts are in `/tmp/wh-palette-old/` and
`/tmp/wh-native-composition/`; the pixel check is
`swift /tmp/wh-palette-edge-check.swift IMAGE.png`. It fails on both affected
captures and passes on the fixed capture. It is a fixture-specific screenshot
check, not a general automated renderer test. Screenshots were also inspected
visually to confirm the palette was open and its border was intact.

The remaining test gap is a GPU readback fixture that poisons the scratch texture
and exercises the production blur submission path, including clipped popups and
window edges. A test of copied blur geometry alone would not cover the failure.
The D3D11 implementation has a similar pass structure but was not changed or
validated here.
