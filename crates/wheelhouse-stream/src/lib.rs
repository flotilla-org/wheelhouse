//! The change-stream primitives the sidebar and Rich Text share
//! (docs/design/change-stream.md).
use std::collections::{HashMap, HashSet};
use std::fmt;
use std::hash::Hash;

use serde::{Deserialize, Serialize};

/// A subscription to the sidebar's changes (`Sidebar::subscribe`) or a Rich Text's.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct SubscriptionId(pub u64);

/// A subscription never given, or one since unsubscribed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct UnknownSubscription(pub SubscriptionId);

impl fmt::Display for UnknownSubscription {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "unknown subscription {}: it was never given or was unsubscribed; subscribe again", self.0 .0)
    }
}

impl std::error::Error for UnknownSubscription {}

/// A positional change to an ordered list. A move takes the item out at
/// `from` and puts it in at `to` in the list without it.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "kebab-case")]
pub enum ListOp<K> {
    Insert { at: usize, key: K },
    Remove { at: usize },
    Move { from: usize, to: usize },
}

pub fn apply_list<K>(list: &mut Vec<K>, ops: &[ListOp<K>]) -> Result<(), String>
where
    K: Clone,
{
    for op in ops {
        match op {
            ListOp::Insert { at, key } if *at <= list.len() => list.insert(*at, key.clone()),
            ListOp::Remove { at } if *at < list.len() => {
                list.remove(*at);
            }
            ListOp::Move { from, to } if *from < list.len() && *to < list.len() => {
                let key = list.remove(*from);
                list.insert(*to, key);
            }
            _ => return Err("a list operation is out of range".into()),
        }
    }
    Ok(())
}

/// Operations that turn `before` into `after`, both lists of distinct keys.
/// Keys in both keep their place where they can: the longest run already in
/// order stays put, and only the others move, each at most twice.
pub fn list_ops<K>(before: &[K], after: &[K]) -> Vec<ListOp<K>>
where
    K: Clone + Eq + Hash,
{
    let wanted: HashSet<&K> = after.iter().collect();
    // Placement keys are distinct within a child list by construction (a
    // loop yields one row per entity); a repeat would make the delta diverge.
    debug_assert_eq!(wanted.len(), after.len(), "a list's keys are distinct");
    debug_assert_eq!(before.iter().collect::<HashSet<_>>().len(), before.len(), "a list's keys are distinct");
    let mut list: Vec<K> = before.to_vec();
    let mut ops = vec![];
    for at in (0..list.len()).rev() {
        if !wanted.contains(&list[at]) {
            list.remove(at);
            ops.push(ListOp::Remove { at });
        }
    }
    let position: HashMap<&K, usize> = list.iter().enumerate().map(|(at, key)| (key, at)).collect();
    // Of the kept keys, in their new order, those whose old positions form
    // the longest increasing run stay where they are.
    let kept: Vec<(usize, usize)> = after.iter().enumerate().filter_map(|(i, key)| Some((i, *position.get(key)?))).collect();
    let mut stable = vec![false; after.len()];
    for k in longest_increasing(&kept.iter().map(|&(_, at)| at).collect::<Vec<_>>()) {
        stable[kept[k].0] = true;
    }
    for (i, key) in after.iter().enumerate() {
        loop {
            if list.get(i) == Some(key) {
                break;
            }
            // Everything before `i` is in place and the stable keys are in
            // order, so what sits here is a key that moves: send it to the end.
            if stable[i] {
                let last = list.len() - 1;
                let moved = list.remove(i);
                list.push(moved);
                ops.push(ListOp::Move { from: i, to: last });
                continue;
            }
            if let Some(from) = list[i..].iter().position(|k| k == key).map(|at| at + i) {
                let moved = list.remove(from);
                list.insert(i, moved);
                ops.push(ListOp::Move { from, to: i });
            } else {
                list.insert(i, key.clone());
                ops.push(ListOp::Insert { at: i, key: key.clone() });
            }
            break;
        }
    }
    ops
}

/// Indices into `values` of one longest strictly increasing subsequence.
fn longest_increasing(values: &[usize]) -> Vec<usize> {
    // tails[l]: the index ending the smallest-ended run of length l + 1.
    let mut tails: Vec<usize> = vec![];
    let mut previous = vec![usize::MAX; values.len()];
    for (i, &value) in values.iter().enumerate() {
        let length = tails.partition_point(|&t| values[t] < value);
        if length > 0 {
            previous[i] = tails[length - 1];
        }
        if length == tails.len() {
            tails.push(i);
        } else {
            tails[length] = i;
        }
    }
    let mut run = vec![];
    let mut at = tails.last().copied().unwrap_or(usize::MAX);
    while at != usize::MAX {
        run.push(at);
        at = previous[at];
    }
    run.reverse();
    run
}

#[cfg(test)]
mod tests;
