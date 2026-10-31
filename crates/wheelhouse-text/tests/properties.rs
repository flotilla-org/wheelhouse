//! Properties of a layout over random text, styles and widths, with the
//! bundled fonts only.
mod common;
use std::sync::LazyLock;

use hegel::generators as gs;
use hegel::TestCase;
use wheelhouse_text::{
    caret_rect, offset_at, selection_rects, Affinity, Cluster, Family, Feature, InlineBox, Input, Paragraph, Style, StyleRun, TextContext,
    CLUSTER_ELLIPSIS, CLUSTER_LIGATURE_CONTINUATION, CLUSTER_RTL,
};

static CTX: LazyLock<TextContext> = LazyLock::new(common::context);

/// Pieces random text is made of: every script the snapshots cover, and
/// the characters that make clusters interesting.
const PIECES: &[&str] = &[
    "a",
    "word",
    "Wide",
    "AV",
    "To",
    "ffi",
    " ",
    " ",
    "  ",
    "\n",
    "\t",
    ".",
    "-",
    "e\u{301}",
    "a\u{30a}\u{323}",
    "\u{1F468}\u{200D}\u{1F469}\u{200D}\u{1F467}",
    "\u{1F1EB}\u{1F1F7}",
    "\u{1F44D}\u{1F3FD}",
    "\u{5E9}\u{5DC}\u{5D5}\u{5DD}",
    "\u{645}\u{631}\u{62D}\u{628}\u{627}",
    "\u{6F22}\u{5B57}",
    "\u{3002}",
    "\u{E2A}\u{E27}\u{E31}\u{E2A}\u{E14}\u{E35}",
    "\u{200B}",
    "\u{A0}",
    "\u{2192}",
    "\u{221E}",
    "123",
    "\u{FEFF}",
];

const LATIN: &[&str] = &["a", "word", "Wide", "AV", "To", " ", ".", "-", "123"];

#[derive(Debug)]
struct Case {
    text: String,
    styles: Vec<(Family, f32, f32, bool, f32, bool)>,
    runs: Vec<StyleRun>,
    boxes: Vec<InlineBox>,
    width: f32,
    max_lines: usize,
    ellipsis: bool,
}

const NO_LIGATURES: [Feature; 1] = [Feature { tag: *b"liga", value: 0 }];

impl Case {
    fn draw(tc: &TestCase, pieces: &'static [&'static str]) -> Self {
        let mut text = String::new();
        if pieces.len() > LATIN.len() && tc.draw(gs::weighted_booleans(0.2)) {
            text = tc.draw(gs::text().max_size(30));
        } else {
            for piece in tc.draw(gs::vecs(gs::sampled_from(pieces)).max_size(40)) {
                text.push_str(piece);
            }
        }
        let style_count = tc.draw(gs::integers::<usize>().min_value(1).max_value(3));
        let styles = (0..style_count)
            .map(|_| {
                (
                    tc.draw(gs::sampled_from(&[Family::Main, Family::Code, Family::Icons][..])),
                    tc.draw(gs::sampled_from(&[6.0, 11.0, 16.0, 23.5, 40.0][..])),
                    tc.draw(gs::sampled_from(&[400.0, 600.0, 800.0][..])),
                    tc.draw(gs::booleans()),
                    tc.draw(gs::sampled_from(&[0.0, 0.0, 1.5][..])),
                    tc.draw(gs::weighted_booleans(0.2)),
                )
            })
            .collect::<Vec<_>>();
        let boundaries = text.char_indices().map(|(i, _)| i).chain([text.len()]).collect::<Vec<_>>();
        let mut cuts = tc.draw(gs::vecs(gs::sampled_from(&boundaries[..])).max_size(6));
        cuts.sort();
        cuts.dedup();
        let mut runs = vec![];
        let mut at = 0;
        for cut in cuts {
            if cut > at {
                runs.push(StyleRun { start: at, len: cut - at, style: tc.draw(gs::integers::<usize>().max_value(style_count - 1)) });
                at = cut;
            }
        }
        let mut offsets = tc.draw(gs::vecs(gs::sampled_from(&boundaries[..])).max_size(3));
        offsets.sort();
        let boxes = offsets
            .into_iter()
            .enumerate()
            .map(|(i, offset)| {
                let height = tc.draw(gs::sampled_from(&[4.0, 16.0, 30.0][..]));
                InlineBox { id: i as u64, offset, width: tc.draw(gs::sampled_from(&[0.0, 12.0, 90.0][..])), height, baseline: height }
            })
            .collect();
        Case {
            text,
            styles,
            runs,
            boxes,
            width: tc.draw(gs::sampled_from(&[0.0, 1.0, 13.0, 40.0, 75.5, 160.0, 400.0, 2000.0][..])),
            max_lines: tc.draw(gs::sampled_from(&[0, 0, 1, 2, 3][..])),
            ellipsis: tc.draw(gs::booleans()),
        }
    }

    fn layout(&self) -> Paragraph {
        let styles = self
            .styles
            .iter()
            .map(|&(family, size, weight, italic, letter_spacing, no_ligatures)| Style {
                family,
                size,
                weight,
                italic,
                features: if no_ligatures { &NO_LIGATURES } else { &[] },
                letter_spacing,
            })
            .collect::<Vec<_>>();
        let input = Input {
            text: &self.text,
            runs: &self.runs,
            styles: &styles,
            boxes: &self.boxes,
            max_lines: self.max_lines,
            ellipsis: self.ellipsis,
        };
        CTX.layout(&input, self.width).unwrap()
    }
}

/// Where the laid-out text ends: the text's end, or where it was cut.
fn laid_out_end(p: &Paragraph) -> usize {
    p.lines.last().map_or(0, |l| l.text_end as usize)
}

#[hegel::test(test_cases = 300)]
fn clusters_cover_the_text_once_in_order(tc: TestCase) {
    let case = Case::draw(&tc, PIECES);
    let p = case.layout();
    assert!(!p.lines.is_empty());
    let end = laid_out_end(&p);
    let limited = case.max_lines > 0 && p.lines.len() == case.max_lines;
    if !limited {
        assert_eq!(end, case.text.len(), "the lines hold the whole text");
    }
    let mut at = 0;
    for (i, line) in p.lines.iter().enumerate() {
        assert_eq!(line.text_start as usize, at, "line {i} starts where the one before ends");
        let mut clusters = p.clusters[line.cluster_start as usize..(line.cluster_start + line.cluster_count) as usize]
            .iter()
            .filter(|c| c.flags & CLUSTER_ELLIPSIS == 0)
            .collect::<Vec<_>>();
        assert!(clusters.iter().all(|c| c.line as usize == i));
        clusters.sort_by_key(|c| c.start);
        for c in clusters {
            assert_eq!(c.start as usize, at, "line {i}: a cluster starts where the one before ends");
            assert!(c.len > 0);
            assert!(case.text.is_char_boundary((c.start + c.len) as usize));
            at += c.len as usize;
        }
        assert_eq!(at, line.text_end as usize, "line {i}: its clusters end where it does");
    }
    let ellipses = p.clusters.iter().filter(|c| c.flags & CLUSTER_ELLIPSIS != 0).count();
    // Text cut ends with an ellipsis; so may inline boxes cut.
    if end < case.text.len() {
        assert_eq!(ellipses, case.ellipsis as usize, "{case:?} {:?}", p.lines);
    } else {
        assert!(ellipses <= (limited && case.ellipsis) as usize);
    }
}

/// Each position at a cluster edge, both ways.
fn positions(p: &Paragraph) -> Vec<(usize, Affinity)> {
    let mut offsets = p.clusters.iter().flat_map(|c| [c.start as usize, (c.start + c.len) as usize]).collect::<Vec<_>>();
    offsets.extend(p.lines.iter().map(|l| l.text_start as usize));
    offsets.sort();
    offsets.dedup();
    offsets.into_iter().flat_map(|o| [(o, Affinity::Downstream), (o, Affinity::Upstream)]).collect()
}

#[hegel::test(test_cases = 300)]
fn a_caret_hit_tests_back_to_itself(tc: TestCase) {
    let case = Case::draw(&tc, PIECES);
    let p = case.layout();
    let view = p.view();
    for (offset, affinity) in positions(&p) {
        let caret = caret_rect(&view, offset, affinity);
        let hit = offset_at(&view, caret.x, caret.y + caret.height / 2.0);
        let back = caret_rect(&view, hit.offset, hit.affinity);
        assert_eq!(back.line, caret.line, "{offset} {affinity:?} hit {hit:?}");
        assert!(
            (back.x - caret.x).abs() < 0.01,
            "{offset} {affinity:?}: caret at {} hit {hit:?} at {} in {case:?}: {:?} {:?}",
            caret.x,
            back.x,
            p.lines,
            p.clusters
        );
    }
}

#[hegel::test(test_cases = 200)]
fn in_left_to_right_text_a_caret_hit_tests_to_its_offset(tc: TestCase) {
    let case = Case::draw(&tc, LATIN);
    let p = case.layout();
    let view = p.view();
    for (offset, affinity) in positions(&p) {
        let caret = caret_rect(&view, offset, affinity);
        let hit = offset_at(&view, caret.x, caret.y + caret.height / 2.0);
        assert_eq!(hit.offset, offset, "{affinity:?} caret at {caret:?}");
    }
}

#[hegel::test(test_cases = 300)]
fn selection_rects_lie_within_their_lines(tc: TestCase) {
    let case = Case::draw(&tc, PIECES);
    let p = case.layout();
    let end = case.text.len() + 1;
    let (a, b) = (tc.draw(gs::integers::<usize>().max_value(end)), tc.draw(gs::integers::<usize>().max_value(end)));
    let view = p.view();
    let rects = selection_rects(&view, a.min(b), a.max(b));
    if a != b && p.clusters.iter().any(|c| c.len > 0 && (a.min(b)..a.max(b)).contains(&(c.start as usize))) {
        assert!(!rects.is_empty());
    }
    for r in rects {
        let line = &p.lines[r.line as usize];
        let clusters = &p.clusters[line.cluster_start as usize..(line.cluster_start + line.cluster_count) as usize];
        let left = clusters.iter().map(|c| c.x).fold(f32::INFINITY, f32::min);
        let right = clusters.iter().map(|c| c.x + c.advance).fold(f32::NEG_INFINITY, f32::max);
        assert!(r.y == line.y && r.height == line.height, "{r:?} {line:?}");
        assert!(r.x >= left - 0.01 && r.x + r.width <= right + 0.01 && r.width >= 0.0, "{r:?} outside {left}..{right}");
    }
}

/// A line's content in logical order, as units that can't break (a
/// ligature's clusters make one) and whether each is whitespace that hangs
/// at the line's end.
fn units(text: &str, clusters: &[Cluster]) -> Vec<(f32, bool)> {
    let blank =
        |c: &Cluster| c.len > 0 && text[c.start as usize..(c.start + c.len) as usize].chars().all(|ch| matches!(ch, ' ' | '\n' | '\t'));
    let mut logical = clusters.iter().collect::<Vec<_>>();
    logical.sort_by_key(|c| (c.start, c.len == 0));
    let mut units: Vec<(f32, bool)> = vec![];
    for c in logical {
        match units.last_mut() {
            Some(unit) if c.flags & CLUSTER_LIGATURE_CONTINUATION != 0 => unit.0 += c.advance,
            _ => units.push((c.advance, blank(c))),
        }
    }
    units
}

// What a line holds, but whitespace at its logical end, fits the width; or
// it is one unit wider than the width. (parley doesn't move an RTL
// paragraph's trailing whitespace to the line's end, so a line's width
// can count whitespace that doesn't hang.)
#[hegel::test(test_cases = 300)]
fn lines_fit_the_width_unless_one_cluster_is_wider(tc: TestCase) {
    let case = Case::draw(&tc, PIECES);
    let p = case.layout();
    for (i, line) in p.lines.iter().enumerate() {
        let clusters = &p.clusters[line.cluster_start as usize..(line.cluster_start + line.cluster_count) as usize];
        let mut units = units(&case.text, clusters);
        while units.last().is_some_and(|u| u.1) {
            units.pop();
        }
        let boxes = p.boxes.iter().filter(|b| b.line as usize == i).map(|b| b.width);
        let content = units.iter().map(|u| u.0).chain(boxes.clone()).sum::<f32>();
        if content <= case.width + 0.01 {
            continue;
        }
        let inked = units.iter().map(|u| u.0).chain(boxes).filter(|&w| w > 0.0).collect::<Vec<_>>();
        assert!(
            inked.len() == 1 && inked[0] > case.width,
            "line {i} holds {content} in {}: {inked:?} {clusters:?} {:?}",
            case.width,
            case.text
        );
    }
}

#[hegel::test(test_cases = 200)]
fn layout_is_the_same_every_time(tc: TestCase) {
    let case = Case::draw(&tc, PIECES);
    let other = Case::draw(&tc, PIECES);
    let first = case.layout();
    other.layout();
    assert_eq!(case.layout(), first);
}

#[test]
fn rtl_flags_follow_the_text() {
    let styles = [Style::default()];
    let p = CTX.layout(&Input::plain("ab \u{5E9}\u{5DC}", &styles), 500.0).unwrap();
    let rtl = p.clusters.iter().map(|c| c.flags & CLUSTER_RTL != 0).collect::<Vec<_>>();
    assert_eq!(rtl, [false, false, false, true, true]);
}
