# Rich text: what Wheelhouse and binnacle have today

Status: research notes, 31 October 2026. Nothing here is decided. The decisions taken from it are recorded on [the Rich Text map](https://github.com/flotilla-org/wheelhouse/issues/537). Companion
to [rich-text-prior-art.md](rich-text-prior-art.md).

Wheelhouse paths are `native/src/...` on `main` at `3c4fafa`. The text-stack
files are unchanged since the move from `src/`. Binnacle references are its
`main` at `1024708` (ABI 21).

## Summary

The RAD-derived stack is a good engine for monospace text. It was not built for
proportional, wrapped, styled text, and it cannot be stretched into it. A rich-text
system should be separate above the glyph layer and share a reworked glyph layer
below it. Binnacle's model of a value-free core, host measurement, a Window and a
Scroll Anchor transfers to Block-level virtualization. Its measurement boundary
does not reach inside a paragraph.

## Glyphs and fonts

- **No shaping on Linux or Windows.**
  - `fnt_run_from_string_scaled` (`font_cache/font_cache.c:591`) decodes one
    codepoint per piece. Each piece gets its own advance, so there is no kerning,
    no ligatures and no clusters, and combining marks are separate pieces.
  - FreeType loads per character and ignores the hinting and AA flags:
    `load_flags` is fixed at `font_provider_freetype.c:306`.
  - DirectWrite maps the cmap directly, without shaping.
  - CoreText builds a `CTLine` per piece. That shapes and may substitute fonts,
    so macOS already behaves differently from the other two.
- **The raster cache is keyed by UTF-8 bytes.**
  - Multi-byte pieces compare only a 64-bit hash, never the string
    (`font_cache.c:717`), so a collision draws the wrong glyph.
  - Shaping would need a cache keyed by (face, glyph id), plus a
    rasterize-by-glyph-index hook in each provider.
  - See [kb-text-shape.md](kb-text-shape.md).
- **Runs are rebuilt every frame.** The run cache lives in the frame arena,
  which `fnt_frame` (`font_cache.c:1213`) clears.
- **Fallback** is a global chain of 8 tags, consulted per codepoint on a miss.
  UI text gets no system fallback and no emoji fallback.
  `fnt_set_fallbacks` (`:225`) wipes every atlas.
- **The atlas** is 1024² RGBA8 pages, with masks also stored at 4 bytes per
  pixel. There are up to 64 pages and no eviction:
  `fnt_atlas_region_release` has no callers.
- **Sizes and positioning.** Sizes are floored to whole points, glyphs are
  always rasterized at pen offset 0 (so no subpixel positioning), and the default
  sampler is Nearest.
- **Faces.** There are four slots (Main, MainSemibold, Code, Icons); there is no
  italic and no bold code face.

## Layout

- **Measurement is single-line only.** Wrapping is
  `fnt_wrapped_string_lines_from_font_size_string_max` (`font_cache.c:976`):
  - it breaks only at spaces, or mid-word on overflow;
  - it returns a list of strings, with no style runs;
  - its only user is `ui_label_multiline` (`ui/ui_basic_widgets.c:50`), which
    makes one box per line.
- **Hit-testing** is one line's prefix sum of advances
  (`fnt_char_pos_from_tag_size_string_p`, `font_cache.c:1120`).
- **The UI layout cannot size a box's height from its wrapped width.**
  - TextContent sizing uses `display_fruns.dim.x` on both axes
    (`ui/ui_core.c`, standalone sizes pass).
  - The X and Y passes are independent.
  - Wrapped content must be measured outside layout and given a pixel height:
    the ADR 0010 pattern.
- **Virtualization supports only uniform rows.** `ui_scroll_list_begin`
  (`ui_basic_widgets.c:1422`) takes one `row_height_px`, plus integer row-block
  multiples. There is no height index anywhere.
- **Draw** visits every box with no clip culling. Retained `DR_Bucket`s are how
  the terminal avoids per-frame rebuild cost.

## The code/text view

`shell_text` (`uishell/uishell_views.c:3456`) is monospace by construction:

- It wraps by counting bytes against the width of "H"
  (`uishell_text_visual_line_array_from_info`, `:3099`), which miscounts UTF-8.
- It rebuilds the visual lines for the whole file every frame.
- Finding a line's tokens is a linear scan from the first token
  (`text/text.c`, `txt_token_array_from_info_line_num__linear_scan`).
- Lexers exist only for C-family languages, Rust, Odin, Jai and Zig, with none
  for Markdown or JSON.
- `mutable_text` copies the whole buffer on every op.

This is fine for source files and not for long logs. Rich text should embed this
view for code blocks, not grow out of it.

## Hosting Views inside Views

- **Any Renderer can be built into any rect** (`wh_view_ui_direct`,
  `shell/shell_core.c:1699`). The watch table already embeds Renderers in cells,
  with a fixed, pre-declared row count (`info.row_count = 8`,
  `uishell_views.c:2951`) and state keyed by row hash and Renderer name.
- **Gaps:** there is no protocol for a child to report its preferred height, and
  no lifecycle contract for embedded Views scrolled out of sight.
  - Jackstay's preview admission and
    [presentation demand](../design/jackstay-presentation-demand.md) are the
    nearest thing.
  - `UI_BoxFlag_RenderToSurface` caches offscreen surfaces, nested at most 8
    deep.

## Binnacle

- **The core never measures.**
  - The host pushes sizes through a per-axis default, Size Overrides, and
    optional per-Window content sizes (`set_window_sized`).
  - Units are abstract `f32`, and distances are `f64`.
  - No shipped renderer uses variable row heights yet. Web assumes single-line
    rows.
- **Reusable for Blocks:**
  - Key-identified items with Change Notices;
  - the Window with overscan;
  - a Scroll Anchor of position plus offset, with tail-follow (`at_end`);
  - an O(overrides + Window) size estimator.
- **Doesn't reach inside a paragraph.** Wrapping, inline runs and hit-testing
  inside text need measurement inside the layout. Binnacle never had to solve
  that.
- **Cost of the model:**
  - every feature lands in the core, the C header and its Rust mirror, wasm and
    TS, and ratatui;
  - the ABI went from 1 to 21 in three weeks;
  - the hand-mirrored bridges already disagree on the cell-state bit order (each
    pair is self-consistent);
  - ratatui measures text in `char`s, not columns: flotilla-org/binnacle#349.
- **No embedding.** No cell can host another View. The nearest thing is the
  editor rect for an overlay editor.
