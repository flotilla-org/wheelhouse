//! The height index: chunks with Fenwick trees over their sums (#541).
use std::cell::Cell;
use std::ops::{AddAssign, Range, Sub};

pub const CHUNK: usize = 256;

pub const MEASURED: u8 = 1;
/// Never measured, or measured at another width.
pub const ESTIMATED: u8 = 2;
/// Inside a collapsed Container: it counts as zero.
pub const HIDDEN: u8 = 4;

/// 8 bytes.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Entry {
    /// In the frontend's units.
    pub height: f32,
    /// The width tag it was measured at (see View).
    pub width: u16,
    pub flags: u8,
    _pad: u8,
}

impl Entry {
    pub fn estimated(height: f32, width: u16) -> Entry {
        Entry { height, width, flags: ESTIMATED, _pad: 0 }
    }

    pub fn measured(height: f32, width: u16) -> Entry {
        Entry { height, width, flags: MEASURED, _pad: 0 }
    }

    pub fn hidden(&self) -> bool {
        self.flags & HIDDEN != 0
    }

    pub fn is_measured(&self) -> bool {
        self.flags & MEASURED != 0
    }

    /// Its width tag stays, saying which width it was measured at.
    pub fn stale(mut self) -> Entry {
        self.flags = (self.flags & !MEASURED) | ESTIMATED;
        self
    }

    pub fn with_hidden(mut self, hidden: bool) -> Entry {
        if hidden {
            self.flags |= HIDDEN;
        } else {
            self.flags &= !HIDDEN;
        }
        self
    }

    pub fn size(&self) -> f64 {
        if self.hidden() {
            0.0
        } else {
            f64::from(self.height)
        }
    }
}

#[derive(Debug, Clone, Default)]
struct Fenwick<T> {
    tree: Vec<T>,
}

impl<T: Copy + Default + AddAssign + Sub<Output = T> + PartialOrd> Fenwick<T> {
    fn build(values: impl Iterator<Item = T>) -> Self {
        let mut tree: Vec<T> = values.collect();
        for i in 1..=tree.len() {
            let j = i + i.isolate_lowest_one();
            if j <= tree.len() {
                let v = tree[i - 1];
                tree[j - 1] += v;
            }
        }
        Fenwick { tree }
    }

    fn add(&mut self, index: usize, delta: T) {
        let mut i = index + 1;
        while i <= self.tree.len() {
            self.tree[i - 1] += delta;
            i += i.isolate_lowest_one();
        }
    }

    fn prefix(&self, count: usize, probes: &Cell<u64>) -> T {
        let mut sum = T::default();
        let mut i = count;
        while i > 0 {
            probes.set(probes.get() + 1);
            sum += self.tree[i - 1];
            i &= i - 1;
        }
        sum
    }

    fn push(&mut self, value: T, probes: &Cell<u64>) {
        let i = self.tree.len() + 1;
        let low = i.isolate_lowest_one();
        let covered = self.prefix(i - 1, probes) - self.prefix(i - low, probes);
        let mut node = value;
        node += covered;
        self.tree.push(node);
    }

    /// The first index whose running sum passes `target`, and how far past
    /// the sums before it `target` is. None past the end.
    fn search(&self, target: T, probes: &Cell<u64>) -> Option<(usize, T)> {
        let mut pos = 0;
        let mut rest = target;
        let mut step = self.tree.len().next_power_of_two();
        while step > 0 {
            probes.set(probes.get() + 1);
            if pos + step <= self.tree.len() && self.tree[pos + step - 1] <= rest {
                pos += step;
                rest = rest - self.tree[pos - 1];
            }
            step >>= 1;
        }
        (pos < self.tree.len()).then_some((pos, rest))
    }
}

#[derive(Debug, Clone, Copy)]
struct Chunk {
    base: u32,
    len: u32,
}

#[derive(Debug, Default)]
pub struct HeightIndex {
    pool: Vec<Entry>,
    chunks: Vec<Chunk>,
    /// Pool regions no chunk uses.
    free: Vec<u32>,
    sums: Vec<f64>,
    estimated: Vec<f64>,
    by_size: Fenwick<f64>,
    by_count: Fenwick<i64>,
    len: usize,
    /// Updates since the trees were last rebuilt, to bound float drift.
    updates: usize,
    probes: Cell<u64>,
}

impl HeightIndex {
    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub fn total(&self) -> f64 {
        self.by_size.prefix(self.chunks.len(), &self.probes)
    }

    pub fn estimated_total(&self) -> f64 {
        self.estimated.iter().sum()
    }

    /// For the scale tests.
    pub fn probes(&self) -> u64 {
        self.probes.get()
    }

    fn slots(&self, c: usize) -> Range<usize> {
        let chunk = self.chunks[c];
        chunk.base as usize..(chunk.base + chunk.len) as usize
    }

    /// The chunk holding entry `index`, and its place in it. `index` may be
    /// the length, which is the end of the last chunk.
    fn locate(&self, index: usize) -> (usize, usize) {
        if index >= self.len {
            let last = self.chunks.len() - 1;
            return (last, self.chunks[last].len as usize);
        }
        let (c, rest) = self.by_count.search(index as i64, &self.probes).expect("an index below the length is in a chunk");
        (c, rest as usize)
    }

    pub fn get(&self, index: usize) -> Entry {
        let (c, at) = self.locate(index);
        self.pool[self.chunks[c].base as usize + at]
    }

    fn new_region(&mut self) -> u32 {
        if let Some(base) = self.free.pop() {
            return base;
        }
        let base = self.pool.len() as u32;
        self.pool.resize(self.pool.len() + CHUNK, Entry::default());
        base
    }

    fn chunk_totals(&self, c: usize) -> (f64, f64) {
        let mut sum = 0.0;
        let mut estimated = 0.0;
        for entry in &self.pool[self.slots(c)] {
            sum += entry.size();
            if !entry.is_measured() {
                estimated += entry.size();
            }
        }
        (sum, estimated)
    }

    fn rebuild(&mut self) {
        let totals: Vec<(f64, f64)> = (0..self.chunks.len()).map(|c| self.chunk_totals(c)).collect();
        self.sums = totals.iter().map(|t| t.0).collect();
        self.estimated = totals.iter().map(|t| t.1).collect();
        self.by_size = Fenwick::build(self.sums.iter().copied());
        self.by_count = Fenwick::build(self.chunks.iter().map(|c| i64::from(c.len)));
        self.updates = 0;
    }

    fn refresh(&mut self, c: usize, grew: i64) {
        let (sum, estimated) = self.chunk_totals(c);
        self.by_size.add(c, sum - self.sums[c]);
        self.sums[c] = sum;
        self.estimated[c] = estimated;
        if grew != 0 {
            self.by_count.add(c, grew);
        }
        self.updates += 1;
        if self.updates > 1 << 14 {
            self.rebuild();
        }
    }

    pub fn set(&mut self, index: usize, entry: Entry) {
        let (c, at) = self.locate(index);
        let slot = self.chunks[c].base as usize + at;
        if self.pool[slot] == entry {
            return;
        }
        self.pool[slot] = entry;
        self.refresh(c, 0);
    }

    /// Replaces every entry with `f(index, entry)`: O(n), where a `set` each
    /// would re-total a chunk each.
    pub fn update_all(&mut self, mut f: impl FnMut(usize, Entry) -> Entry) {
        let mut index = 0;
        for c in 0..self.chunks.len() {
            for slot in self.slots(c) {
                self.pool[slot] = f(index, self.pool[slot]);
                index += 1;
            }
        }
        self.rebuild();
    }

    pub fn push(&mut self, entry: Entry) {
        self.insert(self.len, &[entry]);
    }

    pub fn insert(&mut self, index: usize, entries: &[Entry]) {
        assert!(index <= self.len, "insert past the end");
        if entries.is_empty() {
            return;
        }
        if self.chunks.is_empty() {
            self.append_chunks(entries);
            return;
        }
        let (c, at) = self.locate(index);
        let room = CHUNK - self.chunks[c].len as usize;
        if entries.len() <= room {
            let slots = self.slots(c);
            let start = slots.start + at;
            self.pool.copy_within(start..slots.end, start + entries.len());
            self.pool[start..start + entries.len()].copy_from_slice(entries);
            self.chunks[c].len += entries.len() as u32;
            self.len += entries.len();
            self.refresh(c, entries.len() as i64);
            return;
        }
        if c == self.chunks.len() - 1 && at == self.chunks[c].len as usize {
            // Appending past a full tail: new chunks at the end.
            self.append_chunks(entries);
            return;
        }
        // Put the new entries in chunks of their own before chunk c, or
        // split it at `at` and put them between its halves.
        let mut fresh = vec![];
        for group in entries.chunks(CHUNK) {
            let base = self.new_region();
            self.pool[base as usize..base as usize + group.len()].copy_from_slice(group);
            fresh.push(Chunk { base, len: group.len() as u32 });
        }
        if at == 0 {
            self.chunks.splice(c..c, fresh);
        } else {
            let tail: Vec<Entry> = self.pool[self.slots(c)][at..].to_vec();
            if !tail.is_empty() {
                let base = self.new_region();
                self.pool[base as usize..base as usize + tail.len()].copy_from_slice(&tail);
                fresh.push(Chunk { base, len: tail.len() as u32 });
            }
            self.chunks[c].len = at as u32;
            self.chunks.splice(c + 1..c + 1, fresh);
        }
        self.len += entries.len();
        self.rebuild();
    }

    fn append_chunks(&mut self, entries: &[Entry]) {
        for group in entries.chunks(CHUNK) {
            let base = self.new_region();
            self.pool[base as usize..base as usize + group.len()].copy_from_slice(group);
            self.chunks.push(Chunk { base, len: group.len() as u32 });
            let c = self.chunks.len() - 1;
            let (sum, estimated) = self.chunk_totals(c);
            self.sums.push(sum);
            self.estimated.push(estimated);
            self.by_size.push(sum, &self.probes);
            self.by_count.push(group.len() as i64, &self.probes);
        }
        self.len += entries.len();
    }

    pub fn remove(&mut self, range: Range<usize>) {
        assert!(range.start <= range.end && range.end <= self.len, "remove out of range");
        let mut count = range.len();
        if count == 0 {
            return;
        }
        let (mut c, mut at) = self.locate(range.start);
        let mut emptied = false;
        while count > 0 {
            let slots = self.slots(c);
            let take = count.min(slots.len() - at);
            self.pool.copy_within(slots.start + at + take..slots.end, slots.start + at);
            self.chunks[c].len -= take as u32;
            count -= take;
            self.len -= take;
            if self.chunks[c].len == 0 {
                emptied = true;
            } else if !emptied {
                self.refresh(c, -(take as i64));
            }
            c += 1;
            at = 0;
        }
        if emptied {
            let free: Vec<u32> = self.chunks.iter().filter(|chunk| chunk.len == 0).map(|chunk| chunk.base).collect();
            self.free.extend(free);
            self.chunks.retain(|chunk| chunk.len > 0);
            self.rebuild();
        }
    }

    /// The length gives the total.
    pub fn offset_of(&self, index: usize) -> f64 {
        if index >= self.len {
            return self.total();
        }
        let (c, at) = self.locate(index);
        let mut offset = self.by_size.prefix(c, &self.probes);
        let base = self.chunks[c].base as usize;
        for entry in &self.pool[base..base + at] {
            self.probes.set(self.probes.get() + 1);
            offset += entry.size();
        }
        offset
    }

    /// The entry holding `offset`, and how far into it `offset` is.
    /// Zero-height entries hold nothing.
    pub fn entry_at(&self, offset: f64) -> Option<(usize, f64)> {
        if offset.is_nan() || offset < 0.0 || self.chunks.is_empty() {
            return None;
        }
        let (c, mut rest) = self.by_size.search(offset, &self.probes)?;
        let first: usize = self.by_count.prefix(c, &self.probes) as usize;
        let slots = self.slots(c);
        let mut last = None;
        for (i, entry) in self.pool[slots].iter().enumerate() {
            self.probes.set(self.probes.get() + 1);
            let size = entry.size();
            if size > 0.0 {
                if rest < size {
                    return Some((first + i, rest));
                }
                last = Some((first + i, size));
            }
            rest -= size;
        }
        // Rounding put the offset just past the chunk's last entry.
        last
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_entry_is_eight_bytes() {
        assert_eq!(std::mem::size_of::<Entry>(), 8);
    }
}
