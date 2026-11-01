# Test fonts

Subsets used only by tests, so they exercise shaping that no font in `data/` does: real Hebrew and Arabic glyphs (joining, lam-alef), and Latin `fi`/`ffi` ligatures. All three are under the SIL Open Font License 1.1, in `OFL.txt`.

- `NotoSansHebrew-Subset.ttf`: Noto Sans Hebrew from https://github.com/google/fonts (`ofl/notosanshebrew`), instanced at wght 400, wdth 100, and cut to U+0020 and U+05B0–05EA, U+FB1D–FB4F.
- `NotoSansArabic-Subset.ttf`: Noto Sans Arabic (`ofl/notosansarabic`), instanced at wght 400, wdth 100, and cut to U+0020, U+0621–0652, U+0660–0669 and U+06CC.
- `EBGaramond-Subset.ttf`: EB Garamond (`ofl/ebgaramond`), instanced at wght 400, and cut to space, full stop and A–Z a–z, keeping `liga`, `kern` and `ccmp`.

They were made with fontTools' `varLib.instancer` and `subset`, with all name records kept and hinting dropped.
