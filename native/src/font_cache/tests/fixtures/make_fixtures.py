"""Makes the glyph diagnostics' synthetic fonts (native/src/font_cache/tests/
glyph_diagnostics.c). Each has a box glyph at "I" and "A" and nothing else:

  wheelhouse-test-var.ttf    a wght axis (100-900, default 400, with an avar
                             map) that widens the boxes' stems
  wheelhouse-test-colr.ttf   "A" as two COLRv0 layers, red over blue
  wheelhouse-test-sbix.ttf   "A" as one sbix PNG strike at 64 ppem

    uv run --with fonttools python make_fixtures.py
"""
import struct
import zlib
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables.TupleVariation import TupleVariation

HERE = Path(__file__).parent
UPM = 1000


def box(stem):
    pen = TTGlyphPen(None)
    pen.moveTo((100, 0))
    pen.lineTo((100, 700))
    pen.lineTo((100 + stem, 700))
    pen.lineTo((100 + stem, 0))
    pen.closePath()
    return pen.glyph()


def base(name, glyphs):
    fb = FontBuilder(UPM, isTTF=True)
    order = [".notdef"] + list(glyphs)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({ord(g): g for g in glyphs if len(g) == 1})
    fb.setupGlyf({".notdef": box(0), **glyphs})
    fb.setupHorizontalMetrics({g: (600, 100) for g in order})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": name, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    fb.setupPost()
    return fb


def var():
    fb = base("Wheelhouse Test Var", {"I": box(100), "A": box(100)})
    fb.setupFvar([("wght", 100, 400, 900, "Weight")], [])
    avar = newTable("avar")
    avar.segments = {"wght": {-1.0: -1.0, 0.0: 0.0, 0.5: 0.25, 1.0: 1.0}}
    fb.font["avar"] = avar
    # At wght 900 the stems are 300 units wide; at 100, 20.
    deltas = {}
    for g in ("I", "A"):
        coords = [(0, 0), (0, 0), (200, 0), (200, 0), (0, 0), (0, 0), (0, 0), (0, 0)]
        thin = [(0, 0), (0, 0), (-80, 0), (-80, 0), (0, 0), (0, 0), (0, 0), (0, 0)]
        deltas[g] = [TupleVariation({"wght": (0, 1.0, 1.0)}, coords), TupleVariation({"wght": (-1.0, -1.0, 0)}, thin)]
    fb.setupGvar(deltas)
    fb.save(HERE / "wheelhouse-test-var.ttf")


def colr():
    glyphs = {"I": box(100), "A": box(300), "A.red": box(150), "A.blue": box(300)}
    fb = base("Wheelhouse Test COLR", glyphs)
    fb.setupCOLR({"A": [("A.blue", 1), ("A.red", 0)]}, version=0)
    fb.setupCPAL([[(1.0, 0.0, 0.0, 1.0), (0.0, 0.0, 1.0, 1.0)]])
    fb.save(HERE / "wheelhouse-test-colr.ttf")


def png(w, h):
    # Red and blue checks, opaque.
    pixel = lambda x, y: bytes([200, 40, 40, 255] if (x // 8 + y // 8) % 2 else [40, 40, 200, 255])
    rows = b"".join(b"\0" + b"".join(pixel(x, y) for x in range(w)) for y in range(h))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def sbix():
    from fontTools.ttLib.tables import sbixStrike, sbixGlyph
    fb = base("Wheelhouse Test sbix", {"I": box(100), "A": box(0)})
    table = newTable("sbix")
    table.version = 1
    table.flags = 1
    strike = sbixStrike.Strike(ppem=64, resolution=72)
    image = sbixGlyph.Glyph(glyphName="A", graphicType="png ", originOffsetX=0, originOffsetY=0, imageData=png(40, 44))
    strike.glyphs["A"] = image
    table.strikes[64] = strike
    fb.font["sbix"] = table
    fb.save(HERE / "wheelhouse-test-sbix.ttf")


if __name__ == "__main__":
    var()
    colr()
    sbix()
