//! Decides whether a width change can keep shaping again (#544, decision 4).
//! `WHEELHOUSE_TEXT_BENCH_MB` sets the document's size (default 8).
use std::{
    env, fs,
    time::{Duration, Instant},
};
use wheelhouse_text::{Family, Input, Paragraph, Style, StyleRun, TextContext};

const FONTS: &[&str] = &[
    "AtkinsonHyperlegibleNext-Regular.ttf",
    "AtkinsonHyperlegibleNext-SemiBold.ttf",
    "JetBrainsMonoNerdFont-Regular.ttf",
    "icons.ttf",
    "NotoSansSymbols2-Regular.ttf",
    "NotoSansMath-Regular.ttf",
    "NotoSansSymbols.ttf",
    "NotoColorEmoji.ttf",
    "NotoEmoji-Regular.ttf",
];

const WORDS: &[&str] = &[
    "the",
    "layout",
    "of",
    "a",
    "paragraph",
    "shapes",
    "every",
    "run",
    "with",
    "parley",
    "and",
    "breaks",
    "lines",
    "at",
    "width",
    "when",
    "transcript",
    "agent",
    "tool",
    "result",
    "returned",
    "file",
    "changed",
    "test",
    "passed",
    "build",
    "succeeded",
    "review",
    "comment",
    "wheelhouse",
    "native",
    "glyph",
    "cluster",
    "offset",
    "caret",
    "selection",
    "→",
    "≥",
    "✓",
    "😀",
    "naïve",
    "café",
    "AV",
    "To",
];

/// Deterministic, so every run lays out the same document.
struct Lcg(u64);
impl Lcg {
    fn next(&mut self, n: usize) -> usize {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((self.0 >> 33) as usize) % n
    }
}

/// Styles: 0 prose, 1 code, 2 bold.
struct Para {
    text: String,
    runs: Vec<StyleRun>,
}

fn paragraph(rng: &mut Lcg) -> Para {
    let mut text = String::new();
    let mut runs = vec![];
    match rng.next(10) {
        0 => text.push_str("## "),
        1 | 2 => text.push_str("- "),
        _ => {}
    }
    let words = 8 + rng.next(120);
    for i in 0..words {
        if i > 0 {
            text.push(' ');
        }
        let start = text.len();
        match rng.next(14) {
            0 => {
                text.push_str("`fn_");
                text.push_str(WORDS[rng.next(WORDS.len())]);
                text.push_str("()`");
                runs.push(StyleRun { start, len: text.len() - start, style: 1 });
            }
            1 => {
                text.push_str(WORDS[rng.next(WORDS.len())]);
                runs.push(StyleRun { start, len: text.len() - start, style: 2 });
            }
            _ => text.push_str(WORDS[rng.next(WORDS.len())]),
        }
    }
    text.push('.');
    Para { text, runs }
}

fn layout(ctx: &TextContext, styles: &[Style<'_>], p: &Para, width: f32, out: &mut Paragraph) {
    let input = Input { runs: &p.runs, ..Input::plain(&p.text, styles) };
    ctx.layout_into(&input, width, out).unwrap();
}

fn ms(d: Duration) -> f64 {
    d.as_secs_f64() * 1e3
}

fn percentile(times: &mut [Duration], p: f64) -> Duration {
    times.sort();
    times[((times.len() - 1) as f64 * p).round() as usize]
}

fn main() {
    let ctx = TextContext::new(false);
    for (id, name) in FONTS.iter().enumerate() {
        let bytes = fs::read(format!("{}/../../data/{name}", env!("CARGO_MANIFEST_DIR"))).unwrap();
        ctx.register_fonts(bytes, id as u64);
    }
    let styles =
        [Style::default(), Style { family: Family::Code, size: 14.0, ..Style::default() }, Style { weight: 600.0, ..Style::default() }];
    let mb: f64 = env::var("WHEELHOUSE_TEXT_BENCH_MB").ok().and_then(|s| s.parse().ok()).unwrap_or(8.0);
    let mut rng = Lcg(559);
    let mut doc = vec![];
    let mut bytes = 0;
    while (bytes as f64) < mb * 1e6 {
        let p = paragraph(&mut rng);
        bytes += p.text.len() + 2;
        doc.push(p);
    }
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
    let appends = times.len();
    let last_lines = out.lines.len();
    let sum: Duration = times.iter().sum();
    println!("streaming tail: {appends} appends to {} bytes ({last_lines} lines) at 800 px", tail.text.len());
    println!(
        "  mean {:.3} ms, p50 {:.3} ms, p99 {:.3} ms, max {:.3} ms, last {:.3} ms",
        ms(sum) / appends as f64,
        ms(percentile(&mut times.clone(), 0.5)),
        ms(percentile(&mut times.clone(), 0.99)),
        ms(*times.iter().max().unwrap()),
        ms(*times.last().unwrap())
    );

    // 3. A resize sweep: the paragraphs that fill a 1200 px viewport at
    // 300 px wide, re-laid out at each width from 300 to 1600 px.
    let mut viewport = vec![];
    let mut height = 0.0;
    for p in &doc {
        layout(&ctx, &styles, p, 300.0, &mut out);
        height += out.height;
        viewport.push(p);
        if height > 1200.0 {
            break;
        }
    }
    let mut times = vec![];
    for width in (300..=1600).step_by(10) {
        let started = Instant::now();
        for p in &viewport {
            layout(&ctx, &styles, p, width as f32, &mut out);
        }
        times.push(started.elapsed());
    }
    let sum: Duration = times.iter().sum();
    println!(
        "resize sweep: {} paragraphs ({:.0} bytes), {} widths 300..1600 px",
        viewport.len(),
        viewport.iter().map(|p| p.text.len()).sum::<usize>(),
        times.len()
    );
    println!(
        "  a width: mean {:.3} ms, p50 {:.3} ms, p99 {:.3} ms, max {:.3} ms (a 120 Hz frame is 8.3 ms)",
        ms(sum) / times.len() as f64,
        ms(percentile(&mut times.clone(), 0.5)),
        ms(percentile(&mut times.clone(), 0.99)),
        ms(*times.iter().max().unwrap())
    );
}
