//! Scale: a 10k-line document, and lookups in big height indexes. The
//! timings print with `--nocapture`; run in release for real numbers.
use std::time::Instant;

use super::generate::{self, Choose, Seeded};
use super::heights::{Entry as Row, HeightIndex, CHUNK};
use super::*;

/// The index of a top-level paragraph near the middle: one whose first
/// character is a letter.
fn middle_paragraph(blocks: &[String]) -> usize {
    let middle = blocks.len() / 2;
    (middle..blocks.len()).find(|&i| blocks[i].starts_with(|c: char| c.is_ascii_lowercase())).expect("a paragraph after the middle")
}

#[test]
fn reloading_a_large_document_with_one_paragraph_edited_yields_a_small_delta() {
    let mut blocks = generate::large(7, 10_000);
    let source = generate::render(&blocks);
    let lines = source.lines().count();
    assert!(lines >= 10_000, "{lines} lines");

    let start = Instant::now();
    let mut rich = RichText::open(&source);
    let opened = start.elapsed();
    let id = rich.subscribe();
    let start = Instant::now();
    let snapshot = rich.poll(id).unwrap().unwrap();
    let snapshotted = start.elapsed();

    let at = middle_paragraph(&blocks);
    blocks[at] = format!("{} {}", generate::word(&mut Seeded(1)), blocks[at]);
    let start = Instant::now();
    assert!(rich.reload(&generate::render(&blocks)));
    let reloaded = start.elapsed();
    let start = Instant::now();
    let delta = rich.poll(id).unwrap().unwrap();
    let polled = start.elapsed();

    eprintln!(
        "10k lines ({} bytes, {} nodes): open {opened:?}, snapshot of {} ops {snapshotted:?}, reload {reloaded:?}, delta of {} ops {polled:?}",
        source.len(),
        rich.doc().len(),
        snapshot.ops.len(),
        delta.ops.len(),
    );
    assert!(!delta.snapshot);
    assert_eq!(delta.ops.len(), 1, "{:?}", delta.ops);
    assert!(matches!(&delta.ops[0], Op::Upsert(c) if c.kind == Kind::Paragraph));

    // An unchanged reload delivers nothing.
    assert!(!rich.reload(&generate::render(&blocks)));
    assert_eq!(rich.poll(id).unwrap(), None);
}

/// Steps one lookup may take: a Fenwick search over the chunks and a scan
/// of one chunk.
fn bound(len: usize) -> f64 {
    let chunks = (len / CHUNK + 1) as f64;
    4.0 * (chunks.log2() + 1.0) + CHUNK as f64
}

#[test]
fn lookups_are_logarithmic() {
    let mut per_lookup = vec![];
    for len in [1_000usize, 10_000, 100_000, 1_000_000] {
        let mut index = HeightIndex::default();
        let rows: Vec<Row> = (0..len).map(|i| Row::estimated((i % 37 + 1) as f32, 0)).collect();
        index.insert(0, &rows);
        // A middle insert or two, to split chunks.
        index.insert(len / 2, &[Row::estimated(3.0, 0); 300]);
        let total = index.total();
        let mut c = Seeded(len as u64);
        let lookups = 20_000;
        let probes = index.probes();
        let start = Instant::now();
        let mut worst = 0;
        for _ in 0..lookups {
            let before = index.probes();
            let offset = c.below(total as usize) as f64;
            let (entry, _) = index.entry_at(offset).unwrap();
            let back = index.offset_of(entry);
            assert!(back <= offset);
            worst = worst.max(index.probes() - before);
        }
        let elapsed = start.elapsed();
        let mean = (index.probes() - probes) as f64 / lookups as f64;
        eprintln!(
            "{len:>9} entries: {:>7.1} ns per lookup pair, {mean:.0} steps on average, {worst} at worst",
            elapsed.as_nanos() as f64 / lookups as f64
        );
        assert!(worst as f64 <= 2.0 * bound(len), "{worst} steps for {len} entries");
        per_lookup.push(mean);
    }
    // A thousandfold more entries costs a few more Fenwick steps, not more scanning.
    assert!(per_lookup[3] < per_lookup[0] + 100.0, "{per_lookup:?}");
}
