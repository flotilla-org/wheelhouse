# Research: kb_text_shape against the font cache

Status: research notes, 10 October 2026. Nothing here is decided.

Ticket: #345 (map #343). Background: `docs/research/automation-accessibility-i18n.md`
§b–c on branch `research/automation-accessibility-i18n`.

**Source read:** `kb_text_shape.h` v2.28e, from
[JimmyLefevre/kb](https://github.com/JimmyLefevre/kb) at commit
`cc63806775f615908ec9562653af4726d8b895d5` (2 October 2026), 31,603 lines.
`kbts:N` below means line N of that header. Wheelhouse references are
file:line on `main` at `3260412`. The header was read, not built or run, so
nothing here is measured.

## Summary

- **Bidi:** not full UAX #9. A streaming, fixed-memory resolver produces LTR
  and RTL *runs*, but no embedding levels, no explicit embeddings or isolates,
  and no line-level reordering. The caller reorders runs.
- **Memory:** the caller can control every allocation. Every object has a
  `SizeOf`/`Place` form, a fixed-buffer form, or an allocator callback.
  Segmentation needs no allocation at all.
- **API:** an immediate-mode "context" API (font stack, automatic
  segmentation, font fallback) and a lower-level "direct" API. Grapheme, word
  and line segmentation work alone, without fonts or shaping.
- **Fonts:** it takes raw TTF/OTF/TTC bytes, parses them itself and copies
  the tables it needs into its own blob. It never sees an `FT_Face`,
  `IDWriteFontFace` or `CGFont`. It reads no outlines and has no hinting.
- **Fit:** the font cache would have to be keyed by glyph index rather than
  UTF-8 bytes, and each provider would need a rasterise-by-glyph-index hook.
  DWrite already rasterises glyph-index runs. FreeType is a one-call change.
  CoreText needs the most work, because `CTLine` currently does its own
  shaping.
- **Terminal:** Cleat already segments cells into graphemes, so the ASCII
  batch path can stay as it is. Only multi-codepoint or complex-script cells
  would need shaping, cached per grapheme.
- **Maturity:** zlib licence, one main author, 80 commits, no tagged releases,
  15 open issues. One open issue reports a crash in 2.28e.

## Bidi: run direction with a simplified resolver, not full UAX #9

What the code does:

- **Few bidi classes.** It uses 11 classes: NI, BN, L, R, NSM, AL, AN, EN,
  ES, ET, CS (`kbts:3380-3394`). There are none for the explicit formatting
  characters (LRE, RLE, LRO, RLO, PDF, LRI, RLI, FSI, PDI) or for B, S or WS.
- **Explicit controls are unsupported.** The header says so: "Explicit
  direction control characters are not supported", followed by the list from
  U+202A to U+2069 (`kbts:1538-1548`). So the X1–X10 rules and isolates are
  missing.
- **Weak types are partly handled.** Rules W1–W7 are applied on the fly over a
  window of two characters (`kbts:30300-30367`). The code marks this as
  incomplete in places: "@Incomplete: ET+ EN -> EN+ EN" and "Surely, there are
  other edge cases" (`kbts:29489`, `kbts:29495`).
- **Neutrals** follow N1/N2 against the surrounding strong types, falling back
  to the paragraph direction (`kbts:30369-30418`). The resolution of digits
  departs from the standard on purpose, because the paragraph direction may
  not be known yet (`kbts:30402-30404`).
- **Brackets** are paired in the style of BD16, with a fixed stack of 64
  entries (`kbts:4355`, `kbts:29585-29662`). Bracket canonicalisation is
  marked "@Incomplete" (`kbts:29592`).
- **Paragraph direction** comes from the caller, or from the first strong
  character if the caller passes `KBTS_DIRECTION_DONT_KNOW`
  (`kbts:411-424`, `kbts:29528-29534`). A hard line break starts a new
  paragraph (`kbts:27325-27327`).
- **Only two outputs.** A direction break is either LTR or RTL
  (`kbts:3065-3071`). EN and AN digits come out as *LTR* direction runs that
  do not affect the paragraph direction (`kbts:29506-29511`). No embedding
  level is exposed anywhere (`kbts_break`, `kbts:4312-4321`; `kbts_run`,
  `kbts:4597-4606`).
- **No reordering.** Rules L1 and L2 are not implemented.
  - `kbts_ShapeRun` hands runs back in logical input order
    (`kbts:27268-27486`).
  - Within one RTL run, the glyphs are flipped into left-to-right visual
    order: "RTL runs are flipped so that visual order is consistent"
    (`kbts:718-720`).
  - The caller must reorder the runs on each line.
- **L4 mirroring is done.** Mirrored characters in RTL runs are swapped for
  their mirror glyph when the font has one (`kbts:21448-21464`).

What this means (inference): plain Arabic or Hebrew sentences with embedded
Latin text and numbers segment correctly, and the caller then lays the runs
out. However, the caller can't tell apart:

- a digit run inside an RTL span (level 2), which must move with that span;
- a Latin run at level 0.

Both arrive as "LTR". A layout pass that reverses contiguous RTL groups would
need its own rule for digits that sit between RTL runs. Text using RLI/PDI or
RLO, as in some bidi file names and chat text, will lay out wrongly.

## Memory: caller-supplied buffers or an allocator callback throughout

- **Stated policy:** "Whenever it is possible for you to pass your own buffer
  into a function, we allow it. Whenever it is not possible, we allow
  specifying a custom allocator" (`kbts:227-228`).
- **The allocator** is one callback with ALLOCATE and FREE operations. It
  doesn't have to return aligned memory (`kbts:231-256`).
  - The compile-time defaults are `KBTS_MALLOC` and `KBTS_FREE`.
  - `KB_TEXT_SHAPE_NO_CRT` removes the C runtime and file I/O
    (`kbts:96-116`).
- **The context** can be:
  - placed in caller memory with an allocator
    (`kbts_SizeOfShapeContext` / `kbts_PlaceShapeContext2`,
    `kbts:260-285`);
  - confined to one fixed buffer (`kbts_PlaceShapeContextFixedMemory`,
    `kbts:287-299`);
  - allocated outright (`kbts:301-311`).
- **Memory is reused between runs**, so a `kbts_run` doesn't survive the next
  `kbts_ShapeRun` call (`kbts:444-447`).
- **Direct API:**
  - Shape config: `SizeOf` and `Place` (`kbts:925-973`).
  - Scratchpad: placed, fixed-memory or created. Its initial size is known,
    but "a scratchpad can always dynamically allocate memory during shaping"
    (`kbts:975-1018`). The header says why: "shaping can have a very
    unpredictable memory footprint" (`kbts:696-698`).
  - Glyph storage is an arena of blocks with a free list. It can be given a
    fixed buffer (`kbts:1020-1088`).
- **Segmentation allocates nothing.** `kbts_break_state` is a plain struct of
  roughly 0.5 KB: an 8-entry break buffer and 64 brackets (`kbts:4335-4390`).
  The header describes it as "all of the state needed to perform fixed-memory
  segmentation" (`kbts:1155-1156`).
- **Fit with Wheelhouse arenas.** The allocator callback can push from an
  `Arena` and make FREE a no-op for scratch use. Open upstream issues touch
  this:
  - #100: pass the size to FREE;
  - #73: `KBTS_MALLOC` must return non-null for a zero-size request;
  - #65: better out-of-memory handling.

## API: what the calls look like

**Context API (default)** (`kbts:120-143`, `kbts:408-585`):

```c
kbts_shape_context *ctx = kbts_PlaceShapeContext2(alloc_fn, arena, mem,
                            KBTS_SHAPE_CONTEXT_FLAG_FONT_PRIORITY_BOTTOM_TO_TOP);
kbts_ShapePushFont(ctx, &primary);          // then each fallback
kbts_ShapeBegin(ctx, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
kbts_ShapeUtf8(ctx, s.str, (int)s.size, KBTS_USER_ID_GENERATION_MODE_SOURCE_INDEX);
kbts_ShapeEnd(ctx);
kbts_run run;
while(kbts_ShapeRun(ctx, &run))             // logical order; run.Font, .Direction, .Script
{
  kbts_glyph *g;
  while(kbts_GlyphIteratorNext(&run.Glyphs, &g))
  { /* g->Id, g->AdvanceX/Y, g->OffsetX/Y, g->UserIdOrCodepointIndex */ }
}
```

- **Mapping glyphs back to the source text.** With `SOURCE_INDEX`, user IDs
  advance by UTF-8 byte length (`kbts:514-518`). In the context API, though,
  `UserIdOrCodepointIndex` is always a codepoint index. Getting back to the
  user ID needs `kbts_ShapeGetShapeCodepoint` (`kbts:568-585`, `kbts:4423-4427`).
- **Font fallback.** The context checks font coverage for each grapheme
  against the font stack. The font it chooses is recorded at each grapheme
  break (`kbts:319-330`, `kbts:4494`).
- **Features** are pushed and popped on a stack (`kbts:459-472`).
- **Manual runs** let the caller impose its own segmentation, giving direction
  and script itself (`kbts:653-676`).

**Direct API.** `kbts_ShapeDirect2(scratchpad, storage, run_direction,
variations, &out)` shapes one run that the caller has already segmented
(`kbts:678-727`). The caller does the following:

1. Builds a `kbts_shape_config` for a combination of font, script and
   language (`kbts:925-973`).
2. Pushes glyphs with `kbts_PushGlyph` (`kbts:1062-1077`).
3. Calls `kbts_ShapeDirect2`.

**Segmentation without shaping: yes.**

- `kbts_BreakBegin`, then `kbts_BreakAddCodepoint` for each codepoint,
  draining with `kbts_Break` after each one, then `kbts_BreakEnd`
  (`kbts:1158-1302`).
- `kbts_BreakEntireStringUtf8` does a whole buffer at once and returns the
  breaks in order (`kbts:1304-1349`).
- No font is involved. The flags are DIRECTION, SCRIPT, GRAPHEME, WORD,
  LINE_SOFT, LINE_HARD and MANUAL (`kbts:1261-1295`).
- The streaming API returns each type of break in order, but the types are
  not ordered relative to each other (`kbts:1297-1302`).
- **Graphemes** come from a table-driven state machine. It includes the
  Indic conjunct rule (GB9c), the emoji ZWJ rule (GB11) and regional
  indicators (`kbts:30433-30466`).
- **Line breaking** follows UAX #14 classes (`kbts:3398ff`). It has the three
  Japanese kinsoku styles, though normal and loose currently behave the same
  (`kbts:1171-1199`).
- **Limitation:** you can't ask for only some break types. Every call pays for
  grapheme, word, line, script and direction together. In upstream issue #54
  the author estimates that turning off grapheme, word and line breaking would
  make it "roughly 2x" faster, and that the context is "~30% slower" than the
  direct path. A user in the same thread found full breaking too slow to run
  every frame without caching.

## Fonts: raw tables, parsed by kbts into its own blob

- **Input.** `kbts_FontFromMemory(data, size, index, alloc, alloc_data)`, or
  the two-step `kbts_LoadFont` + `kbts_PlaceBlob` with sizes the caller
  provides (`kbts:762-808`). Collections are supported: `kbts_FontCount` with
  a font index (`kbts:145-156`, `kbts:733-744`).
- **Tables copied.** `kbts_LoadFont` walks the table directory and records
  these tables (`kbts:27677-27696`): head, cmap, GDEF, GSUB, GPOS, hhea,
  vhea, hmtx, vmtx, maxp, OS/2, name, fvar, avar, MVAR, HVAR and VVAR.
  `kbts_PlaceBlob` copies them into the output blob (`kbts:27979-27980`). So
  the original file bytes can be freed once the blob exists.
- **Prebaked blobs.** A file that is already a kbts blob (magic `kbts`) loads
  without that step (`kbts:789-790`, `kbts:27537-27541`). Bundled fonts could
  therefore be preprocessed at build time.
- **What it doesn't read:** glyf, CFF, colour tables or hinting programs. So it
  draws no outlines, and "Hinting is not supported" (`kbts:1537`).
  - Advances and offsets are integers in font design units (`kbts_glyph`,
    `kbts:4429-4433`). The caller scales them by size ÷ unitsPerEm.
  - The legacy `kern` table is not in the list above, so kerning comes from
    GPOS only (inference from the table list).
- **Glyph IDs are `u16`** (`kbts:4420`).
- **Variable fonts** are supported (`kbts:188-206`).
- **Security:** "This library provides NO SECURITY GUARANTEE whatsoever. DO
  NOT use it on untrusted font files" (`kbts:4-6`). Wheelhouse opens font
  files from paths given in settings (`shell_core.c:10601-10604`) and resolves
  system emoji and fallback fonts by path (`uishell_terminal_glyph.c:473`,
  `:498`). Those are fonts already installed on the system, but they would now
  go through a second parser.

## Fit with the font cache

### Today

- **Per-codepoint pieces.** `fnt_run_from_string_scaled`
  (`font_cache.c:590-966`) walks the UTF-8 string one codepoint at a time
  (`:641-678`). Each piece is a substring of UTF-8 bytes.
- **Raster cache keyed by bytes.** A piece is looked up in an ASCII direct map
  (`:704-707`) or a hash table keyed on its UTF-8 bytes (`:710-723`). Both
  hang off the style node (`font_cache.h:162-177`).
- **On a miss** (`:727-885`):
  1. The fallback font is chosen per codepoint with `fp_font_has_codepoint`
     (`:759-777`).
  2. The substring goes to `fp_raster` (`:787`), whose hook takes a `String8`
     (`font_provider.h:86`).
  3. The provider's own advance becomes the piece's advance (`:868`).
- **Pieces map one-to-one to source.** Each piece records `decode_size`, its
  byte length (`font_cache.h:50`, `font_cache.c:927`). Two places add these up
  in logical order:
  - hit testing (`fnt_char_pos_from_tag_size_string_p`, `:1120-1145`);
  - wrapping (`fnt_wrapped_string_lines_from_font_size_string_max`,
    `:976ff`), which breaks only at `char_is_space` (`:1004`).
- **The run cache lasts one frame.** It is allocated from `frame_arena`
  (`:600-605`, `:959-962`), which `fnt_frame` clears every frame
  (`:1213-1217`). Run layout is redone every frame. Only the rasters persist.

### A glyph-index shape (sketch)

1. **A font node owns a `kbts_font` next to its `FP_Handle`.**
   - Bundled fonts already arrive as bytes (`fp_font_open_from_static_data_string`,
     `font_provider.h:82`).
   - Fonts opened by path would be read once, turned into a blob, and the file
     bytes freed.
   - Alternatively, build a `kbts` blob ahead of time for each embedded font.
2. **The fallback list becomes the context's font stack**, with the primary
   font highest. kbts's per-grapheme coverage test (`kbts:1419-1445`) replaces
   the per-codepoint `fp_font_has_codepoint` loop (`font_cache.c:759-777`).
   This also fixes base-plus-combining-mark pairs being split across two fonts.
3. **On a run-cache miss, shape the string**, then emit one `FNT_Piece` per
   glyph:
   - the advance and offset are kbts design units × (size ÷ unitsPerEm) ×
     `raster_scale`;
   - the texture subrect comes from the raster cache.
4. **Re-key the raster cache** from UTF-8 bytes to `(font, glyph id)` within a
   style node.
   - `hash2info` becomes a hash of font handle and glyph ID.
   - The 256-entry ASCII map could become a direct map from glyph ID to info
     for the primary font.
   - `FNT_RasterCacheInfo` stops carrying the advance; it holds only the bitmap
     and its bearings.
5. **Replace `decode_size` with a cluster.** Each piece needs:
   - a source byte range, from the user ID;
   - a flag for "first glyph of cluster" or "attached mark" (`AttachGlyph`,
     `kbts:4435-4445`).

   A ligature then covers several codepoints in one piece, and a mark has zero
   advance. Hit testing would snap to grapheme breaks, not codepoints.
   Wrapping would use `KBTS_BREAK_FLAG_LINE_SOFT` instead of spaces.
6. **Visual order.** Within each wrapped line, reorder the runs (L2). kbts
   leaves this to the caller (see Bidi). Hit testing and selection then become
   a mapping between visual and logical positions, not a sum of prefixes.
7. **Caching.** Shaping on every frame for every UI string would undo the
   cheap per-frame rebuild.
   - Options: a persistent shaped-run cache keyed on (style, string), or
     keeping the current loop as a fast path for strings that are pure ASCII
     or pure simple script.
   - That fast path would then disagree with the shaped path on kerning, since
     the current path has none. Whether that matters is undecided.

### Providers

The hook would change from `fp_raster(..., String8 string)` to something like
`fp_raster_glyph(arena, font, size, flags, U32 glyph_id)`, which returns the
bitmap and its bearings. Positions come from kbts.
`fp_font_has_codepoint` (`font_provider.h:85`) would no longer be needed for
fallback.

- **FreeType** (`font_provider_freetype.c:279ff`):
  - Today `FT_Load_Char` maps each codepoint through the cmap and loads it
    (`:309`, `:341`). The change is `FT_Load_Glyph(face, glyph_id, flags)`.
  - The current advance is FreeType's hinted `advance.x >> 6` (`:306-330`).
    kbts advances are unhinted, so glyph spacing changes slightly at small
    sizes unless the hinted advances are kept on purpose.
  - Colour bitmaps (`FT_LOAD_COLOR`, BGRA) work the same by glyph index.
- **DWrite** (`font_provider_dwrite.c:590ff`):
  - It already builds a `DWRITE_GLYPH_RUN` from glyph indices. Today it fills
    them with `GetGlyphIndices` (`:613`, `:682`) and draws with
    `DrawGlyphRun` (`:737`, `:800`).
  - The change is to take kbts glyph IDs and skip the cmap lookup. The run
    could also carry `glyphAdvances`/`glyphOffsets` if several glyphs are
    rasterised together.
  - COLR layering goes through `TranslateColorGlyphRun`, which already takes
    glyph indices (`:703`).
- **CoreText** (`mac_font_provider.c:240ff`):
  - It builds an attributed string and a `CTLine` (`:268`) and calls
    `CTLineDraw` (`:305`). CoreText therefore already shapes each piece
    itself, and may also substitute fonts.
  - With kbts, this path would become `CTFontDrawGlyphs` (or
    `CGContextShowGlyphsAtPositions`) with `CGGlyph` IDs.
  - Apple Color Emoji (sbix), which this provider prefers for colour emoji
    (`data/FONTS.md`), also draws by glyph ID through `CTFontDrawGlyphs`.
- **The stub provider** (`font_provider/stub/font_provider_stub.c:44`) would
  need the new hook too.

kbts reads no colour tables, and doesn't need to, because the providers keep
doing all rasterisation.

## Terminal

Today:

- **Cells are already graphemes.** Cleat hands over cells with their
  codepoints (`cell->graphemes`, `grapheme_count`), and
  `uishell_terminal_string_from_cell` re-encodes them as UTF-8
  (`uishell_terminal_glyph.c:19-29`).
- **Batch path.** Neighbouring narrow cells with a single printable ASCII
  codepoint (`:6717-6721`), the same font and colour, and no styling or cursor
  (`:9099-9126`) are joined into one string. That string makes one
  `dr_fnt_run_from_string` call (`:9620-9655`).
- **Other cells** are drawn one at a time.
  - A cell with more than one codepoint is rasterised as one `SinglePiece`
    (`:7008-7011`). With FreeType, that means `FT_Load_Char` for each
    codepoint with no mark positioning.
  - Batched runs and single cells both start on the cell grid (`:9647`,
    `:9675`). Within a batch, the monospace font's advances carry the pen.

What kbts would cost (inference, not measured):

- **No segmentation is needed.** Cleat has already split the text into
  graphemes and assigned columns.
- **The ASCII batch path can stay as it is.** A glyph-index direct map for the
  primary font is just as cheap as the byte direct map.
- **Only complex cells need shaping:** those with `grapheme_count > 1`, or
  whose script needs a complex shaper (`kbts_ScriptIsComplex`,
  `kbts:1502-1506`).
  - Each would go through `kbts_ShapeDirect2` once per distinct grapheme
    string.
  - The result would be cached just as cell rasters are cached today.
  - In steady state the cost is a hash lookup per cell.
- **Shaping across cells** would be a separate decision: programming
  ligatures, or Arabic letters joining across cells. Most terminals don't do
  it, because it fights the grid.
- **Terminal bidi is out of scope.** Cleat cells are in visual order, and
  kbts's bidi would not be used there.

## Maturity

- **Licence:** zlib (`LICENSE`; `kbts:1743-1744`).
- **Activity:**
  - Repository created June 2025; last push 2 October 2026.
  - 80 commits, about 1.2k stars, 32 forks, no tagged releases.
  - Versions appear in the header changelog only; 2.28e is current
    (`kbts:1562-1600`).
  - One main author, with nine contributors listed (`kbts:1550-1553`).
- **Open issues** (15 on 10 October 2026). The notable ones:
  - **#92:** `kbts_ShapePopFont` crashes in `kbts__EndLifetime` since about
    2.28, reproducible in the author's own refpad. No reply yet.
  - **#91:** errors with GCC 15.2.
  - **#49:** undefined behaviour in blob packing (misaligned u32 writes) and
    signed shifts.
  - **#78:** shaping turns Greek iota into U+1FBE PROSGEGRAMMENI with Arial.
    The 2.28c note "Only perform single decomposition on unsupported glyphs"
    (`kbts:1566`) may address it, but the issue is still open.
  - **#90:** Thai/Lao sara am decomposition gives the inserted nikhahit user
    ID 0, which breaks cluster mapping.
  - **#54:** break-type masks for cheaper segmentation.
  - **#64:** vertical text. The direction enum has only LTR and RTL
    (`kbts:3065-3071`).
  - **#65, #73, #100:** out-of-memory handling and allocator details.
- **Gaps the header itself lists** (`kbts:1516-1548`):
  - No shaping for Zawgyi, the Syriac abbreviation mark, or (probably)
    Egyptian hieroglyphs.
  - No dictionary word breaking, so no CJK word breaks.
  - No Indic v1 fonts (`beng` fails; `bng2` works).
  - No Traditional Arabic Windows 3.1 fonts and no Thai/Lao PUA fonts.
  - "We try less hard than Harfbuzz to be compatible with every font."
  - No hinting.
  - No explicit bidi controls.
- **Other observations:**
  - `kbts_ScriptDirection` returns RTL only for Arabic and Hebrew
    (`kbts:25029-25033`). Segmentation uses bidi classes rather than this
    helper, so Syriac, Thaana and N'Ko runs still segment correctly. Callers
    that use the helper would get the wrong answer for those scripts.
  - **Build cost.** At 31.6k lines, much of it Unicode and shaper tables, the
    header would add to the unity build. It could go in its own translation
    unit. See the build timings in `docs/research/plugin-views-and-build.md`
    §d.

## Implications for language scope

These are options, not decisions. They are listed against the phases in
`automation-accessibility-i18n.md` §b.

**What kbts plus glyph-index rasterisation would unlock:**

- **Latin, Greek and Cyrillic:**
  - correct combining marks and decomposed accents;
  - GPOS kerning and ligatures in UI text.

  Greek and Cyrillic still need a face, since none is bundled (§b, Fonts).
- **Arabic, Persian, Urdu and Hebrew:**
  - joining forms, mark placement and RTL runs;
  - mirrored brackets;
  - simple mixed-direction sentences.

  They still need Arabic and Hebrew fonts, and line-level run reordering
  written by us. Text that relies on isolates or overrides (U+2066–2069,
  U+202A–202E) won't lay out correctly.
- **Indic scripts** (Devanagari, Bengali, Tamil, Telugu and others), **Thai,
  Lao, Khmer, Myanmar, Sinhala and Tibetan**, plus scripts handled by the
  Universal Shaping Engine:
  - shaping becomes correct, given suitable fonts.
  - **Line breaking is not solved for Thai, Lao, Khmer and Myanmar.** The
    line-break class enum has no SA class (`kbts:3398ff`), and dictionary
    breaking is explicitly unsupported (`kbts:1521`). So lines would break
    only at spaces or explicit break characters (inference).
- **CJK:**
  - UAX #14 line breaking between ideographs, with kinsoku handling;
  - a basis for grapheme-aware caret movement.

  No CJK face is bundled. Double-click word selection needs dictionaries,
  which kbts doesn't provide.

**What would still be missing, whichever library shapes the text:**

- **IME on all three platforms:** XIM preedit and `XFilterEvent` on X11,
  `NSTextInputClient` on macOS, and joining surrogate pairs from `WM_CHAR` on
  Windows (§b, IME). Without it, nobody can type CJK, and on Windows nobody
  can type characters above U+FFFF.
- **RTL layout mirroring of the UI** (sidebar, tab strips, alignment and icon
  direction): phase 5 in §b. kbts orders glyphs within a line; it doesn't lay
  out boxes.
- **Bidi-aware caret, selection and hit testing** in text fields, built on
  the cluster mapping above.
- **System font fallback.** kbts falls back only through the fonts it is
  given ("does not handle selection and loading of system fonts", README).
  Wide coverage needs either bundled Noto faces per script or a
  platform lookup (fontconfig, DWrite system collection, CoreText cascade
  list).
- **Full UAX #9:** explicit embeddings, isolates and embedding levels. These
  would have to come from elsewhere if text from terminals, file names or chat
  is in scope.
- **Content work:** the string catalogue, plurals and formatting
  (phases 1–2), and translations.
