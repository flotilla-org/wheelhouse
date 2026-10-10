# Bundled Fonts

Wheelhouse currently bundles a small font set for default UI and terminal rendering.

The UI font is Atkinson Hyperlegible Next (Regular, plus SemiBold for group headers), from https://github.com/googlefonts/atkinson-hyperlegible-next:

- `AtkinsonHyperlegibleNext-Regular.ttf`
- `AtkinsonHyperlegibleNext-SemiBold.ttf`

It lacks symbols the UI draws, such as arrows (↗ →) and key glyphs (⌘ ⇧). Every run takes a character its font lacks from the Noto symbol faces below, in the order Symbols 2, Math, Symbols. A UI font chosen in Settings gets the same fallbacks.

The terminal-oriented fonts copied from Ghostty's `src/font/res` are:

- `JetBrainsMonoNerdFont-Regular.ttf`
- `NotoEmoji-Regular.ttf`
- `NotoColorEmoji.ttf`

Additional terminal symbol fallbacks installed from Homebrew's Noto font casks are:

- `NotoSansSymbols.ttf`
- `NotoSansSymbols2-Regular.ttf`
- `NotoSansMath-Regular.ttf`

These fill non-color symbol ranges that terminal UI examples often use, including arrows, geometric shapes, and mathematical operators.

Atkinson Hyperlegible Next, JetBrains Mono and Noto fonts are distributed under the SIL Open Font License 1.1. A copy is included in `OFL.txt`.

`NotoColorEmoji.ttf` is included in the terminal fallback list before the monochrome Noto Emoji face. On macOS, UIShell also prefers the system Apple Color Emoji face because CoreText does not accept the bundled CBDT/CBLC Noto color font through `CGFontCreateWithDataProvider`, font descriptors, or process-scope registration. Color glyphs require the font renderer's source-color raster path; monochrome glyphs remain tintable atlas entries.
