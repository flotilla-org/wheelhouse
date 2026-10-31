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
fn polls_need_a_live_subscription() {
    let mut rich = RichText::open(GFM);
    let id = rich.subscribe();
    assert!(rich.unsubscribe(id));
    assert_eq!(rich.poll(id), Err(UnknownSubscription(id)));
    assert_eq!(rich.changed_since(id), Err(UnknownSubscription(id)));
    assert!(!rich.unsubscribe(id));
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
