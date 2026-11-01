//! Deltas in the change stream's vocabulary (docs/design/change-stream.md),
//! from a ring of changes rather than by comparing trees (#540).
use std::collections::{HashMap, VecDeque};

use super::tree::{Key, NodeContent, NodeId, Tree};
use crate::stream::ListOp;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Op {
    Upsert(Box<NodeContent>),
    /// Its descendants go by their own ops.
    Remove(Key),
    Children {
        parent: Key,
        ops: Vec<ListOp<Key>>,
    },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Delta {
    pub generation: u64,
    /// Clear the model before applying it.
    pub snapshot: bool,
    /// Removes, then upserts in document order, then child lists.
    pub ops: Vec<Op>,
}

#[derive(Debug, Clone)]
pub(crate) enum What {
    Created,
    Content,
    Removed,
    Children(Box<[ListOp<Key>]>),
}

#[derive(Debug, Clone)]
struct Change {
    generation: u64,
    node: NodeId,
    key: Key,
    what: What,
}

/// In changes plus child-list operations.
pub const RING_CAPACITY: usize = 1 << 16;

#[derive(Debug)]
pub(crate) struct Ring {
    changes: VecDeque<Change>,
    weight: usize,
    capacity: usize,
    /// A subscriber at this generation or later is served from the ring.
    /// Eviction may leave part of the floor's own generation behind, which
    /// is fine: a subscriber at the floor has seen all of it.
    floor: u64,
}

impl Ring {
    pub fn new(capacity: usize) -> Ring {
        Ring { changes: VecDeque::new(), weight: 0, capacity: capacity.max(1), floor: 0 }
    }

    fn weight(change: &Change) -> usize {
        1 + match &change.what {
            What::Children(ops) => ops.len(),
            _ => 0,
        }
    }

    pub fn push(&mut self, generation: u64, node: NodeId, key: Key, what: What) {
        let change = Change { generation, node, key, what };
        self.weight += Self::weight(&change);
        self.changes.push_back(change);
        while self.weight > self.capacity {
            let Some(old) = self.changes.pop_front() else { break };
            self.weight -= Self::weight(&old);
            self.floor = self.floor.max(old.generation);
        }
    }

    pub fn floor(&self) -> u64 {
        self.floor
    }

    #[cfg(test)]
    pub fn oldest(&self) -> Option<u64> {
        self.changes.front().map(|c| c.generation)
    }
}

pub(crate) fn snapshot(doc: &Tree, generation: u64) -> Delta {
    let order = doc.preorder();
    let mut ops: Vec<Op> = order.iter().map(|&id| Op::Upsert(Box::new(doc.content(id)))).collect();
    for &id in &order {
        let children: Vec<ListOp<Key>> =
            doc.children(id).enumerate().map(|(at, child)| ListOp::Insert { at, key: doc.key(child) }).collect();
        if !children.is_empty() {
            ops.push(Op::Children { parent: doc.key(id), ops: children });
        }
    }
    Delta { generation, snapshot: true, ops }
}

#[derive(Default)]
struct Seen {
    /// The subscriber has it: its first change since wasn't its creation.
    known: bool,
    /// Created again after the subscriber knew it.
    recreated: bool,
    touched: bool,
    /// Its node at its latest change.
    node: Option<NodeId>,
    /// Child-list operations since its latest creation.
    children: Vec<ListOp<Key>>,
}

pub(crate) fn changes(doc: &Tree, ring: &Ring, since: u64, generation: u64) -> Delta {
    let first = ring.changes.partition_point(|c| c.generation <= since);
    let mut seen: HashMap<Key, Seen> = HashMap::new();
    let mut order: Vec<Key> = vec![];
    for change in ring.changes.range(first..) {
        let entry = seen.entry(change.key).or_insert_with(|| {
            order.push(change.key);
            Seen { known: !matches!(change.what, What::Created), ..Seen::default() }
        });
        match &change.what {
            What::Created => {
                entry.recreated |= entry.known;
                entry.touched = true;
                entry.children.clear();
            }
            What::Content => entry.touched = true,
            What::Removed => entry.children.clear(),
            What::Children(ops) => entry.children.extend_from_slice(ops),
        }
        entry.node = Some(change.node);
    }
    // A removed node's slot may be reused under another key, so a node is
    // live for its key only if the key still names it.
    let live = |key: &Key| seen[key].node.filter(|&id| doc.get(*key) == Some(id));
    let mut ops = vec![];
    for key in &order {
        let entry = &seen[key];
        if entry.known && (live(key).is_none() || entry.recreated) {
            ops.push(Op::Remove(*key));
        }
    }
    let mut upserts: Vec<NodeId> = order.iter().filter(|key| seen[key].touched).filter_map(live).collect();
    // Document order: the root, then by index entry.
    upserts.sort_by_key(|&id| (doc.parent(id).is_some(), doc.node(id).entry));
    ops.extend(upserts.into_iter().map(|id| Op::Upsert(Box::new(doc.content(id)))));
    for key in &order {
        let entry = &seen[key];
        if !entry.children.is_empty() && live(key).is_some() {
            ops.push(Op::Children { parent: *key, ops: entry.children.clone() });
        }
    }
    Delta { generation, snapshot: false, ops }
}
