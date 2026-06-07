#!/usr/bin/env python3
"""Generate a tiny SVG-in-OpenType font for FreeType color format smoke tests."""

from __future__ import annotations

import argparse
import os
import shutil
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="Path to write the generated TTF")
    args = parser.parse_args()

    try:
        from fontTools.fontBuilder import FontBuilder
        from fontTools.pens.ttGlyphPen import TTGlyphPen
        from fontTools.ttLib import newTable
        from fontTools.ttLib.tables.S_V_G_ import SVGDocument
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
            "generate_freetype_svg_smoke_font.py requires fontTools. "
            "Install it with Homebrew or run with Homebrew's fonttools Python.",
            file=sys.stderr,
        )
        print(f"missing module: {exc.name}", file=sys.stderr)
        return 2

    units_per_em = 1000
    glyph_order = [".notdef", "space", "svgcolor"]
    builder = FontBuilder(units_per_em, isTTF=True)
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap({0xE002: "svgcolor"})

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

    builder.setupGlyf(
        {
            ".notdef": rect_glyph(50, 0, 450, 700),
            "space": empty_glyph(),
            "svgcolor": empty_glyph(),
        }
    )
    builder.setupHorizontalMetrics({name: (500, 0) for name in glyph_order})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupMaxp()
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupNameTable(
        {
            "familyName": "UIShellSVGSmoke",
            "styleName": "Regular",
            "uniqueFontIdentifier": "UIShellSVGSmoke Regular",
            "fullName": "UIShellSVGSmoke Regular",
            "psName": "UIShellSVGSmoke-Regular",
        }
    )
    builder.setupPost()

    font = builder.font
    svg_doc = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 500 1000">'
        '<rect x="40" y="200" width="420" height="700" fill="red"/>'
        "</svg>"
    )
    svg_table = newTable("SVG ")
    svg_glyph_id = font.getGlyphID("svgcolor")
    svg_table.docList = [SVGDocument(svg_doc, svg_glyph_id, svg_glyph_id, False)]
    font["SVG "] = svg_table
    font.save(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
