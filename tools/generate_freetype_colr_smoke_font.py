#!/usr/bin/env python3
"""Generate tiny COLR fonts for FreeType color rendering smoke tests."""

from __future__ import annotations

import argparse
import os
import shutil
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="Path to write the generated TTF")
    parser.add_argument("--colr-version", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()

    try:
        from fontTools.colorLib.builder import buildCOLR, buildCPAL
        from fontTools.fontBuilder import FontBuilder
        from fontTools.pens.ttGlyphPen import TTGlyphPen
        from fontTools.ttLib.tables import otTables as ot
    except ModuleNotFoundError as exc:
        ttx_path = shutil.which("ttx")
        if ttx_path is not None:
            try:
                with open(ttx_path, "rb") as file:
                    first_line = file.readline().decode("utf-8", errors="ignore").strip()
                if first_line.startswith("#!") and "python" in first_line.lower():
                    ttx_python = first_line[2:]
                    if os.path.realpath(ttx_python) != os.path.realpath(sys.executable):
                        os.execv(ttx_python, [ttx_python, __file__, *sys.argv[1:]])
            except OSError:
                pass
        print(
            "generate_freetype_colr_smoke_font.py requires fontTools. "
            "Install it with Homebrew or run with Homebrew's fonttools Python.",
            file=sys.stderr,
        )
        print(f"missing module: {exc.name}", file=sys.stderr)
        return 2

    units_per_em = 1000
    glyph_order = [".notdef", "space", "color", "red", "blue"]
    builder = FontBuilder(units_per_em, isTTF=True)
    builder.setupGlyphOrder(glyph_order)
    codepoint = 0xE001 if args.colr_version == 1 else 0xE000
    builder.setupCharacterMap({codepoint: "color"})

    def empty_glyph():
        return TTGlyphPen(None).glyph()

    def rect_glyph(x0: int, y0: int, x1: int, y1: int):
        pen = TTGlyphPen(None)
        pen.moveTo((x0, y0))
        pen.lineTo((x1, y0))
        pen.lineTo((x1, y1))
        pen.lineTo((x0, y1))
        pen.closePath()
        return pen.glyph()

    glyphs = {
        ".notdef": rect_glyph(50, 0, 450, 700),
        "space": empty_glyph(),
        "color": empty_glyph(),
        "red": rect_glyph(40, 0, 300, 700),
        "blue": rect_glyph(220, 0, 480, 700),
    }
    metrics = {name: (500, 0) for name in glyph_order}

    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupMaxp()
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupNameTable(
        {
            "familyName": f"UIShellCOLR{args.colr_version}Smoke",
            "styleName": "Regular",
            "uniqueFontIdentifier": f"UIShellCOLR{args.colr_version}Smoke Regular",
            "fullName": f"UIShellCOLR{args.colr_version}Smoke Regular",
            "psName": f"UIShellCOLR{args.colr_version}Smoke-Regular",
        }
    )
    builder.setupPost()

    font = builder.font
    font["CPAL"] = buildCPAL([[(1, 0, 0, 1), (0, 0, 1, 1)]])
    if args.colr_version == 0:
        font["COLR"] = buildCOLR(
            {"color": [("red", 0), ("blue", 1)]},
            version=0,
            glyphMap=font.getReverseGlyphMap(),
        )
    else:
        font["COLR"] = buildCOLR(
            {
                "color": {
                    "Format": int(ot.PaintFormat.PaintGlyph),
                    "Glyph": "red",
                    "Paint": {
                        "Format": int(ot.PaintFormat.PaintSolid),
                        "PaletteIndex": 1,
                        "Alpha": 1.0,
                    },
                },
            },
            version=1,
            glyphMap=font.getReverseGlyphMap(),
        )
    font.save(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
