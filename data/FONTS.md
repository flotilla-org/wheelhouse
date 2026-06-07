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

`NotoColorEmoji.ttf` is available for future color glyph work, but the current font renderer still treats normal text glyphs as tintable atlas entries.
