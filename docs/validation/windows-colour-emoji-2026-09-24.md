# Colour emoji in a Windows terminal pane, 24 September 2026

For [#59](https://github.com/flotilla-org/wheelhouse/issues/59), following the
colour emoji decision in [#57](https://github.com/flotilla-org/wheelhouse/issues/57).

## Change

The font provider layer has a new hook, `fp_system_color_emoji_fonts`. It returns
the platform's colour emoji families in preference order, each looked up by family
name and resolved to a font file:

- DirectWrite: "Segoe UI Emoji" from the system font collection, via the font face's
  local file loader.
- CoreText: "Apple Color Emoji", matched with the family name as a mandatory
  attribute.
- FreeType: fontconfig's "emoji" family with `color=True`, accepted only if the match
  is a colour font. fontconfig is loaded at run time, so hosts and builds without it
  still work.

Each backend resolves the list once and keeps it. The terminal font set puts these
fonts ahead of the embedded Noto fonts, replacing the hard-coded
`/System/Library/Fonts/Apple Color Emoji.ttc`. A family is skipped if its face is
not the first in its file, because `fp_font_open` opens face 0.

Segoe UI Emoji is a COLR font. It goes through the DirectWrite provider's existing
`TranslateColorGlyphRun` colour-layer path. No CBDT or PNG support was added.

The `terminal_glyph` diagnostic logs each system colour emoji family and whether
the host has it. On Windows and macOS it fails if the host has none, naming the
families it looked for. It also fails if no configured font rasters U+1F642 in
colour. Before this change, only macOS had that check.

## Host

Beaufort: Windows 11 Pro 10.0.26200, over RDP. Wheelhouse used its default D3D11
renderer and was built with `build.bat wheelhouse` (MSVC, debug) against Cleat
`c5eaa36` with Ghostty. This build does not include #63's bundled ConPTY, so panes
used the inbox ConPTY.

## Results

Observed on Beaufort:

- Before the change, `run_tests -Diagnostics terminal_glyph` failed. No configured
  font rastered U+1F642 in colour, and the fixture readback had `emoji=0 emoji_chroma=0`.
- After the change, the diagnostic logs `system colour emoji font "Segoe UI Emoji" is
  C:\WINDOWS\FONTS\SEGUIEMJ.TTF` and passes. `run_tests` passes all six diagnostics.
- In a build where the family name was temporarily changed to one that does not
  exist, the diagnostic logged that the family `is not installed on this host`. It
  then failed with `this host has no system colour emoji font (looked for ...)`.
- For the live check, I launched Wheelhouse with temporary `--user`/`--project` files.
  The window's only tab was an in-process terminal that ran a PowerShell script to
  print a UTF-8 emoji file. I captured that window with `PrintWindow`, then inspected
  the screenshot myself:
  - These rendered in colour: basic emoji (U+1F642, U+1F600, rocket, party popper,
    snake, pizza, globe, thumbs up, U+2705, U+231A, U+26A1) and VS16 sequences (red
    heart, smiling face, cloud).
  - Text presentation (`U+263A U+FE0E`, `U+2764 U+FE0E`) rendered as monochrome
    outlines, as expected.
  - The terminal-glyph fixture (`--terminal_glyph_fixture_ppm`) shows the same thing.

Glyphs that fall back:

- **Skin-tone modifiers, ZWJ sequences and keycaps:** these drew their parts side by
  side: the base emoji, then a skin-tone swatch, or each person in a family. Keycaps
  drew as `1` beside an empty box. The DirectWrite provider maps codepoints to
  glyphs one by one, without OpenType shaping. Segoe UI Emoji's ligatures for these
  sequences are therefore never applied. The FreeType provider does not shape either;
  only CoreText does. This is a shaping gap, not a COLR one, so #57 does not reopen
  on it.
- **Flags:** regional indicator pairs drew as letters (`GB`, `US`). Segoe UI Emoji has
  no flag glyphs, and Windows shows flags as letters too.
- No emoji I tried drew blank or in monochrome under emoji presentation. The run did
  not show glyphs that exist only as COLRv1.

In CI, the Windows job runs `run_tests`, which now includes `terminal_glyph`. Its log
shows whether the `windows-2022` image has Segoe UI Emoji.

Screenshots were temporary local artifacts. They were not committed.
