# Bundled Fonts

UI Shell currently bundles a small font set for default UI and terminal rendering.

The terminal-oriented fonts copied from Ghostty's `src/font/res` are:

- `JetBrainsMonoNerdFont-Regular.ttf`
- `NotoEmoji-Regular.ttf`
- `NotoColorEmoji.ttf`

Additional terminal symbol fallbacks installed from Homebrew's Noto font casks are:

- `NotoSansSymbols.ttf`
- `NotoSansSymbols2-Regular.ttf`
- `NotoSansMath-Regular.ttf`

These fill non-color symbol ranges that terminal UI examples often use, including arrows, geometric shapes, and mathematical operators.

JetBrains Mono and Noto fonts are distributed under the SIL Open Font License 1.1. A copy is included in `OFL.txt`.

`NotoColorEmoji.ttf` is included in the terminal fallback list before the monochrome Noto Emoji face. On macOS, UIShell also prefers the system Apple Color Emoji face because CoreText does not accept the bundled CBDT/CBLC Noto color font through `CGFontCreateWithDataProvider`, font descriptors, or process-scope registration. Color glyphs require the font renderer's source-color raster path; monochrome glyphs remain tintable atlas entries.
