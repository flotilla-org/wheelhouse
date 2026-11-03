//! Scale: a 10k-line document, and lookups in big height indexes. The
//! timings print with `--nocapture`; run in release for real numbers.
use std::time::{Duration, Instant};

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

/// About the size of a long agent session's transcript (#563).
const LARGE_TRANSCRIPT: usize = 35 << 20;

fn transcript_scale(bytes: usize) -> (Duration, Duration, Vec<Duration>, RichText) {
    let jsonl = generate::conversation::large(5, bytes);
    let start = Instant::now();
    let mut rich = RichText::open_conversation(&jsonl);
    let opened = start.elapsed();
    let id = rich.subscribe();
    let start = Instant::now();
    rich.poll(id).unwrap();
    let polled = start.elapsed();
    let mut steps = vec![];
    loop {
        let start = Instant::now();
        let left = rich.backfill(BACKFILL_BUDGET);
        steps.push(start.elapsed());
        rich.poll(id).unwrap();
        if left == 0 {
            break;
        }
    }
    (opened, polled, steps, rich)
}

#[test]
fn opening_a_transcript_ingests_only_its_tail() {
    let jsonl = generate::conversation::large(5, 4 << 20);
    let rich = RichText::open_conversation(&jsonl);
    let doc = rich.doc();
    let items = doc.children(doc.root().unwrap()).count();
    assert!(items <= TAIL_ITEMS, "{items} items in the tail");
    assert!(rich.backfill_remaining() > jsonl.len() - TAIL_BYTES - 64 * 1024, "{} bytes left", rich.backfill_remaining());
}

/// Run in release: `cargo test --release -p wheelhouse-rich-text -- --ignored --nocapture transcript`.
#[test]
#[ignore = "a timing report, for release builds"]
fn a_large_transcripts_tail_takes_less_than_a_frame() {
    let (opened, polled, steps, rich) = transcript_scale(LARGE_TRANSCRIPT);
    let total: Duration = steps.iter().sum();
    let mut sorted = steps.clone();
    sorted.sort();
    eprintln!(
        "{} MB transcript: tail of {} items ingested in {opened:?}, its snapshot polled in {polled:?}; backfill in {} steps of median {:?}, \
         p99 {:?}, worst {:?}, {total:?} in all; {} nodes, {} entries",
        LARGE_TRANSCRIPT >> 20,
        TAIL_ITEMS,
        steps.len(),
        sorted[sorted.len() / 2],
        sorted[sorted.len() * 99 / 100],
        sorted[sorted.len() - 1],
        rich.doc().len(),
        rich.doc().entries().len(),
    );
    assert!(opened + polled < Duration::from_millis(16), "the tail took {:?}", opened + polled);
}

/// A live conversation thousands of items long, its lines appended one at a
/// time: an append late on costs about what one early on does, however
/// many items its slot holds, Tool Runs and all.
#[test]
fn appending_to_a_long_conversation_costs_the_same_late_as_early() {
    let mut c = Seeded(11);
    let events = generate::conversation::events(&mut c, 6_000);
    let lines: Vec<Vec<u8>> = events.iter().map(|e| generate::conversation::jsonl(std::slice::from_ref(e))).collect();
    let mut rich = RichText::open_conversation(&[]);
    let (fifth, mut first, mut last) = (lines.len() / 5, Duration::ZERO, Duration::ZERO);
    for (i, line) in lines.iter().enumerate() {
        let start = Instant::now();
        rich.append(line);
        let took = start.elapsed();
        if i < fifth {
            first += took;
        } else if i >= lines.len() - fifth {
            last += took;
        }
    }
    let items = rich.doc().children(rich.doc().root().unwrap()).count();
    eprintln!("{} lines, {items} top-level children: first fifth {first:?}, last fifth {last:?}", lines.len());
    assert!(last < first * 3, "late appends took {last:?}, early ones {first:?}");
}

/// A call whose arguments stream in as many small deltas, such as a Write
/// of a large file, or a multi-edit whose deltas end inside it with `}`:
/// each delta costs what it adds, not what came before.
#[test]
fn streaming_a_large_calls_arguments_costs_what_each_delta_adds() {
    let content = format!(r#"{{"file_path":"big.txt","content":"{}"}}"#, "abcdefgh".repeat(256 * 1024));
    let pieces: Vec<String> = content.as_bytes().chunks(1024).map(|piece| std::str::from_utf8(piece).unwrap().to_owned()).collect();
    stream_arguments(&pieces, r#"{"content":"abcdefgh"#);
    let edit = r#"{"old":"abcdefgh abcdefgh abcdefgh","new":"hgfedcba hgfedcba"}"#;
    let mut pieces = vec![r#"{"file_path":"big.txt","edits":["#.to_owned()];
    pieces.extend((0..4000).map(|i| format!("{}{edit}", if i == 0 { "" } else { "," })));
    pieces.push("]}".to_owned());
    stream_arguments(&pieces, r#"{"edits":[{"new":"#);
}

fn stream_arguments(pieces: &[String], input_prefix: &str) {
    let line = |event: String| format!("{event}\n").into_bytes();
    let start = r#"{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"Write","kind":"edit"}"#;
    let args: String = pieces.concat();
    let deltas: Vec<Vec<u8>> = pieces
        .iter()
        .enumerate()
        .map(|(i, piece)| {
            let delta = serde_json::to_string(piece).unwrap();
            line(format!(r#"{{"type":"ToolCallArgs","threadId":"t","seq":{},"toolCallId":"c","delta":{delta}}}"#, i + 2))
        })
        .collect();
    let mut rich = RichText::open_conversation(&[line(r#"{"type":"Hello","contract":1}"#.into()), line(start.into())].concat());
    let (half, mut first, mut last) = (deltas.len() / 2, Duration::ZERO, Duration::ZERO);
    for (i, delta) in deltas.iter().enumerate() {
        let started = Instant::now();
        rich.append(delta);
        if i < half {
            first += started.elapsed();
        } else {
            last += started.elapsed();
        }
    }
    eprintln!("{} deltas of {} bytes: first half {first:?}, second half {last:?}", deltas.len(), args.len());
    // A quadratic cost puts three times the first half's in the second.
    assert!(last < first * 2, "late deltas took {last:?}, early ones {first:?}");
    let call = rich.doc().get(Key::named(&["t", "tool", "c"])).unwrap();
    let input = rich.doc().children(call).next().unwrap();
    let want = input_prefix;
    let text = rich.doc().text(input);
    assert!(text.starts_with(want), "the input, parsed and folded: {}", &text[..text.len().min(80)]);
}
