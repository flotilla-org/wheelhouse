//! Behaviour on the hand-written fixtures.
use super::*;

pub(crate) const BLOCKS: &str = include_str!("fixtures/blocks.md");
pub(crate) const GFM: &str = include_str!("fixtures/gfm.md");
pub(crate) const EXTENSIONS: &str = include_str!("fixtures/extensions.md");

/// An outline of the tree, one node a line.
pub(crate) fn outline(doc: &Tree) -> String {
    let mut out = String::new();
    fn walk(doc: &Tree, id: NodeId, depth: usize, out: &mut String) {
        let c = doc.content(id);
        out.push_str(&"  ".repeat(depth));
        out.push_str(&format!("{:?}", c.kind));
        if c.level > 0 {
            out.push_str(&format!(" {}", c.level));
        }
        if c.hidden {
            out.push_str(" hidden");
        }
        match &c.detail {
            Detail::None => {}
            Detail::Embed { media_type, table: Some(table) } => {
                out.push_str(&format!(" {media_type} {:?} {} rows", table.alignments, table.rows.len()))
            }
            detail => out.push_str(&format!(" {detail:?}")),
        }
        if !c.text.is_empty() {
            out.push_str(&format!(" {:?}", c.text));
        }
        for span in &c.spans {
            let text = &c.text[span.start as usize..span.end() as usize];
            let attr = match span.attr {
                Attr::Link { target, .. } => format!("Link({})", c.strings[target as usize]),
                Attr::FootnoteReference { label } => format!("Footnote({})", c.strings[label as usize]),
                other => format!("{other:?}"),
            };
            out.push_str(&format!(" [{attr} {text:?}]"));
        }
        out.push('\n');
        for child in doc.children(id) {
            walk(doc, child, depth + 1, out);
        }
    }
    if let Some(root) = doc.root() {
        walk(doc, root, 0, &mut out);
    }
    out
}

#[test]
fn fixtures_ingest_every_block_kind() {
    insta::assert_snapshot!("blocks", outline(RichText::open(BLOCKS).doc()));
    insta::assert_snapshot!("gfm", outline(RichText::open(GFM).doc()));
    insta::assert_snapshot!("extensions", outline(RichText::open(EXTENSIONS).doc()));
}

fn find(doc: &Tree, text: &str) -> Key {
    let id = doc.preorder().into_iter().find(|&id| doc.text(id).contains(text)).unwrap_or_else(|| panic!("no Block has {text:?}"));
    doc.key(id)
}

#[test]
fn keys_are_parent_and_ordinal_and_survive_a_reload() {
    let rich = RichText::open(BLOCKS);
    let doc = rich.doc();
    let root = doc.root().unwrap();
    for (ordinal, child) in doc.children(root).enumerate() {
        assert_eq!(doc.key(child), Key::ROOT.child(ordinal as u32));
    }
    let again = RichText::open(BLOCKS);
    let keys = |doc: &Tree| doc.preorder().into_iter().map(|id| doc.key(id)).collect::<Vec<_>>();
    assert_eq!(keys(doc), keys(again.doc()));
}

#[test]
fn front_matter_is_kept_but_hidden() {
    let rich = RichText::open(EXTENSIONS);
    let doc = rich.doc();
    let front = doc.children(doc.root().unwrap()).next().unwrap();
    assert!(doc.hidden(front));
    assert_eq!(doc.text(front), "title: Extensions\ntags: [synthetic]");
    assert_eq!(doc.entry_of(doc.key(front), Part::Block), None, "a hidden Block has no index entry");
}

#[test]
fn a_reload_replaces_only_changed_blocks() {
    let mut rich = RichText::open(GFM);
    let id = rich.subscribe();
    rich.poll(id).unwrap();
    let edited = GFM.replace("Mind the gap.", "Mind the step.");
    assert!(rich.reload(&edited));
    let delta = rich.poll(id).unwrap().unwrap();
    assert_eq!(delta.generation, 2);
    assert_eq!(delta.ops.len(), 1);
    let Op::Upsert(content) = &delta.ops[0] else { panic!("{:?}", delta.ops) };
    assert_eq!(content.text, "Mind the step.");
    assert!(!rich.reload(&edited), "the same source changes nothing");
    assert_eq!(rich.poll(id).unwrap(), None);
}

#[test]
fn a_subscriber_that_falls_off_the_ring_gets_a_snapshot() {
    let mut rich = RichText::with_ring_capacity(BLOCKS, 4);
    let id = rich.subscribe();
    assert!(rich.poll(id).unwrap().unwrap().snapshot, "the first poll is the snapshot");
    rich.reload(&BLOCKS.replace("a tight item", "a tighter item"));
    let delta = rich.poll(id).unwrap().unwrap();
    assert!(!delta.snapshot);
    for n in 0..3 {
        rich.reload(&BLOCKS.replace("# The harbour log", &format!("# Log {n}\n\nmore {n}\n\nand more {n}")));
    }
    let delta = rich.poll(id).unwrap().unwrap();
    assert!(delta.snapshot, "the ring no longer holds this subscriber's changes");
}

#[test]
fn a_subscriber_at_a_partly_evicted_generation_is_served_from_the_ring() {
    let mut rich = RichText::with_ring_capacity("a\n\nb\n\nc\n\nd\n", 4);
    let id = rich.subscribe();
    rich.reload("a2\n\nb2\n\nc\n\nd\n");
    rich.poll(id).unwrap();
    rich.reload("a2\n\nb3\n\nc3\n\nd3\n");
    assert_eq!((rich.ring.floor(), rich.ring.oldest()), (2, Some(2)), "half of generation 2 was evicted");
    let delta = rich.poll(id).unwrap().unwrap();
    assert!(!delta.snapshot);
    let upserted: Vec<Key> = delta.ops.iter().filter_map(|op| if let Op::Upsert(c) = op { Some(c.key) } else { None }).collect();
    assert_eq!(upserted, [1, 2, 3].map(|i| Key::ROOT.child(i)), "{:?}", delta.ops);
    assert_eq!(delta.ops.len(), 3, "{:?}", delta.ops);
}

#[test]
fn polls_need_a_live_subscription() {
    let mut rich = RichText::open(GFM);
    let id = rich.subscribe();
    assert!(rich.unsubscribe(id));
    assert_eq!(rich.poll(id), Err(UnknownSubscription(id)));
    assert_eq!(rich.changed_since(id), Err(UnknownSubscription(id)));
    assert!(!rich.unsubscribe(id));
}

#[test]
fn find_is_literal_and_case_insensitive_by_default_and_runs_in_slices() {
    let source: String = (0..200).map(|i| format!("Paragraph {i} mentions the Harbour and the harbour.\n\n")).collect();
    let mut rich = RichText::open(&source);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.find("HARBOUR.", FindOptions::default()).unwrap();
    let status = view.find_step(doc, 500).unwrap();
    assert!(!status.done, "a small budget leaves the scan unfinished: N of M+");
    assert!(status.count > 0 && status.count < 200);
    while !view.find_step(doc, 500).unwrap().done {}
    let find = view.find_state().unwrap();
    assert_eq!(find.matches().len(), 200, "literal: the dot only matches a dot");
    let first = find.matches()[0];
    assert_eq!(&doc.text(doc.get(first.key).unwrap())[first.start as usize..first.end as usize], "harbour.");
    view.find("harbou?r\\b", FindOptions { regex: true, case_sensitive: true }).unwrap();
    assert_eq!(view.find_step(doc, usize::MAX).unwrap().count, 200);
    assert!(view.find("(", FindOptions { regex: true, ..FindOptions::default() }).is_err());
}

#[test]
fn find_keeps_at_most_match_limit_matches() {
    let source = "x".repeat(MATCH_LIMIT + 10);
    let mut rich = RichText::open(&source);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.find(".", FindOptions { regex: true, ..FindOptions::default() }).unwrap();
    let status = view.find_step(doc, usize::MAX).unwrap();
    assert_eq!((status.count, status.done, status.capped), (MATCH_LIMIT, true, true));
    view.find("x", FindOptions::default()).unwrap();
    assert!(!view.find_state().unwrap().status().capped, "a new find starts uncapped");
}

#[test]
fn find_moves_to_a_match_and_reveals_it() {
    let mut rich = RichText::open(BLOCKS);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_viewport(30.0);
    let item = doc.get(find(doc, "second")).unwrap();
    let nested = doc.parent(doc.parent(item).unwrap()).unwrap();
    view.set_collapsed(doc, doc.key(nested), true).unwrap();
    view.find("SECOND", FindOptions::default()).unwrap();
    view.find_step(doc, FIND_BUDGET);
    assert_eq!(view.find_state().unwrap().status().count, 2, "collapsed Blocks are searched too");
    let m = view.find_next(doc, true).unwrap();
    assert_eq!(m.key, doc.key(item));
    assert!(!view.is_collapsed(doc.key(nested)), "revealing a match expands what hides it");
    let anchor = view.anchor().unwrap();
    assert_eq!((anchor.key, anchor.viewport_offset), (m.key, 10.0), "a third of the way down");
    assert_eq!(view.find_state().unwrap().status().current, Some(0));
    let m = view.find_next(doc, true).unwrap();
    assert_eq!(doc.text(doc.get(m.key).unwrap()), "with a second paragraph");
    assert_eq!(view.find_next(doc, true).unwrap().key, doc.key(item), "it wraps");
}

fn block_rows(doc: &Tree, view: &View) -> Vec<(Key, Part, f64)> {
    view.window(doc).entries.iter().map(|e| (e.key, e.part, e.top)).collect()
}

#[test]
fn collapsing_a_container_hides_its_range() {
    let mut rich = RichText::open(BLOCKS);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_viewport(1000.0);
    let total = view.heights().total();
    let list = doc.parent(doc.parent(doc.get(find(doc, "a tight item")).unwrap()).unwrap()).unwrap();
    let key = doc.key(list);
    for (entry, height) in [(Part::Header, 2.0), (Part::Footer, 3.0)] {
        view.set_height(doc, key, entry, height, 0.0).unwrap();
    }
    let open = view.heights().total();
    assert_eq!(open, total + 5.0);
    view.set_collapsed(doc, key, true).unwrap();
    let (first, end) = doc.entry_range(list);
    let hidden: f64 = (first + 1..end).map(|i| f64::from(view.heights().get(i).height)).sum();
    assert_eq!(view.heights().total(), open - hidden, "the header stays; the rest counts zero");
    let rows = block_rows(doc, view);
    assert!(rows.iter().any(|r| r.0 == key && r.1 == Part::Header));
    assert!(!rows.iter().any(|r| r.0 == key && r.1 == Part::Footer));
    assert!(!rows.iter().any(|r| r.0 == find(doc, "first")));
    assert!(view.set_collapsed(doc, find(doc, "first"), true).is_err(), "a Block doesn't collapse");
    view.set_collapsed(doc, key, false).unwrap();
    assert_eq!(view.heights().total(), open);
}

#[test]
fn a_collapsed_container_hides_the_rows_a_reload_adds_to_it() {
    let mut rich = RichText::open("intro\n\n- one\n- two\n\nafter\n");
    let id = rich.subscribe();
    let list = Key::ROOT.child(1);
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_viewport(1000.0);
    view.set_collapsed(doc, list, true).unwrap();
    rich.reload("intro\n\n- one\n- two, edited\n- three\n\n  more\n\nafter\n");
    let (doc, view) = rich.view_mut(id).unwrap();
    assert!(view.is_collapsed(list));
    let (first, end) = doc.entry_range(doc.get(list).unwrap());
    for i in first + 1..end {
        assert!(view.heights().get(i).hidden(), "entry {i} of the collapsed list is shown");
    }
    let rows = block_rows(doc, view);
    assert!(rows.iter().any(|r| r.0 == list && r.1 == Part::Header));
    assert_eq!(rows.iter().filter(|r| r.0 != list).count(), 2, "{rows:?}");
    view.set_collapsed(doc, list, false).unwrap();
    assert!(block_rows(doc, view).iter().any(|r| r.0 == find(doc, "three")));
}

#[test]
fn the_window_holds_the_viewport_and_half_a_viewport_each_side() {
    let source: String = (0..100).map(|i| format!("Paragraph {i}.\n\n")).collect();
    let mut rich = RichText::open(&source);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_metrics(doc, Metrics { width: 100.0, line_height: 10.0, chars_per_line: 50.0 });
    view.set_viewport(100.0);
    view.scroll_by(doc, 500.0);
    let window = view.window(doc);
    assert_eq!(window.top, 500.0);
    assert_eq!(window.total, 1000.0);
    let tops: Vec<f64> = window.entries.iter().map(|e| e.top).collect();
    assert_eq!(tops.first(), Some(&450.0));
    assert_eq!(tops.last(), Some(&640.0));
    let thumb = view.thumb(doc);
    assert_eq!((thumb.start, thumb.size, thumb.estimated), (0.5, 0.1, 1.0));
    for e in &window.entries {
        view.set_height(doc, e.key, e.part, 10.0, 100.0).unwrap();
    }
    assert_eq!(view.thumb(doc).estimated, 0.8);
}

#[test]
fn follow_tail_breaks_on_scrolling_up_and_rearms_at_the_bottom() {
    let source = |n: usize| (0..n).map(|i| format!("Line {i}.\n\n")).collect::<String>();
    let mut rich = RichText::open(&source(50));
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_metrics(doc, Metrics { width: 100.0, line_height: 10.0, chars_per_line: 50.0 });
    view.set_viewport(100.0);
    view.scroll_to_end();
    assert_eq!(view.top(doc), 400.0);
    rich.reload(&source(60));
    let (doc, view) = rich.view_mut(id).unwrap();
    assert!(view.at_end());
    assert_eq!(view.top(doc), 500.0, "growing content keeps following");
    view.scroll_by(doc, -20.0);
    assert!(!view.at_end(), "scrolling up breaks follow");
    assert_eq!(view.top(doc), 480.0);
    rich.reload(&source(70));
    let (doc, view) = rich.view_mut(id).unwrap();
    assert_eq!(view.top(doc), 480.0, "content growing below doesn't move a reader");
    view.scroll_by(doc, 110.0);
    assert!(!view.at_end());
    view.scroll_by(doc, 100.0);
    assert!(view.at_end(), "reaching the bottom re-arms it");
}

#[test]
fn a_width_change_makes_measured_heights_scaled_estimates() {
    let mut rich = RichText::open("one\n\ntwo\n\nthree\n");
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_metrics(doc, Metrics { width: 100.0, line_height: 10.0, chars_per_line: 50.0 });
    let one = find(doc, "one");
    view.set_height(doc, one, Part::Block, 40.0, 100.0).unwrap();
    assert!(view.heights().get(0).is_measured());
    view.set_metrics(doc, Metrics { width: 200.0, line_height: 10.0, chars_per_line: 100.0 });
    let row = view.heights().get(0);
    assert_eq!(row.height, 20.0, "half the height at twice the width");
    assert!(!row.is_measured(), "a height from another width is stale");
    assert_ne!(row.width, 0, "its width tag says which width it was measured at");
    view.set_height(doc, one, Part::Block, 30.0, 200.0).unwrap();
    assert!(view.heights().get(0).is_measured());
    // A report for an old width is kept as a scaled estimate.
    view.set_height(doc, one, Part::Block, 40.0, 100.0).unwrap();
    assert_eq!(view.heights().get(0).height, 20.0);
    assert!(!view.heights().get(0).is_measured());
    // Back at the width it was measured at, a height is exact again.
    view.set_metrics(doc, Metrics { width: 100.0, line_height: 10.0, chars_per_line: 50.0 });
    assert_eq!(view.heights().get(0).height, 40.0);
    assert!(view.heights().get(0).is_measured());
}

#[test]
fn the_anchor_finds_its_block_by_content_after_an_insert_above() {
    let mut rich = RichText::open("alpha\n\nbravo\n\ncharlie\n");
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_viewport(1.0);
    let charlie = find(doc, "charlie");
    view.scroll_to(
        doc,
        Anchor { key: charlie, part: Part::Block, position: Position::Text { byte: 2, line_offset: 0.0 }, viewport_offset: 0.0 },
    )
    .unwrap();
    rich.reload("zulu\n\nalpha\n\nbravo\n\ncharlie\n");
    let (doc, view) = rich.view_mut(id).unwrap();
    let anchor = view.anchor().unwrap();
    assert_eq!(anchor.key, find(doc, "charlie"));
    assert_ne!(anchor.key, charlie, "keys are ordinals, so charlie's moved");
}

#[test]
fn a_key_removed_and_made_again_between_polls_is_removed_then_upserted() {
    let mut rich = RichText::open("alpha\n\nbravo\n");
    let id = rich.subscribe();
    rich.poll(id).unwrap();
    let bravo = Key::ROOT.child(1);
    rich.reload("alpha\n");
    rich.reload("alpha\n\n- charlie\n");
    let delta = rich.poll(id).unwrap().unwrap();
    let at = |wanted: &dyn Fn(&Op) -> bool| delta.ops.iter().position(wanted).unwrap_or_else(|| panic!("{:?}", delta.ops));
    let removed = at(&|op| *op == Op::Remove(bravo));
    let made = at(&|op| matches!(op, Op::Upsert(c) if c.key == bravo && c.kind == Kind::List));
    assert!(removed < made, "the old node goes before the new one comes");
    assert!(delta.ops.iter().any(|op| matches!(op, Op::Children { parent, .. } if *parent == bravo)));
}

#[test]
fn a_freed_slot_reused_under_another_key_still_removes_the_old_key() {
    let mut rich = RichText::open("alpha\n\nbravo\n");
    let id = rich.subscribe();
    rich.poll(id).unwrap();
    let bravo = Key::ROOT.child(1);
    let slot = rich.doc().get(bravo).unwrap();
    rich.reload("alpha\n");
    rich.reload("> alpha\n");
    let quoted = Key::ROOT.child(0).child(0);
    assert_eq!(rich.doc().get(quoted), Some(slot), "the quote's paragraph took bravo's slot");
    let delta = rich.poll(id).unwrap().unwrap();
    assert!(delta.ops.contains(&Op::Remove(bravo)), "{:?}", delta.ops);
    assert!(delta.ops.iter().any(|op| matches!(op, Op::Upsert(c) if c.key == quoted)));
}
