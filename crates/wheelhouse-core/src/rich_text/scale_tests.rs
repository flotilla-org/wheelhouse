//! The timings print with `--nocapture`; run in release for real numbers.
use std::time::Instant;

use super::generate::{self, Seeded};
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
