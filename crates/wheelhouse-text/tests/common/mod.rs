#![allow(dead_code)]
use std::{fmt::Write, fs};
use wheelhouse_text::{Family, FontSource, Paragraph, TextContext, DEFAULT_FALLBACKS};

/// Indexed by bytes id.
pub const FONTS: &[&str] = &[
    "AtkinsonHyperlegibleNext-Regular.ttf",
    "AtkinsonHyperlegibleNext-SemiBold.ttf",
    "JetBrainsMonoNerdFont-Regular.ttf",
    "icons.ttf",
    "NotoSansSymbols2-Regular.ttf",
    "NotoSansMath-Regular.ttf",
    "NotoSansSymbols.ttf",
    "NotoColorEmoji.ttf",
    "NotoEmoji-Regular.ttf",
    // From tests/fonts: real Hebrew, Arabic and Latin ligatures.
    "NotoSansHebrew-Subset.ttf",
    "NotoSansArabic-Subset.ttf",
    "EBGaramond-Subset.ttf",
];

/// Has `fi`, `ff`, `fl`, `ffi` and `ffl` ligatures.
pub const LIGATURES: &str = "EB Garamond";

pub fn data(name: &str) -> Vec<u8> {
    let dir = if name.contains("-Subset") { "tests/fonts" } else { "../../data" };
    fs::read(format!("{}/{dir}/{name}", env!("CARGO_MANIFEST_DIR"))).unwrap()
}

pub fn context() -> TextContext {
    let ctx = TextContext::new(false);
    for (id, name) in FONTS.iter().enumerate() {
        assert!(ctx.register_fonts(data(name), id as u64) > 0, "{name}");
    }
    let mut fallbacks = vec!["Noto Sans Hebrew", "Noto Sans Arabic"];
    fallbacks.extend(DEFAULT_FALLBACKS);
    ctx.set_fallbacks(&fallbacks);
    ctx
}

/// Prose in a face with Latin ligatures.
pub fn ligature_context() -> TextContext {
    let ctx = context();
    ctx.set_family(Family::Main, LIGATURES);
    ctx
}

pub fn dump(ctx: &TextContext, text: &str, p: &Paragraph) -> String {
    let mut s = String::new();
    for (i, l) in p.lines.iter().enumerate() {
        writeln!(
            s,
            "line {i} x={:.1} y={:.1} h={:.1} base={:.1} w={:.1} bytes={}..{} {:?}",
            l.x,
            l.y,
            l.height,
            l.baseline,
            l.width,
            l.text_start,
            l.text_end,
            &text[l.text_start as usize..l.text_end as usize]
        )
        .unwrap();
        for r in &p.runs[l.run_start as usize..(l.run_start + l.run_count) as usize] {
            let face = ctx.font(r.font).unwrap();
            let font = match face.source {
                FontSource::Bytes(id) => FONTS[id as usize].to_owned(),
                other => format!("{other:?}"),
            };
            let glyphs = &p.glyphs[r.glyph_start as usize..(r.glyph_start + r.glyph_count) as usize];
            let ids = glyphs.iter().map(|g| g.id.to_string()).collect::<Vec<_>>().join(",");
            writeln!(
                s,
                "  run {font} size={:.1} x={:.1}{}{}{} glyphs=[{ids}]",
                r.size,
                r.x,
                if r.rtl != 0 { " rtl" } else { "" },
                if r.embolden != 0 { " embolden" } else { "" },
                if r.skew != 0.0 { format!(" skew={}", r.skew) } else { String::new() }
            )
            .unwrap();
        }
        for c in &p.clusters[l.cluster_start as usize..(l.cluster_start + l.cluster_count) as usize] {
            let t = &text[c.start as usize..(c.start + c.len) as usize];
            let mut flags = vec![];
            for (bit, name) in [(1, "rtl"), (2, "space"), (4, "word"), (8, "lig-cont"), (16, "break"), (32, "ellipsis"), (64, "tab")] {
                if c.flags & bit != 0 {
                    flags.push(name);
                }
            }
            let glyphs = &p.glyphs[c.glyph_start as usize..(c.glyph_start + c.glyph_count) as usize];
            let ids = glyphs.iter().map(|g| g.id.to_string()).collect::<Vec<_>>().join(",");
            let text = if c.flags & 32 != 0 { "…" } else { t };
            writeln!(s, "  {:>7.2} +{:<6.2} {text:?} [{ids}] {}", c.x, c.advance, flags.join(" ")).unwrap();
        }
    }
    for b in &p.boxes {
        writeln!(s, "box {} x={:.1} y={:.1} {}x{} line {}", b.id, b.x, b.y, b.width, b.height, b.line).unwrap();
    }
    s
}
