use std::collections::HashMap;

use hegel::generators as gs;
use hegel::TestCase;

use super::generate::{self, Choose};
use super::heights::{Entry as Row, HeightIndex};
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

#[hegel::test(test_cases = 300)]
fn the_height_index_agrees_with_a_prefix_sum(tc: TestCase) {
    let mut index = HeightIndex::default();
    let mut naive: Vec<Row> = vec![];
    // Whole heights keep every sum exact.
    let height = |tc: &TestCase| tc.draw(gs::integers::<u32>().max_value(40)) as f32;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(40)) {
        let len = naive.len();
        match tc.draw(gs::integers::<u8>().max_value(7)) {
            0 | 1 => {
                let at = tc.draw(gs::integers::<usize>().max_value(len));
                let count = tc.draw(gs::integers::<usize>().min_value(1).max_value(if tc.draw(gs::booleans()) { 3 } else { 700 }));
                let rows: Vec<Row> = (0..count).map(|_| Row::estimated(height(&tc), 0)).collect();
                index.insert(at, &rows);
                naive.splice(at..at, rows);
            }
            2 if len > 0 => {
                let start = tc.draw(gs::integers::<usize>().max_value(len - 1));
                let end = tc.draw(gs::integers::<usize>().min_value(start).max_value(len.min(start + 600)));
                index.remove(start..end);
                naive.drain(start..end);
            }
            3 if len > 0 => {
                let at = tc.draw(gs::integers::<usize>().max_value(len - 1));
                let row = Row::measured(height(&tc), 1).with_hidden(naive[at].hidden());
                index.set(at, row);
                naive[at] = row;
            }
            4 if len > 0 => {
                let start = tc.draw(gs::integers::<usize>().max_value(len - 1));
                let end = tc.draw(gs::integers::<usize>().min_value(start).max_value(len.min(start + 300)));
                let hide = tc.draw(gs::booleans());
                for (at, row) in naive.iter_mut().enumerate().take(end).skip(start) {
                    *row = row.with_hidden(hide);
                    index.set(at, *row);
                }
            }
            7 => {
                let base = height(&tc);
                let remeasure = |i: usize, row: Row| Row::measured(base + (i % 3) as f32, 2).with_hidden(row.hidden());
                index.update_all(remeasure);
                for (i, row) in naive.iter_mut().enumerate() {
                    *row = remeasure(i, *row);
                }
            }
            _ => index.push({
                let row = Row::estimated(height(&tc), 0);
                naive.push(row);
                row
            }),
        }
        assert_eq!(index.len(), naive.len());
        let total: f64 = naive.iter().map(Row::size).sum();
        assert_eq!(index.total(), total);
        for _ in 0..4 {
            if naive.is_empty() {
                break;
            }
            let at = tc.draw(gs::integers::<usize>().max_value(naive.len()));
            let offset: f64 = naive[..at].iter().map(Row::size).sum();
            assert_eq!(index.offset_of(at), offset, "offset of entry {at}");
            if at < naive.len() {
                assert_eq!(index.get(at), naive[at]);
            }
            let probe = tc.draw(gs::integers::<u32>().max_value(total as u32 + 2)) as f64 + if tc.draw(gs::booleans()) { 0.5 } else { 0.0 };
            let mut expected = None;
            let mut sum = 0.0;
            for (i, row) in naive.iter().enumerate() {
                if row.size() > 0.0 && probe >= sum && probe < sum + row.size() {
                    expected = Some((i, probe - sum));
                    break;
                }
                sum += row.size();
            }
            assert_eq!(index.entry_at(probe), expected, "entry at {probe}");
        }
    }
}

#[hegel::test(test_cases = 200)]
fn the_anchor_holds_when_heights_above_it_change(tc: TestCase) {
    let mut c = Draws(&tc);
    let blocks = generate::document(&mut c, 30);
    let mut rich = RichText::open(&generate::render(&blocks));
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    view.set_metrics(doc, Metrics { width: 400.0, line_height: 10.0, chars_per_line: 40.0 });
    view.set_viewport(120.0);
    let blocks: Vec<usize> = (0..doc.entries().len()).filter(|&i| doc.entries()[i].part == Part::Block).collect();
    let entry = blocks[c.below(blocks.len())];
    let (key, part) = doc.entry_key(entry).unwrap();
    let text = doc.text(doc.get(key).unwrap()).len() as u32;
    let byte = c.below(text as usize + 1) as u32;
    let viewport_offset = c.below(120) as f64;
    let position = if doc.kind(doc.get(key).unwrap()) == Kind::Embed {
        Position::Fraction(c.below(10) as f32 / 10.0)
    } else {
        Position::Text { byte, line_offset: c.below(5) as f32 }
    };
    view.scroll_to(doc, Anchor { key, part, position, viewport_offset }).unwrap();
    // The anchored Block's top, from the viewport's top, and whether the
    // scroll position is pinned at either end.
    let on_screen = |doc: &Tree, view: &View| {
        let top = view.top(doc);
        let max = (view.heights().total() - view.viewport()).max(0.0);
        (view.heights().offset_of(entry) - top, top == 0.0 || top == max)
    };
    let (before, clamped) = on_screen(doc, view);
    for _ in 0..c.below(10) + 1 {
        if entry == 0 {
            break;
        }
        let above = c.below(entry);
        let (k, p) = doc.entry_key(above).unwrap();
        view.set_height(doc, k, p, c.below(200) as f32, 400.0).unwrap();
        let (after, clamped_now) = on_screen(doc, view);
        if !clamped && !clamped_now {
            assert!((after - before).abs() < 1e-6, "the anchor moved from {before} to {after}");
        }
    }
}

/// Collapse whitespace runs, and blank-line runs to one, for comparing
/// copies: a copy's edges may differ in whitespace that markdown drops.
pub(crate) fn normalize(text: &str) -> String {
    let mut out: Vec<String> = vec![];
    for line in text.lines() {
        let line = line.split_whitespace().collect::<Vec<_>>().join(" ");
        if line.is_empty() && out.last().is_none_or(|l| l.is_empty()) {
            continue;
        }
        out.push(line);
    }
    while out.last().is_some_and(|l| l.is_empty()) {
        out.pop();
    }
    out.join("\n")
}

#[hegel::test(test_cases = 500)]
fn copy_as_markdown_parses_to_the_plain_copy(tc: TestCase) {
    let mut c = Draws(&tc);
    let size = c.below(8) + 1;
    let blocks = generate::document(&mut c, size);
    let source = generate::render(&blocks);
    let mut rich = RichText::open(&source);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    let blocks: Vec<NodeId> = doc.entries().iter().filter(|e| e.part == Part::Block).map(|e| e.node).collect();
    if blocks.is_empty() {
        return;
    }
    let point = |c: &mut Draws| {
        let id = blocks[c.below(blocks.len())];
        let len = select::extent(doc, id) as usize;
        Point { key: doc.key(id), byte: c.below(len + 1) as u32 }
    };
    let anchor = point(&mut c);
    let focus = point(&mut c);
    view.select(doc, anchor, focus).unwrap();
    let Some(plain) = view.copy(doc, Format::Plain) else { return };
    let markdown = view.copy(doc, Format::Markdown).unwrap();
    let parsed = RichText::open(&markdown);
    assert_eq!(
        normalize(&rendered(parsed.doc())),
        normalize(&plain),
        "\nsource:\n{source}\nselection {anchor:?} to {focus:?}\nmarkdown:\n{markdown}\n"
    );
}

/// The text of HTML: tags dropped, entities decoded.
fn html_text(html: &str) -> String {
    let mut out = String::new();
    let mut rest = html;
    while let Some(at) = rest.find('<') {
        out.push_str(&rest[..at]);
        let end = rest[at..].find('>').expect("a tag ends") + at + 1;
        // Elements that end a line of text.
        if ["<br", "<p", "<h", "<li", "<tr", "<pre", "<hr", "<aside", "<blockquote", "<t"].iter().any(|t| rest[at..].starts_with(t)) {
            out.push(' ');
        }
        rest = &rest[end..];
    }
    out.push_str(rest);
    out.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&amp;", "&")
}

/// The plain copy's words, less the list markers, task boxes and footnote
/// labels it adds as text and HTML gives as markup.
fn plain_words(plain: &str) -> Vec<String> {
    let markers = regex::Regex::new(r"^ *((- |\d+\. |\[[ x]\] |\[\^[^\]]+\]: )+)").unwrap();
    plain.lines().map(|line| markers.replace(line, "").into_owned()).flat_map(|line| words(&line)).collect()
}

fn words(text: &str) -> Vec<String> {
    text.split(|c: char| c.is_whitespace() || c == '\t').filter(|w| !w.is_empty() && *w != "---").map(str::to_owned).collect()
}

#[hegel::test(test_cases = 300)]
fn html_copy_has_the_plain_copys_text(tc: TestCase) {
    let mut c = Draws(&tc);
    let size = c.below(8) + 1;
    let source = generate::render(&generate::document(&mut c, size));
    let mut rich = RichText::open(&source);
    let id = rich.subscribe();
    let (doc, view) = rich.view_mut(id).unwrap();
    let blocks: Vec<NodeId> = doc.entries().iter().filter(|e| e.part == Part::Block).map(|e| e.node).collect();
    if blocks.is_empty() {
        return;
    }
    let point = |c: &mut Draws| {
        let id = blocks[c.below(blocks.len())];
        Point { key: doc.key(id), byte: c.below(select::extent(doc, id) as usize + 1) as u32 }
    };
    let (anchor, focus) = (point(&mut c), point(&mut c));
    view.select(doc, anchor, focus).unwrap();
    let Some(plain) = view.copy(doc, Format::Plain) else { return };
    let html = view.copy(doc, Format::Html).unwrap();
    assert!(!html.contains("class=") && !html.contains("style="), "{html}");
    assert_eq!(words(&html_text(&html)), plain_words(&plain), "\nsource:\n{source}\nhtml:\n{html}\nplain:\n{plain}\n");
}
