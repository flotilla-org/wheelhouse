//! Decides whether a width change can keep shaping again (#544, decision 4).
//! `WHEELHOUSE_TEXT_BENCH_MB` sets the document's size (default 8). The FFI's
//! benchmark (wheelhouse-ffi's `text`) times the same with the copy into C memory.
mod common;
use std::time::Instant;

use common::{document, font, ms, summary, Lcg, Para, FONTS, WORDS};
use wheelhouse_text::{Family, Input, Paragraph, Style, StyleRun, TextContext};

fn layout(ctx: &TextContext, styles: &[Style<'_>], p: &Para, width: f32, out: &mut Paragraph) {
    let runs = p.runs.iter().map(|&(start, len, style)| StyleRun { start, len, style }).collect::<Vec<_>>();
    let input = Input { runs: &runs, ..Input::plain(&p.text, styles) };
    ctx.layout_into(&input, width, out).unwrap();
}

fn main() {
    let ctx = TextContext::new(false);
    for (id, name) in FONTS.iter().enumerate() {
        ctx.register_fonts(font(name), id as u64);
    }
    let styles =
        [Style::default(), Style { family: Family::Code, size: 14.0, ..Style::default() }, Style { weight: 600.0, ..Style::default() }];
    let (doc, bytes) = document();
    let mut out = Paragraph::default();
    // Warm up: the first layouts load faces and fill caches.
    for p in doc.iter().take(200) {
        layout(&ctx, &styles, p, 800.0, &mut out);
    }

    // 1. The document, paragraph by paragraph, at 800 px.
    let started = Instant::now();
    let (mut lines, mut glyphs) = (0, 0);
    for p in &doc {
        layout(&ctx, &styles, p, 800.0, &mut out);
        lines += out.lines.len();
        glyphs += out.glyphs.len();
    }
    let total = started.elapsed();
    println!("document: {:.1} MB, {} paragraphs, {lines} lines, {glyphs} glyphs at 800 px", bytes as f64 / 1e6, doc.len());
    println!(
        "  {:.0} ms total; {:.1} µs a paragraph; {:.1} MB/s; {:.0} ms a 35 MB transcript at this rate",
        ms(total),
        total.as_secs_f64() * 1e6 / doc.len() as f64,
        bytes as f64 / 1e6 / total.as_secs_f64(),
        ms(total) * 35.0 / (bytes as f64 / 1e6)
    );

    // 2. A streaming tail: one paragraph growing a word at a time.
    let mut tail = Para { text: String::new(), runs: vec![] };
    let mut times = vec![];
    let mut words = Lcg(44);
    while tail.text.len() < 8000 {
        if !tail.text.is_empty() {
            tail.text.push(' ');
        }
        tail.text.push_str(WORDS[words.next(WORDS.len())]);
        let started = Instant::now();
        layout(&ctx, &styles, &tail, 800.0, &mut out);
        times.push(started.elapsed());
    }
    println!("streaming tail: {} appends to {} bytes ({} lines) at 800 px", times.len(), tail.text.len(), out.lines.len());
    println!("  {}, last {:.3} ms", summary(&times), ms(*times.last().unwrap()));

    // 3. A resize sweep: at each width from 300 to 1600 px, the paragraphs
    // that fill a 1200 px tall viewport at that width.
    let mut times = vec![];
    let mut counts = vec![];
    for width in (300..=1600).step_by(10) {
        let started = Instant::now();
        let mut height = 0.0;
        let mut count = 0;
        for p in &doc {
            layout(&ctx, &styles, p, width as f32, &mut out);
            height += out.height;
            count += 1;
            if height >= 1200.0 {
                break;
            }
        }
        times.push(started.elapsed());
        counts.push(count);
    }
    println!(
        "resize sweep: {} widths 300..1600 px, {}..{} paragraphs filling 1200 px",
        times.len(),
        counts.iter().min().unwrap(),
        counts.iter().max().unwrap()
    );
    println!("  a width: {} (a 120 Hz frame is 8.3 ms)", summary(&times));
}
