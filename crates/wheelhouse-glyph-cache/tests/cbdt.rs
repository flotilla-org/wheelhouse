//! CoreText doesn't draw CBDT, so native finds the glyph's PNG itself
//! (font_provider.c). Against the bundled Noto Color Emoji: the glyphs of its
//! strike have PNGs (bar the few its index subtables skip), with the strike's
//! size and a bearing inside the image.
#![cfg(glyph_cache)]
use wheelhouse_glyph_cache::cbdt_png;

fn table<'a>(font: &'a [u8], tag: &[u8; 4]) -> &'a [u8] {
    let be32 = |at: usize| u32::from_be_bytes(font[at..at + 4].try_into().unwrap()) as usize;
    let count = u16::from_be_bytes([font[4], font[5]]) as usize;
    let record = (0..count).map(|i| 12 + i * 16).find(|&r| &font[r..r + 4] == tag).expect("the table");
    &font[be32(record + 8)..be32(record + 8) + be32(record + 12)]
}

#[test]
fn the_strikes_glyphs_have_their_pngs() {
    let font = std::fs::read(concat!(env!("CARGO_MANIFEST_DIR"), "/../../data/NotoColorEmoji.ttf")).unwrap();
    let (cblc, cbdt) = (table(&font, b"CBLC"), table(&font, b"CBDT"));
    let first = u16::from_be_bytes([cblc[8 + 40], cblc[8 + 41]]) as u32;
    let last = u16::from_be_bytes([cblc[8 + 42], cblc[8 + 43]]) as u32;
    assert!(last > first + 100);
    let mut count = 0;
    for glyph in first..=last {
        let Some(found) = cbdt_png(cblc, cbdt, glyph, 48.0) else { continue };
        count += 1;
        assert_eq!(&found.png[..8], b"\x89PNG\r\n\x1a\n", "glyph {glyph}");
        let width = u32::from_be_bytes(found.png[16..20].try_into().unwrap()) as i32;
        assert_eq!(found.ppem, 109);
        assert!(found.left.abs() < width && found.top > 0 && found.top <= 128, "glyph {glyph}: {} {}", found.left, found.top);
    }
    assert!(count as u32 > (last - first) * 9 / 10, "{count} of {}", last - first + 1);
    assert!(cbdt_png(cblc, cbdt, last + 1, 48.0).is_none());
}
