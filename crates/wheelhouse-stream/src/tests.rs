use hegel::generators as gs;
use hegel::TestCase;

use crate::{apply_list, list_ops, ListOp};

/// List operations turn any list of distinct keys into any other, and move
/// only keys outside the longest run already in order, each at most twice.
#[hegel::test(test_cases = 1000)]
fn list_ops_turn_one_list_into_another(tc: TestCase) {
    let distinct = |keys: Vec<u8>| {
        let mut seen = std::collections::HashSet::new();
        keys.into_iter().filter(|key| seen.insert(*key)).collect::<Vec<_>>()
    };
    let keys = || gs::vecs(gs::integers::<u8>().max_value(30)).max_size(25);
    let before = distinct(tc.draw(keys()));
    let after = distinct(tc.draw(keys()));
    let ops = list_ops(&before, &after);
    let mut list = before.clone();
    apply_list(&mut list, &ops).unwrap();
    assert_eq!(list, after);
    // The kept keys' old positions, in their new order; those outside its
    // longest increasing run are what has to move.
    let old: Vec<usize> = after.iter().filter_map(|key| before.iter().position(|k| k == key)).collect();
    let mut run = vec![1; old.len()];
    for i in 0..old.len() {
        for j in 0..i {
            if old[j] < old[i] {
                run[i] = run[i].max(run[j] + 1);
            }
        }
    }
    let moving = old.len() - run.iter().copied().max().unwrap_or(0);
    let moves = ops.iter().filter(|op| matches!(op, ListOp::Move { .. })).count();
    assert!(moves <= 2 * moving, "{moves} moves for {moving} keys out of order");
    let inserts = ops.iter().filter(|op| matches!(op, ListOp::Insert { .. })).count();
    assert_eq!(inserts, after.len() - old.len());
    let removes = ops.iter().filter(|op| matches!(op, ListOp::Remove { .. })).count();
    assert_eq!(removes, before.len() - old.len());
}

/// A list op past a list's end fails rather than panicking.
#[test]
fn an_out_of_range_list_op_fails() {
    let mut list = vec![1, 2];
    assert!(apply_list(&mut list, &[ListOp::Move { from: 2, to: 0 }]).is_err());
    assert!(apply_list(&mut list, &[ListOp::Insert { at: 3, key: 9 }]).is_err());
    assert!(apply_list(&mut list, &[ListOp::Remove { at: 2 }]).is_err());
}
