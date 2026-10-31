//! The generated document both benchmarks lay out: wheelhouse-text's and the FFI's.
#![allow(dead_code)]
use std::{env, fs, time::Duration};

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
];

pub fn font(name: &str) -> Vec<u8> {
    fs::read(format!("{}/../../data/{name}", env!("CARGO_MANIFEST_DIR"))).unwrap()
}

pub const WORDS: &[&str] = &[
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
pub struct Lcg(pub u64);
impl Lcg {
    pub fn next(&mut self, n: usize) -> usize {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((self.0 >> 33) as usize) % n
    }
}

/// Styles: 0 prose, 1 code, 2 bold.
pub struct Para {
    pub text: String,
    /// (start, len, style)
    pub runs: Vec<(usize, usize, usize)>,
}

pub fn paragraph(rng: &mut Lcg) -> Para {
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
                runs.push((start, text.len() - start, 1));
            }
            1 => {
                text.push_str(WORDS[rng.next(WORDS.len())]);
                runs.push((start, text.len() - start, 2));
            }
            _ => text.push_str(WORDS[rng.next(WORDS.len())]),
        }
    }
    text.push('.');
    Para { text, runs }
}

/// Paragraphs of about `WHEELHOUSE_TEXT_BENCH_MB` megabytes (default 8), and their bytes.
pub fn document() -> (Vec<Para>, usize) {
    let mb: f64 = env::var("WHEELHOUSE_TEXT_BENCH_MB").ok().and_then(|s| s.parse().ok()).unwrap_or(8.0);
    let mut rng = Lcg(559);
    let mut doc = vec![];
    let mut bytes = 0;
    while (bytes as f64) < mb * 1e6 {
        let p = paragraph(&mut rng);
        bytes += p.text.len() + 2;
        doc.push(p);
    }
    (doc, bytes)
}

pub fn ms(d: Duration) -> f64 {
    d.as_secs_f64() * 1e3
}

pub fn percentile(times: &[Duration], p: f64) -> Duration {
    let mut times = times.to_vec();
    times.sort();
    times[((times.len() - 1) as f64 * p).round() as usize]
}

/// Mean, p50, p99 and max.
pub fn summary(times: &[Duration]) -> String {
    let sum: Duration = times.iter().sum();
    format!(
        "mean {:.3} ms, p50 {:.3} ms, p99 {:.3} ms, max {:.3} ms",
        ms(sum) / times.len() as f64,
        ms(percentile(times, 0.5)),
        ms(percentile(times, 0.99)),
        ms(*times.iter().max().unwrap())
    )
}
