use std::collections::HashMap;

use hegel::generators as gs;
use hegel::TestCase;

use super::generate::{self, Choose};
use super::*;
use crate::stream::apply_list;

pub(crate) struct Draws<'a>(pub &'a TestCase);

impl Choose for Draws<'_> {
    fn below(&mut self, n: usize) -> usize {
        if n == 0 {
            return 0;
        }
        self.0.draw(gs::integers::<usize>().max_value(n - 1))
    }
}

/// What a frontend keeps: a model built only from deltas.
#[derive(Debug, Default)]
struct Model {
    nodes: HashMap<Key, NodeContent>,
    children: HashMap<Key, Vec<Key>>,
    generation: u64,
}

impl Model {
    fn apply(&mut self, delta: &Delta) -> Result<(), String> {
        if delta.snapshot {
            self.nodes.clear();
            self.children.clear();
        }
        for op in &delta.ops {
            match op {
                Op::Upsert(content) => {
                    self.nodes.insert(content.key, (**content).clone());
                }
                Op::Remove(key) => {
                    self.nodes.remove(key).ok_or_else(|| format!("removed {key:?}, which the model hasn't"))?;
                    self.children.remove(key);
                }
                Op::Children { parent, ops } => {
                    if !self.nodes.contains_key(parent) {
                        return Err(format!("children of {parent:?}, which the model hasn't"));
                    }
                    apply_list(self.children.entry(*parent).or_default(), ops)?;
                }
            }
        }
        self.children.retain(|_, list| !list.is_empty());
        if delta.generation < self.generation {
            return Err("generations went backwards".into());
        }
        self.generation = delta.generation;
        Ok(())
    }
}

fn fresh(rich: &mut RichText) -> Model {
    let id = rich.subscribe();
    let mut model = Model::default();
    model.apply(&rich.poll(id).unwrap().expect("a first poll is a snapshot")).unwrap();
    rich.unsubscribe(id);
    model
}

fn follow(rich: &mut RichText, id: SubscriptionId, model: &mut Model) {
    if let Some(delta) = rich.poll(id).unwrap() {
        model.apply(&delta).unwrap();
    }
    assert!(!rich.changed_since(id).unwrap());
}

fn same(model: &Model, fresh: &Model) {
    assert_eq!(model.nodes, fresh.nodes, "a followed model's nodes differ from a fresh snapshot's");
    assert_eq!(model.children, fresh.children, "a followed model's child lists differ from a fresh snapshot's");
}

/// With rings small enough to fall off.
#[hegel::test(test_cases = 300)]
fn followed_models_match_fresh_snapshots(tc: TestCase) {
    let mut c = Draws(&tc);
    let size = c.below(6) + 1;
    let mut blocks = generate::document(&mut c, size);
    let capacity = [8, 64, RING_CAPACITY][c.below(3)];
    let mut rich = RichText::with_ring_capacity(&generate::render(&blocks), capacity);
    let every = rich.subscribe();
    let rarely = rich.subscribe();
    let mut each = Model::default();
    let mut rare = Model::default();
    follow(&mut rich, every, &mut each);
    for _ in 0..c.below(12) {
        generate::edit(&mut c, &mut blocks);
        let before = rich.generation();
        let changed = rich.reload(&generate::render(&blocks));
        assert_eq!(rich.generation(), before + u64::from(changed), "a reload's generation rises only if it changed something");
        assert_eq!(rich.changed_since(every).unwrap(), changed);
        follow(&mut rich, every, &mut each);
        same(&each, &fresh(&mut rich));
        if c.chance(4) {
            follow(&mut rich, rarely, &mut rare);
        }
    }
    follow(&mut rich, rarely, &mut rare);
    let snapshot = fresh(&mut rich);
    same(&each, &snapshot);
    same(&rare, &snapshot);
}

fn versions(doc: &Tree) -> HashMap<Key, NodeContent> {
    doc.preorder().into_iter().map(|id| (doc.key(id), doc.content(id))).collect()
}

#[hegel::test(test_cases = 300)]
fn unchanged_blocks_keep_their_versions(tc: TestCase) {
    let mut c = Draws(&tc);
    let size = c.below(8) + 1;
    let mut blocks = generate::document(&mut c, size);
    let mut rich = RichText::open(&generate::render(&blocks));
    for _ in 0..c.below(6) + 1 {
        let before = versions(rich.doc());
        let newest = before.values().map(|c| c.version).max().unwrap_or(0);
        generate::edit(&mut c, &mut blocks);
        rich.reload(&generate::render(&blocks));
        for (key, after) in versions(rich.doc()) {
            match before.get(&key) {
                Some(old) if NodeContent { version: after.version, ..old.clone() } == after => {
                    assert_eq!(old.version, after.version, "an unchanged node's version changed")
                }
                _ => assert!(after.version > newest, "a new or changed node kept an old version"),
            }
        }
    }
}
