# fontique against each platform's font fallback

Status: research notes for [#560](https://github.com/flotilla-org/wheelhouse/issues/560)
part 1, 1 November 2026. The gate is stated at the end.

fontique is Wheelhouse's only font resolver ([#352](https://github.com/flotilla-org/wheelhouse/issues/352),
[#543](https://github.com/flotilla-org/wheelhouse/issues/543)). This note checks
that it agrees with each platform's own fallback, and records where it doesn't.

## Method

`crates/wheelhouse-text/tests/parity.rs` lays out a corpus with parley and
fontique: Latin with ligatures, accented Latin, Greek and Cyrillic, Japanese,
Simplified and Traditional Chinese, Korean, emoji (modifiers, ZWJ families, a
flag, a tag sequence), Arabic with Arabic-Indic digits, Hebrew with points,
Devanagari, Thai, symbols, and one line mixing them all. Each line carries its
language. For every cluster with ink it records the face fontique chose, and
asks the platform the same question, from the same base family (fontique's own
`sans-serif`) and language:

- Linux: `fc-match` with the family, `lang` and the cluster as `charset`, as a
  browser's `FcFontSort` query asks.
- Windows: `IDWriteFontFallback::MapCharacters` over the system collection.
- macOS: `CTFontCreateForStringWithLanguage` from the base font, which walks
  CoreText's cascade list for that language.

It compares PostScript names, and checks both picks cover the cluster: a
cluster drawn with `.notdef` (fontique) or a face whose cmap lacks one of its
code points (the platform) is tofu. Three passes:

- **system**: fontique's fallback alone.
- **wheelhouse**: the base family, then Wheelhouse's bundled fallbacks
  (`DEFAULT_FALLBACKS`: Noto Sans Symbols 2, Math, Symbols, Color Emoji,
  Emoji), as wheelhouse-text stacks them.
- **configured**: `wheelhouse`, plus `Collection::set_fallbacks` for each
  line's script and language where `system` differed, giving it the platform's
  family. Whatever still differs here can't be configured away.

The test fails only on tofu in `configured` where the platform has a glyph.
It runs where the native builds run: `native/build.sh` (Linux, macOS) and
`native/build.bat` (Windows) under CI, printing its tables to the job log.
A job of its own in `core.yml`, with a fuller Linux font set, needs the
workflow scope the crew's token doesn't have; it's a follow-up.

## Results

Counts are clusters with ink (223 to 227 per pass, as hosts shape the emoji
differently).

| Host | system | wheelhouse | configured |
|---|---|---|---|
| Linux, Ubuntu 24.04 with Noto (crew host) | 124 same, 98 differ, 1 tofu | 125 same, 98 differ | 182 same, 41 differ |
| Linux, CI runner (DejaVu only) | 143 same, 80 both tofu | 143 same, 79 both tofu | 143 same, 80 both tofu |
| Windows Server 2022 (CI) | 206 same, 16 differ, 3 tofu | 199 same, 24 differ, 3 tofu | 208 same, 18 differ |
| macOS 15 (CI) | 175 same, 16 same family, 29 differ, 5 tofu | 146 same, 16 same family, 63 differ | see 8 below |

The CI Linux runner has only DejaVu, so neither side covers CJK, Thai or emoji
there; the crew host's run, with `fonts-noto-core`, `fonts-noto-cjk`,
`fonts-noto-color-emoji` and `fonts-dejavu-core` from Ubuntu 24.04, is the one
that says something about Linux.

### What differs, and whether it configures away

1. **Simplified Chinese falls back to a Japanese face once Japanese text has
   been laid out** (Linux and Windows). Laid out alone, `zh-CN` text gets
   Noto Sans CJK SC (Linux); after a `ja` line in the same collection it gets
   Noto Sans CJK JP (Linux) or Yu Gothic UI (Windows). On Windows that face
   lacks simplified-only characters (简, 汉, 标), so they are tofu. fontique
   caches `zh-CN` as Han's *default* fallback, and the order of queries decides
   what fills it. Configurable: `set_fallbacks((Hani, zh-CN), …)` gives the
   platform's face (Microsoft YaHei UI, Noto Sans CJK SC) and the tofu goes, as
   the `configured` pass shows. Wheelhouse should seed the CJK keys up front;
   the fontique bug is worth reporting upstream.
2. **Korean falls back to the Japanese CJK face on Linux.** fontique tracks no
   locales for Hangul, so `ko` doesn't reach fontconfig's query. Configurable:
   `set_fallbacks(Hang, …)`.
3. **Thai and Hebrew pick unusual faces on Linux**: Noto Looped Thai, and Noto
   Rashi Hebrew (a historical style). fontique asks fontconfig with the
   script's whole charset, which the plainer faces don't fully cover;
   fontconfig itself, asked with the cluster, picks Noto Sans Thai and DejaVu
   Sans. Configurable per script with `set_fallbacks`.
4. **Symbols fall back to a CJK face** in `system` (Noto Sans CJK on Linux, Yu
   Gothic UI on Windows), where the platforms pick DejaVu Sans or Segoe UI
   Symbol. ⏻ (U+23FB) is tofu in fontique's fallback. Wheelhouse's bundled
   Noto Sans Symbols 2 and Math cover all of them, so `wheelhouse` has no tofu;
   the faces still differ from the platform's.
5. **Arabic goes to Noto Sans Math under Wheelhouse's bundled fallbacks.**
   Noto Sans Math carries Arabic letters for mathematical use, and the bundled
   faces come before fontique's script fallback, so Arabic text gets it
   instead of an Arabic face. That's Wheelhouse's own stack, not fontique:
   configurable by ordering the bundled faces after the script fallback, or
   by leaving Math out of the text stack. Worth fixing before Rich Text shows
   Arabic (follow-up).
6. **Emoji**: Wheelhouse's bundled Noto Color Emoji wins over Segoe UI Emoji
   and Apple Color Emoji, by design. ❤️ (U+2764 U+FE0F) gets Noto Sans Symbols 2,
   a text-presentation face, because the bundled stack doesn't weigh the
   emoji variation selector. Configurable by ordering. 🧑🏿‍💻 is a sequence
   Segoe UI Emoji on Windows Server 2022 doesn't have; Noto does.
7. **Unmarked Han** (the mixed line, `en`) goes to whichever CJK face is
   Han's default: the platforms and fontique differ, neither is wrong without
   a language.

8. **macOS has no PingFang in fontique.** fontique leaves PingFang out (its
   outlines are `hvgl`) and stands Heiti SC in for all Han, Traditional
   Chinese included, so `zh-TW` text gets simplified forms of shared
   characters where CoreText gives PingFang TC; `zh-CN` gets Heiti SC where
   CoreText gives PingFang SC. The region is configurable:
   `set_fallbacks((Hani, zh-TW), Heiti TC)` (the `configured` pass seeds Heiti
   where the platform names PingFang). The face itself isn't: PingFang can't be
   reached through fontique's collection. Both faces cover the text; it is a
   difference of design, not of coverage. Wheelhouse rasterizes with CoreText
   on macOS, which draws `hvgl`, so registering PingFang's files is a way to
   close it later.
9. **Japanese** gets Hiragino Sans W4 where CoreText picks W3: the same family,
   a weight step heavier. Small.
10. **Symbols are tofu in fontique's own fallback on macOS** (⇒ ✓ ⚠ ☃ ⏻ in
    Helvetica), where CoreText finds Hiragino, Lucida Grande and STIX. The
    bundled fonts cover them.

## Faces as rendered

The second check, that synthesis and variation axes come out as fontique
reports them once rasterized, needs the glyph-id hook of part 2, so it runs
there: `--glyph_raster_diagnostics` (`native/src/uishell/uishell_glyph_raster_diagnostics.c`)
lays out synthetic test fonts with wh_text and rasterizes what it reports
through FreeType, DirectWrite or CoreText:

- weight 700 of a font with one weight: fontique reports `embolden`, and the
  rasterized glyph covers more;
- italic of a font with no italic: fontique reports a 14° skew, and the glyph
  leans right;
- weight 700 of a variable font with an avar map: fontique reports the
  normalized, avar-mapped coordinate (0.4), and the rasterized stem is the
  width the font's own deltas give at that coordinate. DirectWrite and
  CoreText take user values, so native undoes avar to give them 700; the stem
  matching on those two shows the round trip is right.

Results are in the part 2 pull request.

## Gate

**Passed: continue to part 2.** On all three platforms, every cluster the
platform covers is covered once Wheelhouse's bundled fallbacks and per-script
`set_fallbacks` are in place (the test asserts it). What still differs is the
choice of face among faces that cover the text: emoji and symbols (Wheelhouse's
bundled faces, by design), Arabic under the bundled Math face (Wheelhouse's own
ordering, a follow-up), Hiragino's weight, and macOS's Heiti for PingFang. The
last is the one difference fontique can't be configured out of today; it is
a matter of design rather than coverage, the region (SC or TC) is
configurable, and there is a route to PingFang through registering its files.
That is judged small enough to continue, and is recorded on #560.

## Follow-ups

- #590: seed fontique's CJK and Hangul fallbacks from the platform, and report the
  order-dependent `zh-CN` default upstream to fontique.
- #591: put Noto Sans Math (and the text-presentation symbol faces) after the
  script fallback in Wheelhouse's stack, so Arabic and emoji presentation get
  their own faces.
- #589: a `core.yml` job running the parity test on all three runners with a full
  Linux font set (needs a push with workflow scope).
