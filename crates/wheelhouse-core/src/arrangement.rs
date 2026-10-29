//! An arrangement as a frontend edits it (ADR 0012, "Arrangements are
//! documents"; #441): panels with stable numeric IDs, each panel's tabs and
//! Selected View, and relative split weights. The root's axis is the
//! document's, and each level below alternates.
//!
//! This is the live shape a frontend keeps and edits, not the stored
//! [`crate::slots::ArrangementDoc`]: its panel IDs are numbers with a
//! `next_id` counter, the root keeps an axis while it is still a tab panel,
//! and panels carry what the stored document doesn't (a provider's name for
//! a panel, a Presentation State move, a restore's mark). Tabs are whatever
//! the frontend names Views by (`T`): the native shell's View Addresses,
//! a page's strings.
//!
//! Every edit is one [`Edit`], applied by [`apply`]; [`problem`] is the
//! validity check. Each keeps the semantics of the native shell's C rules
//! it replaced (`native/src/shell/shell_arrangement.c`), edge cases
//! included: a valid arrangement stays valid, and what an edit refuses it
//! leaves unchanged, except where noted.
use crate::suggested_layout::Axis;

mod weight;
pub use weight::quantize;

fn children_weights<T>(children: &[Panel<T>]) -> Vec<f32> {
    children.iter().map(|c| c.weight).collect()
}

/// The sibling the boundary after child `i` of a split pairs it with, given
/// the children's weights: the next one with space (a weight above zero).
/// A child with no space has no boundary, and neither has the last one
/// with space. The one place this rule lives: resize and equalize use it,
/// and the native shell asks it to draw the splitters.
pub fn boundary_partner(weights: &[f32], i: usize) -> Option<usize> {
    if !weights.get(i)?.gt(&0.0) {
        return None;
    }
    weights[i + 1..].iter().position(|w| *w > 0.0).map(|p| i + 1 + p)
}

#[cfg(any(test, feature = "test-support"))]
pub mod generate;

#[cfg(test)]
mod tests;

/// A panel's ID, stable within its arrangement. 0 is no panel.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PanelId(pub u64);

/// A side of a panel: where a split puts the new panel.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Dir {
    Left,
    Up,
    Right,
    Down,
}

impl Dir {
    pub const ALL: [Dir; 4] = [Dir::Left, Dir::Up, Dir::Right, Dir::Down];

    /// The axis a split towards it divides along: `Row` for left and right.
    pub fn axis(self) -> Axis {
        match self {
            Dir::Left | Dir::Right => Axis::Row,
            Dir::Up | Dir::Down => Axis::Column,
        }
    }

    /// Whether the new panel comes after the one split.
    fn after(self) -> bool {
        matches!(self, Dir::Right | Dir::Down)
    }
}

fn flip(axis: Axis) -> Axis {
    match axis {
        Axis::Row => Axis::Column,
        Axis::Column => Axis::Row,
    }
}

/// A panel. A split's children divide it along its axis by weight; a leaf
/// holds tabs. Edits never give a split tabs, but a document may say so
/// (and [`problem`] then reports it), so a panel has room for both.
#[derive(Clone, Debug, PartialEq)]
pub struct Panel<T> {
    pub id: PanelId,
    /// Relative to its siblings. Edits write weights quantized
    /// ([`quantize`]) and keep a split's summing to one; weights a document
    /// came with are kept as it had them.
    pub weight: f32,
    pub children: Vec<Panel<T>>,
    pub tabs: Vec<T>,
    /// The Selected View: one of the tabs, or none.
    pub selected: Option<T>,
    /// The ID a provider's document gave the panel (#445): part of that
    /// plan, it stays when its last tab goes. Empty is none.
    pub named: Option<String>,
    /// A panel that took another's tabs ([`Edit::Add`]) takes that panel's
    /// Presentation State with them when the arrangement is next set.
    pub options_from: Option<PanelId>,
    /// A dock restore took a sidebar section out of it
    /// ([`crate::dock::restore`]): the sidebar's next commit leaves it out.
    pub restore_emptied: bool,
}

/// Dropping a panel takes its descendants in a loop, so a deep chain
/// can't exhaust the stack.
impl<T> Drop for Panel<T> {
    fn drop(&mut self) {
        let mut stack = std::mem::take(&mut self.children);
        while let Some(mut panel) = stack.pop() {
            stack.append(&mut panel.children);
        }
    }
}

impl<T> Panel<T> {
    pub fn new(id: PanelId) -> Self {
        Self { id, weight: 0.0, children: vec![], tabs: vec![], selected: None, named: None, options_from: None, restore_emptied: false }
    }

    /// Whether it goes when its last tab does (#358): one a provider named
    /// stays.
    pub fn closes_when_emptied(&self) -> bool {
        self.named.as_deref().is_none_or(str::is_empty)
    }

    /// Its first leaf: down the first children.
    pub fn first_leaf(&self) -> &Panel<T> {
        let mut leaf = self;
        while let Some(first) = leaf.children.first() {
            leaf = first;
        }
        leaf
    }
}

/// An arrangement: at most one root, and what it was edited from.
#[derive(Clone, Debug, PartialEq)]
pub struct Arrangement<T> {
    pub root: Option<Panel<T>>,
    /// The root's axis, kept while the root is a tab panel, so that it
    /// splits as it did.
    pub root_axis: Axis,
    /// The next panel's ID; every panel's is below it.
    pub next_id: u64,
    /// Tabs removed since the frontend last set it, oldest first, whose
    /// Views go when it is set unless an arrangement has them again.
    pub removed: Vec<T>,
}

impl<T> Default for Arrangement<T> {
    fn default() -> Self {
        Self { root: None, root_axis: Axis::Column, next_id: 1, removed: vec![] }
    }
}

/// An edit of an arrangement: one of the rules.
#[derive(Clone, Debug, PartialEq)]
pub enum Edit<T> {
    /// Replaces every panel with one empty root, as a layout producer
    /// starts a document. The root's axis stays until it splits. Every tab
    /// is removed.
    Clear,
    /// Removes every panel and tab: an emptied Floating Panel goes.
    Discard,
    /// Inserts a sibling beside `panel` when its parent splits along
    /// `dir`'s axis, rescaling the siblings; otherwise bisects `panel`.
    Split { panel: PanelId, dir: Dir },
    /// Adds an empty panel as `parent`'s last child; with no panels, no
    /// parent makes the root first. A parent with tabs first moves them,
    /// its Selected View and its Presentation State into a new first child
    /// of weight 1, keeping its own ID. A positive `weight` is the new
    /// panel's and its siblings keep theirs, as the sidebar's sections do;
    /// otherwise it takes an equal share and its siblings scale to make
    /// room, each counted as at least 0.01.
    Add { parent: Option<PanelId>, weight: f32 },
    /// Removes `panel` and its tabs as the sidebar's reconciliation does:
    /// its siblings keep their weights, and a split it leaves with one panel
    /// stays a split. A split it leaves with none goes too, short of the
    /// root. Refuses the root.
    Remove { panel: PanelId },
    /// Removes `panel` and its tabs. Its siblings grow into its space; a
    /// split left with one child is replaced by it, merging into a parent
    /// along the same axis. Reports the leaf that takes focus if `panel` had
    /// it. Refuses the root.
    Close { panel: PanelId },
    /// Moves `tab` into `to` after `after` (none, or not there: first),
    /// from wherever it was, and selects it there when `select`; otherwise
    /// `to` keeps its Selected View. A panel it leaves while selected has no
    /// Selected View until one is selected. Moving a tab is `select: true`.
    InsertTab { tab: T, to: PanelId, after: Option<T>, select: bool },
    /// Takes `tab` out but keeps its View, for another arrangement to take.
    DetachTab { tab: T },
    /// Removes `tab`; its View goes when the arrangement is set.
    RemoveTab { tab: T },
    /// Drops `tab` into `to` (no `dir`), or into the new panel a split of
    /// `to` towards `dir` makes, after `after` as a move places it, and
    /// settles the panel it left, when this arrangement has it, in the same
    /// edit ([`Edit::Settle`]). A move within one panel settles nothing; a
    /// split of the source itself never closes it. With no tab it only
    /// splits, which is what measuring a drop needs before one is chosen.
    Drop { tab: Option<T>, to: PanelId, dir: Option<Dir>, after: Option<T> },
    /// Settles `panel` after a drop took a tab out of it. After a move (no
    /// `dir`) it selects its first tab not hidden, and closes as
    /// [`Edit::Close`] does when there is none. After a split it selects
    /// that tab only when it has no Selected View, and closes only when it
    /// has no tabs. A named panel, and any with `may_close` false, stays.
    Settle { panel: PanelId, dir: Option<Dir>, may_close: bool },
    /// Makes `tab`, one of `panel`'s, its Selected View; none selects none.
    Select { panel: PanelId, tab: Option<T> },
    /// Moves `panel` among its siblings to after `after` (none: first),
    /// with its weight.
    Reorder { panel: PanelId, after: Option<PanelId> },
    /// Moves the boundary after `panel` (between it and the next sibling
    /// with space, see [`boundary_partner`]) by `delta` of its parent,
    /// keeping both sides at least `floor` where they can be. Siblings whose
    /// weights drifted from summing to one are rescaled first.
    Resize { panel: PanelId, delta: f32, floor: f32 },
    /// Gives the two sides of the boundary after `panel` equal shares of
    /// their space, leaving panels between them at zero, rescaling
    /// drifted siblings as resizing does.
    Equalize { panel: PanelId },
    /// Every tab of `from`, a Selected View and a removed tab included,
    /// becomes `to`: its View moved to another owner, or its key changed.
    Readdress { from: T, to: T },
}

impl<T> Edit<T> {
    /// Its name, for diagnostics.
    pub fn name(&self) -> &'static str {
        match self {
            Edit::Clear => "clear",
            Edit::Discard => "discard",
            Edit::Split { .. } => "split",
            Edit::Add { .. } => "add",
            Edit::Remove { .. } => "remove",
            Edit::Close { .. } => "close",
            Edit::InsertTab { .. } => "insert-tab",
            Edit::DetachTab { .. } => "detach-tab",
            Edit::RemoveTab { .. } => "remove-tab",
            Edit::Drop { .. } => "drop",
            Edit::Settle { .. } => "settle",
            Edit::Select { .. } => "select",
            Edit::Reorder { .. } => "reorder",
            Edit::Resize { .. } => "resize",
            Edit::Equalize { .. } => "equalize",
            Edit::Readdress { .. } => "readdress",
        }
    }
}

/// What an edit did.
#[derive(Clone, Debug, PartialEq)]
pub struct Outcome<T> {
    /// Whether it was applied. A refused edit changed nothing, except a
    /// drop whose split was made before its tab was refused.
    pub applied: bool,
    /// The panel made (split, add, clear) or landed in (drop).
    pub panel: Option<PanelId>,
    /// The panel a drop's tab left, when this arrangement had it.
    pub source: Option<PanelId>,
    /// The leaf that takes focus after a close, a drop's or a settle's
    /// included, if the closed panel had it.
    pub heir: Option<PanelId>,
    /// Tabs this edit removed, whose Views go when the arrangement is set
    /// (and are now at the end of [`Arrangement::removed`]).
    pub removed: Vec<T>,
}

impl<T> Default for Outcome<T> {
    fn default() -> Self {
        Self { applied: false, panel: None, source: None, heir: None, removed: vec![] }
    }
}

fn id(raw: u64) -> Option<PanelId> {
    (raw != 0).then_some(PanelId(raw))
}

fn raw(panel: Option<PanelId>) -> u64 {
    panel.map_or(0, |p| p.0)
}

/// `Max(a, b)` as the C wrote it: `a` unless `b` is greater, so NaN wins
/// over `a`.
fn c_max(a: f32, b: f32) -> f32 {
    if a > b {
        a
    } else {
        b
    }
}

/// A panel's place: child indices from the root.
type Path = Vec<usize>;

impl<T: Clone + PartialEq> Arrangement<T> {
    pub fn new() -> Self {
        Self::default()
    }

    /// Every panel in tree order (pre-order), with its depth.
    pub fn panels(&self) -> impl Iterator<Item = &Panel<T>> {
        let mut stack: Vec<&Panel<T>> = self.root.iter().collect();
        std::iter::from_fn(move || {
            let panel = stack.pop()?;
            stack.extend(panel.children.iter().rev());
            Some(panel)
        })
    }

    /// The first panel in tree order with `id`.
    pub fn panel(&self, id: PanelId) -> Option<&Panel<T>> {
        self.path(id.0).map(|path| self.at(&path))
    }

    /// The first panel in tree order with `tab`.
    pub fn panel_with(&self, tab: &T) -> Option<&Panel<T>> {
        self.path_of_tab(tab).map(|path| self.at(&path))
    }

    /// The axis `panel`'s children divide along: the root's, alternating by
    /// depth.
    pub fn split_axis(&self, id: PanelId) -> Option<Axis> {
        self.path(id.0).map(|path| self.axis_at(path.len()))
    }

    pub fn panel_mut(&mut self, id: PanelId) -> Option<&mut Panel<T>> {
        let path = self.path(id.0)?;
        Some(self.at_mut(&path))
    }

    fn axis_at(&self, depth: usize) -> Axis {
        (0..depth).fold(self.root_axis, |axis, _| flip(axis))
    }

    fn alloc(&mut self) -> Panel<T> {
        let panel = Panel::new(PanelId(self.next_id));
        self.next_id = self.next_id.wrapping_add(1);
        panel
    }

    fn at(&self, path: &[usize]) -> &Panel<T> {
        path.iter().fold(self.root.as_ref().expect("a path is under the root"), |panel, &i| &panel.children[i])
    }

    fn at_mut(&mut self, path: &[usize]) -> &mut Panel<T> {
        path.iter().fold(self.root.as_mut().expect("a path is under the root"), |panel, &i| &mut panel.children[i])
    }

    /// The first path in tree order whose panel matches. Every walk here is
    /// a loop, not a recursion: a hand-edited document may nest deeply
    /// (#206).
    fn find(&self, matches: impl Fn(&Panel<T>) -> bool) -> Option<Path> {
        let root = self.root.as_ref()?;
        // The panels along the path, each with the index of its next child.
        let mut path: Path = vec![];
        let mut stack: Vec<(&Panel<T>, usize)> = vec![(root, 0)];
        if matches(root) {
            return Some(path);
        }
        while let Some((panel, next)) = stack.last_mut() {
            if let Some(child) = panel.children.get(*next) {
                path.push(*next);
                *next += 1;
                if matches(child) {
                    return Some(path);
                }
                stack.push((child, 0));
            } else {
                stack.pop();
                path.pop();
            }
        }
        None
    }

    fn path(&self, id: u64) -> Option<Path> {
        if id == 0 {
            return None;
        }
        self.find(|p| p.id.0 == id)
    }

    fn path_of_tab(&self, tab: &T) -> Option<Path> {
        self.find(|p| p.tabs.contains(tab))
    }

    /// Takes the panel at `path` out of the tree.
    fn unlink(&mut self, path: &[usize]) -> Panel<T> {
        match path.split_last() {
            None => self.root.take().expect("the root is there"),
            Some((&i, parent)) => self.at_mut(parent).children.remove(i),
        }
    }

    /// Removes the tabs of the panel at `path` and those under it, in tree
    /// order, onto `removed`.
    fn remove_tabs(&mut self, path: &[usize], removed: &mut Vec<T>) {
        if self.root.is_none() {
            return;
        }
        let mut stack = vec![self.at_mut(path)];
        while let Some(panel) = stack.pop() {
            removed.append(&mut panel.tabs);
            panel.selected = None;
            stack.extend(panel.children.iter_mut().rev());
        }
    }

    fn clear(&mut self, removed: &mut Vec<T>) -> u64 {
        self.remove_tabs(&[], removed);
        let mut root = self.alloc();
        root.weight = 1.0;
        let id = root.id.0;
        self.root = Some(root);
        id
    }

    fn split(&mut self, id: u64, dir: Dir) -> u64 {
        let Some(path) = self.path(id) else { return 0 };
        let axis = dir.axis();
        let mut created = self.alloc();
        let created_id = created.id.0;
        match path.split_last() {
            Some((&i, parent_path)) if self.axis_at(parent_path.len()) == axis => {
                let parent = self.at_mut(parent_path);
                let count = parent.children.len();
                let scale = count as f32 / (count + 1) as f32;
                created.weight = quantize(1.0 / (count + 1) as f32);
                for child in &mut parent.children {
                    child.weight = quantize(child.weight * scale);
                }
                parent.children.insert(if dir.after() { i + 1 } else { i }, created);
            }
            _ => {
                // The new split takes the panel's place and weight; a level
                // deeper, the axes alternate to the split's.
                let mut split = self.alloc();
                if path.is_empty() {
                    self.root_axis = axis;
                }
                let slot = self.at_mut(&path);
                split.weight = slot.weight;
                let mut panel = std::mem::replace(slot, split);
                panel.weight = 0.5;
                created.weight = 0.5;
                slot.children = if dir.after() { vec![panel, created] } else { vec![created, panel] };
            }
        }
        created_id
    }

    fn add(&mut self, parent_id: u64, weight: f32) -> u64 {
        let path = match self.path(parent_id) {
            Some(path) => path,
            None => {
                if parent_id != 0 || self.root.is_some() {
                    return 0;
                }
                let mut root = self.alloc();
                root.weight = 1.0;
                self.root = Some(root);
                vec![]
            }
        };
        let parent = self.at(&path);
        if parent.children.is_empty() && !parent.tabs.is_empty() {
            let mut lifted = self.alloc();
            let parent = self.at_mut(&path);
            lifted.weight = 1.0;
            lifted.tabs = std::mem::take(&mut parent.tabs);
            lifted.selected = parent.selected.take();
            lifted.options_from = Some(parent.id);
            parent.children.insert(0, lifted);
        }
        let mut created = self.alloc();
        let created_id = created.id.0;
        let parent = self.at_mut(&path);
        if weight > 0.0 {
            created.weight = quantize(weight);
        } else {
            // Malformed or zero weights still keep a share, so none becomes
            // unreachable.
            let total = parent.children.iter().fold(0.0f32, |total, child| total + c_max(0.01, child.weight));
            let fraction = 1.0 / (parent.children.len() + 1) as f32;
            for child in &mut parent.children {
                child.weight = quantize((1.0 - fraction) * c_max(0.01, child.weight) / total);
            }
            created.weight = quantize(fraction);
        }
        parent.children.push(created);
        created_id
    }

    fn remove(&mut self, id: u64, removed: &mut Vec<T>) -> bool {
        let Some(mut path) = self.path(id) else { return false };
        if path.is_empty() {
            return false;
        }
        self.remove_tabs(&path, removed);
        loop {
            self.unlink(&path);
            path.pop();
            let parent = self.at(&path);
            if path.is_empty() || !parent.children.is_empty() || !parent.tabs.is_empty() {
                return true;
            }
        }
    }

    /// Closes the panel at `id`: (applied, heir).
    fn close(&mut self, id: u64, removed: &mut Vec<T>) -> (bool, u64) {
        let Some(path) = self.path(id) else { return (false, 0) };
        let Some((&i, parent_path)) = path.split_last() else { return (false, 0) };
        self.remove_tabs(&path, removed);
        let parent = self.at(parent_path);
        if parent.children.len() == 2 {
            let heir = parent.children[1 - i].first_leaf().id.0;
            self.unlink(&path);
            let parent_weight = self.at(parent_path).weight;
            match parent_path.split_last() {
                None => {
                    // The survivor becomes the root, keeping the axis it had.
                    self.root_axis = flip(self.root_axis);
                    let mut keep = self.at_mut(&[]).children.remove(0);
                    keep.weight = 1.0;
                    self.root = Some(keep);
                }
                Some((&pi, grandparent)) => {
                    let mut keep = self.at_mut(parent_path).children.remove(0);
                    if keep.children.is_empty() {
                        keep.weight = quantize(parent_weight);
                        self.at_mut(grandparent).children[pi] = keep;
                    } else {
                        // A surviving split runs along its grandparent's
                        // axis, so its panels join the grandparent in its
                        // place, sharing its weight.
                        let grandparent = self.at_mut(grandparent);
                        grandparent.children.remove(pi);
                        for (k, mut child) in std::mem::take(&mut keep.children).into_iter().enumerate() {
                            child.weight = quantize(child.weight * parent_weight);
                            grandparent.children.insert(pi + k, child);
                        }
                    }
                }
            }
            (true, heir)
        } else {
            let heir = if i > 0 { parent.children.get(i - 1) } else { parent.children.get(i + 1) };
            let heir = heir.map_or(0, |h| h.first_leaf().id.0);
            let gone = self.unlink(&path).weight;
            let parent = self.at_mut(parent_path);
            let count = parent.children.len();
            for child in &mut parent.children {
                // A hand-edited panel may hold its whole parent despite
                // siblings: the survivors then share equally.
                child.weight = quantize(if gone < 1.0 { child.weight / (1.0 - gone) } else { 1.0 / count.max(1) as f32 });
            }
            (true, heir)
        }
    }

    /// Takes `tab` out of the first panel that has it.
    fn unlink_tab(&mut self, tab: &T) -> bool {
        let Some(path) = self.path_of_tab(tab) else { return false };
        let panel = self.at_mut(&path);
        let i = panel.tabs.iter().position(|t| t == tab).expect("the panel has the tab");
        panel.tabs.remove(i);
        if panel.selected.as_ref() == Some(tab) {
            panel.selected = None;
        }
        true
    }

    fn insert_tab(&mut self, tab: &T, to: u64, after: Option<&T>, select: bool) -> bool {
        if self.path(to).is_none() || after == Some(tab) {
            return false;
        }
        self.unlink_tab(tab);
        let path = self.path(to).expect("unlinking a tab keeps the panels");
        let panel = self.at_mut(&path);
        let at = after.and_then(|after| panel.tabs.iter().position(|t| t == after)).map_or(0, |i| i + 1);
        panel.tabs.insert(at, tab.clone());
        if select {
            panel.selected = Some(tab.clone());
        }
        true
    }

    /// Settles a panel a drop took a tab from: (changed, heir).
    fn settle(&mut self, id: u64, dir: Option<Dir>, hidden: &dyn Fn(&T) -> bool, may_close: bool, removed: &mut Vec<T>) -> (bool, u64) {
        let Some(path) = self.path(id) else { return (false, 0) };
        let panel = self.at(&path);
        let first_shown = panel.tabs.iter().find(|tab| !hidden(tab)).cloned();
        let split = dir.is_some();
        if may_close && (if split { panel.tabs.is_empty() } else { first_shown.is_none() }) && panel.closes_when_emptied() {
            // Closing the root does nothing.
            let (_, heir) = self.close(id, removed);
            return (heir != 0, heir);
        }
        let Some(first_shown) = first_shown else { return (false, 0) };
        if (split && panel.selected.is_some()) || panel.selected.as_ref() == Some(&first_shown) {
            return (false, 0);
        }
        self.at_mut(&path).selected = Some(first_shown);
        (true, 0)
    }

    fn reorder(&mut self, id: u64, after: u64) -> bool {
        let Some(path) = self.path(id) else { return false };
        let Some((&i, parent_path)) = path.split_last() else { return false };
        if after == id {
            return false;
        }
        let after_index = if after == 0 {
            None
        } else {
            match self.path(after) {
                Some(prev) if prev.split_last().map(|(_, p)| p) == Some(parent_path) => prev.last().copied(),
                _ => return false,
            }
        };
        let parent = self.at_mut(parent_path);
        let panel = parent.children.remove(i);
        let at = after_index.map_or(0, |a| if a > i { a } else { a + 1 });
        parent.children.insert(at, panel);
        true
    }

    fn resize(&mut self, id: u64, delta: f32, floor: f32) -> bool {
        let Some(path) = self.path(id) else { return false };
        let Some((&i, parent_path)) = path.split_last() else { return false };
        let parent = self.at_mut(parent_path);
        let Some(j) = boundary_partner(&children_weights(&parent.children), i) else { return false };
        let (mut min, mut max) = (parent.children[i].weight, parent.children[j].weight);
        let total = parent.children.iter().fold(0.0f32, |total, child| total + c_max(0.0, child.weight));
        if total > 0.0 && (total - 1.0).abs() > 0.0001 {
            min /= total;
            max /= total;
            for child in &mut parent.children {
                child.weight = quantize(c_max(0.0, child.weight) / total);
            }
        }
        let mut delta = delta;
        if min + max >= 2.0 * floor {
            // C's Clamp: below the low bound, the low bound; else above the
            // high one, the high one.
            let (low, high) = (floor - min, max - floor);
            delta = if delta < low {
                low
            } else if delta > high {
                high
            } else {
                delta
            };
        }
        parent.children[i].weight = quantize(min + delta);
        parent.children[j].weight = quantize(max - delta);
        true
    }

    fn equalize(&mut self, id: u64) -> bool {
        let applied = self.resize(id, 0.0, 0.0);
        let Some(path) = self.path(id) else { return false };
        let weight = self.at(&path).weight;
        let next = path.split_last().and_then(|(&i, parent)| {
            let children = &self.at(parent).children;
            boundary_partner(&children_weights(children), i).map(|j| children[j].weight)
        });
        let next = next.unwrap_or(0.0);
        self.resize(id, (next - weight) / 2.0, 0.0) || applied
    }

    fn readdress(&mut self, from: &T, to: &T) -> bool {
        if from == to {
            return false;
        }
        let mut found = false;
        let mut stack: Vec<&mut Panel<T>> = self.root.iter_mut().collect();
        while let Some(panel) = stack.pop() {
            for tab in &mut panel.tabs {
                if tab == from {
                    *tab = to.clone();
                    found = true;
                }
            }
            if panel.selected.as_ref() == Some(from) {
                panel.selected = Some(to.clone());
            }
            stack.extend(panel.children.iter_mut().rev());
        }
        for tab in &mut self.removed {
            if tab == from {
                *tab = to.clone();
            }
        }
        found
    }
}

/// Applies `edit` to `a`. `hidden` says which tabs a settle passes over
/// (the native shell's: Views the current project doesn't show).
pub fn apply<T: Clone + PartialEq>(a: &mut Arrangement<T>, edit: &Edit<T>, hidden: &dyn Fn(&T) -> bool) -> Outcome<T> {
    let mut out = Outcome::default();
    let mut removed = vec![];
    match edit {
        Edit::Clear => {
            out.panel = id(a.clear(&mut removed));
            out.applied = true;
        }
        Edit::Discard => {
            a.remove_tabs(&[], &mut removed);
            a.root = None;
            out.applied = true;
        }
        Edit::Split { panel, dir } => {
            out.panel = id(a.split(panel.0, *dir));
            out.applied = out.panel.is_some();
        }
        Edit::Add { parent, weight } => {
            out.panel = id(a.add(raw(*parent), *weight));
            out.applied = out.panel.is_some();
        }
        Edit::Remove { panel } => out.applied = a.remove(panel.0, &mut removed),
        Edit::Close { panel } => {
            let (applied, heir) = a.close(panel.0, &mut removed);
            (out.applied, out.heir) = (applied, id(heir));
        }
        Edit::InsertTab { tab, to, after, select } => out.applied = a.insert_tab(tab, to.0, after.as_ref(), *select),
        Edit::DetachTab { tab } => out.applied = a.unlink_tab(tab),
        Edit::RemoveTab { tab } => {
            out.applied = a.unlink_tab(tab);
            if out.applied {
                removed.push(tab.clone());
            }
        }
        Edit::Drop { tab, to, dir, after } => {
            let source = tab.as_ref().and_then(|tab| a.panel_with(tab)).map_or(0, |p| p.id.0);
            let target = match dir {
                None => to.0,
                Some(dir) => a.split(to.0, *dir),
            };
            match tab {
                None => {
                    out.panel = id(target);
                    out.applied = out.panel.is_some();
                }
                Some(tab) if a.insert_tab(tab, target, after.as_ref(), true) => {
                    out.applied = true;
                    out.panel = id(target);
                    out.source = id(source);
                    if dir.is_some() || source != to.0 {
                        let (_, heir) = a.settle(source, *dir, hidden, source != to.0, &mut removed);
                        out.heir = id(heir);
                    }
                }
                Some(_) => {}
            }
        }
        Edit::Settle { panel, dir, may_close } => {
            let (applied, heir) = a.settle(panel.0, *dir, hidden, *may_close, &mut removed);
            (out.applied, out.heir) = (applied, id(heir));
        }
        Edit::Select { panel, tab } => {
            if let Some(path) = a.path(panel.0) {
                let p = a.at_mut(&path);
                if tab.as_ref().is_none_or(|tab| p.tabs.contains(tab)) {
                    p.selected = tab.clone();
                    out.applied = true;
                }
            }
        }
        Edit::Reorder { panel, after } => out.applied = a.reorder(panel.0, raw(*after)),
        Edit::Resize { panel, delta, floor } => out.applied = a.resize(panel.0, *delta, *floor),
        Edit::Equalize { panel } => out.applied = a.equalize(panel.0),
        Edit::Readdress { from, to } => out.applied = a.readdress(from, to),
    }
    a.removed.extend(removed.iter().cloned());
    out.removed = removed;
    out
}

/// Drops `tab` into `to` in `a` as [`Edit::Drop`] does, where the panel it
/// left, `source`, is in another arrangement (the sidebar's, for a
/// workspace's drop): the same edit takes the tab out of that one and
/// settles the panel there, reselecting it, and closing it when the drop
/// left it no tabs (#358). The second outcome is the source arrangement's.
pub fn drop_across<T: Clone + PartialEq>(
    a: &mut Arrangement<T>,
    source: &mut Arrangement<T>,
    tab: &T,
    to: PanelId,
    dir: Option<Dir>,
    after: Option<T>,
    hidden: &dyn Fn(&T) -> bool,
) -> (Outcome<T>, Outcome<T>) {
    let left = source.panel_with(tab).map(|p| p.id);
    let out = apply(a, &Edit::Drop { tab: Some(tab.clone()), to, dir, after }, hidden);
    let mut settled = Outcome::default();
    if out.applied && out.source.is_none() {
        if let Some(left) = left {
            let detached = apply(source, &Edit::DetachTab { tab: tab.clone() }, hidden).applied;
            let empty = source.panel(left).is_some_and(|p| p.tabs.is_empty());
            settled = apply(source, &Edit::Settle { panel: left, dir, may_close: empty }, hidden);
            settled.applied |= detached;
            settled.source = Some(left);
        }
    }
    (out, settled)
}

/// Whose arrangement: a Workspace's, or a window's sidebar dock. A Panel ID
/// is unique only within its owner's arrangement.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum OwnerKind {
    /// A Workspace's panels (a window's own panels are a Workspace too).
    Workspace,
    /// The sidebar's dock, as one window shows it.
    Sidebar,
}

/// An arrangement's owner: its kind and ID (a Workspace ID; for the
/// sidebar, its window's Window ID).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Owner {
    pub kind: OwnerKind,
    pub id: crate::WorkspaceId,
}

/// A change to an arrangement as a frontend's commands and gestures ask
/// for it, and as the Automation Endpoint (ADR 0014) names it. Each is one
/// or two edits of `owner`'s arrangement; a frontend applies it to its live
/// copy ([`apply_intent`]) and sets the result.
#[derive(Clone, Debug, PartialEq)]
pub enum Intent<T> {
    /// Selects `tab`, one of `panel`'s (none selects none).
    SelectTab { owner: Owner, panel: PanelId, tab: Option<T> },
    /// Moves `tab` into `to` after `after` (none: first), selected there.
    MoveTab { owner: Owner, tab: T, to: PanelId, after: Option<T> },
    /// Removes `tab`, and closes its panel when that leaves it empty and
    /// unnamed; reports the heir.
    CloseTab { owner: Owner, tab: T },
    /// A tab drag's drop: into `to`, or into the panel a split of `to`
    /// towards `dir` makes. The panel the tab left is settled in the same
    /// edit, in the arrangement it is in.
    DropTab { owner: Owner, tab: T, to: PanelId, dir: Option<Dir>, after: Option<T> },
    /// Splits `panel` towards `dir`: a new empty panel there.
    SplitPanel { owner: Owner, panel: PanelId, dir: Dir },
    /// Closes `panel` and its tabs; reports the heir.
    ClosePanel { owner: Owner, panel: PanelId },
    /// Moves `panel` among its siblings to after `after` (none: first).
    ReorderPanel { owner: Owner, panel: PanelId, after: Option<PanelId> },
    /// A boundary drag, once, at release: the boundary after `panel` moves
    /// by `delta` of its parent, keeping both sides `floor` where they can.
    ResizePanel { owner: Owner, panel: PanelId, delta: f32, floor: f32 },
    /// A boundary double-click: both sides of the boundary after `panel`
    /// become equal.
    EqualizePanel { owner: Owner, panel: PanelId },
}

impl<T> Intent<T> {
    /// Its stable name, as the Automation Endpoint invokes it.
    pub fn name(&self) -> &'static str {
        match self {
            Intent::SelectTab { .. } => "select-tab",
            Intent::MoveTab { .. } => "move-tab",
            Intent::CloseTab { .. } => "close-tab",
            Intent::DropTab { .. } => "drop-tab",
            Intent::SplitPanel { .. } => "split-panel",
            Intent::ClosePanel { .. } => "close-panel",
            Intent::ReorderPanel { .. } => "reorder-panel",
            Intent::ResizePanel { .. } => "resize-panel",
            Intent::EqualizePanel { .. } => "equalize-panel",
        }
    }

    /// The arrangement it edits.
    pub fn owner(&self) -> Owner {
        match self {
            Intent::SelectTab { owner, .. }
            | Intent::MoveTab { owner, .. }
            | Intent::CloseTab { owner, .. }
            | Intent::DropTab { owner, .. }
            | Intent::SplitPanel { owner, .. }
            | Intent::ClosePanel { owner, .. }
            | Intent::ReorderPanel { owner, .. }
            | Intent::ResizePanel { owner, .. }
            | Intent::EqualizePanel { owner, .. } => *owner,
        }
    }
}

/// What an intent did: to its owner's arrangement, and to the one a dropped
/// tab came from when that is another's.
#[derive(Clone, Debug, PartialEq)]
pub struct IntentOutcome<T> {
    pub outcome: Outcome<T>,
    pub source: Option<Outcome<T>>,
}

/// Applies `intent` to `a`, its owner's arrangement. For a drop whose tab
/// is in another arrangement (the sidebar's, into a workspace), `source` is
/// that arrangement, which the same intent settles.
pub fn apply_intent<T: Clone + PartialEq>(
    a: &mut Arrangement<T>,
    intent: &Intent<T>,
    source: Option<&mut Arrangement<T>>,
    hidden: &dyn Fn(&T) -> bool,
) -> IntentOutcome<T> {
    let edit = match intent {
        Intent::SelectTab { panel, tab, .. } => Edit::Select { panel: *panel, tab: tab.clone() },
        Intent::MoveTab { tab, to, after, .. } => Edit::InsertTab { tab: tab.clone(), to: *to, after: after.clone(), select: true },
        Intent::CloseTab { tab, .. } => {
            let panel = a.panel_with(tab).map(|p| p.id);
            let mut out = apply(a, &Edit::RemoveTab { tab: tab.clone() }, hidden);
            // A panel left with no tabs goes, and its siblings grow into its
            // space (#358); a root panel, or one a layout planned, stays.
            if let Some(panel) = panel.filter(|_| out.applied) {
                if a.panel(panel).is_some_and(|p| p.tabs.is_empty() && p.closes_when_emptied()) {
                    let closed = apply(a, &Edit::Close { panel }, hidden);
                    out.heir = closed.heir;
                    out.removed.extend(closed.removed);
                }
            }
            return IntentOutcome { outcome: out, source: None };
        }
        Intent::DropTab { tab, to, dir, after, .. } => {
            if let Some(source) = source {
                let (outcome, settled) = drop_across(a, source, tab, *to, *dir, after.clone(), hidden);
                return IntentOutcome { outcome, source: Some(settled) };
            }
            Edit::Drop { tab: Some(tab.clone()), to: *to, dir: *dir, after: after.clone() }
        }
        Intent::SplitPanel { panel, dir, .. } => Edit::Split { panel: *panel, dir: *dir },
        Intent::ClosePanel { panel, .. } => Edit::Close { panel: *panel },
        Intent::ReorderPanel { panel, after, .. } => Edit::Reorder { panel: *panel, after: *after },
        Intent::ResizePanel { panel, delta, floor, .. } => Edit::Resize { panel: *panel, delta: *delta, floor: *floor },
        Intent::EqualizePanel { panel, .. } => Edit::Equalize { panel: *panel },
    };
    IntentOutcome { outcome: apply(a, &edit, hidden), source: None }
}

/// The first broken invariant, or none. Edits keep a valid arrangement
/// valid.
pub fn problem<T: Clone + PartialEq>(a: &Arrangement<T>) -> Option<String> {
    let root = a.root.as_ref()?;
    if root.weight != 1.0 {
        return Some("the root panel has a parent or a weight other than 1".into());
    }
    let panels: Vec<&Panel<T>> = a.panels().collect();
    for (index, p) in panels.iter().enumerate() {
        let id = p.id.0;
        if id == 0 || id >= a.next_id {
            return Some(format!("panel {id} has an ID that is not allocated"));
        }
        if panels[..index].iter().any(|q| q.id == p.id) {
            return Some(format!("two panels have ID {id}"));
        }
        let mut total = 0.0f32;
        for c in &p.children {
            if c.weight.is_nan() || c.weight < 0.0 {
                return Some(format!("panel {} has a negative weight", c.id.0));
            }
            total += c.weight;
        }
        let children = p.children.len();
        if children == 1 {
            return Some(format!("panel {id} splits into one panel"));
        }
        if children != 0 && (total - 1.0).abs() > 0.00001 * children as f32 {
            return Some(format!("panel {id}'s children weigh {}, not 1", weight::format(total as f64)));
        }
        if children != 0 && !p.tabs.is_empty() {
            return Some(format!("split panel {id} has tabs"));
        }
        for tab in &p.tabs {
            let copies: usize = panels.iter().map(|q| q.tabs.iter().filter(|t| *t == tab).count()).sum();
            if copies > 1 {
                return Some(format!("panel {id}'s View has two tabs"));
            }
        }
        if p.selected.as_ref().is_some_and(|s| !p.tabs.contains(s)) {
            return Some(format!("panel {id} selects a View it does not have"));
        }
    }
    None
}
