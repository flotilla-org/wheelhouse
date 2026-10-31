//! Line breaks and cluster positions with the bundled fonts only. Scripts
//! the bundled fonts don't cover (Arabic, Hebrew, CJK, Thai) shape to
//! notdef glyphs, so their snapshots check breaks and bidi order, not
//! glyph shapes.
mod common;
use common::{context, dump};
use insta::assert_snapshot;
use wheelhouse_text::{Family, Feature, InlineBox, Input, Style, StyleRun};

fn plain(text: &str, width: f32) -> String {
    let ctx = context();
    let styles = [Style::default()];
    let p = ctx.layout(&Input::plain(text, &styles), width).unwrap();
    dump(&ctx, text, &p)
}

#[test]
fn latin_kerning() {
    // "AV" and "To" kern: the same text with kerning turned off, below, is wider.
    let ctx = context();
    let off = [Feature { tag: *b"kern", value: 0 }];
    let styles = [Style::default(), Style { features: &off, ..Style::default() }];
    let text = "AV To\nAV To";
    let runs = [StyleRun { start: 6, len: 5, style: 1 }];
    let p = ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 1000.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn latin_wraps_at_spaces() {
    assert_snapshot!(plain("The quick brown fox jumps over the lazy dog.", 120.0));
}

#[test]
fn combining_marks_join_their_base() {
    // e + combining acute, a + combining ring + combining dot below.
    assert_snapshot!(plain("Cafe\u{301} a\u{30a}\u{323}x", 1000.0));
}

#[test]
fn emoji_zwj_sequence_is_one_cluster() {
    // Family: man, ZWJ, woman, ZWJ, girl; then a flag.
    assert_snapshot!(plain("a\u{1F468}\u{200D}\u{1F469}\u{200D}\u{1F467}b\u{1F1EB}\u{1F1F7}", 1000.0));
}

#[test]
fn bidi_arabic_and_hebrew_in_english() {
    assert_snapshot!(plain("one \u{5E9}\u{5DC}\u{5D5}\u{5DD} two \u{645}\u{631}\u{62D}\u{628}\u{627} three", 1000.0));
}

#[test]
fn bidi_wraps_in_logical_order() {
    assert_snapshot!(plain("one \u{5E9}\u{5DC}\u{5D5}\u{5DD} \u{5E2}\u{5D5}\u{5DC}\u{5DD} two", 70.0));
}

#[test]
fn rtl_paragraph_with_english() {
    assert_snapshot!(plain("\u{645}\u{631}\u{62D}\u{628}\u{627} hello \u{628}\u{643}", 1000.0));
}

#[test]
fn cjk_breaks_between_ideographs() {
    // No spaces: each ideograph is a break opportunity, but not before "。".
    assert_snapshot!(plain("\u{6F22}\u{5B57}\u{304B}\u{306A}\u{6F22}\u{5B57}\u{3002}\u{6587}\u{5B57}\u{5217}", 50.0));
}

#[test]
fn thai_breaks_between_words() {
    // No bundled font covers Thai, so this checks break positions only:
    // ICU4X's dictionary finds the words in "สวัสดีครับ ยินดีต้อนรับ".
    assert_snapshot!(plain("\u{E2A}\u{E27}\u{E31}\u{E2A}\u{E14}\u{E35}\u{E04}\u{E23}\u{E31}\u{E1A}\u{E22}\u{E34}\u{E19}\u{E14}\u{E35}\u{E15}\u{E49}\u{E2D}\u{E19}\u{E23}\u{E31}\u{E1A}", 60.0));
}

#[test]
fn inline_boxes_take_their_width_and_sit_on_the_baseline() {
    let ctx = context();
    let styles = [Style::default()];
    let text = "see  and  here";
    let boxes = [
        InlineBox { id: 7, offset: 4, width: 20.0, height: 12.0, baseline: 12.0 },
        InlineBox { id: 8, offset: 9, width: 30.0, height: 30.0, baseline: 24.0 },
    ];
    let p = ctx.layout(&Input { boxes: &boxes, ..Input::plain(text, &styles) }, 1000.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn inline_box_wraps_like_a_word() {
    let ctx = context();
    let styles = [Style::default()];
    let text = "before after";
    let boxes = [InlineBox { id: 1, offset: 7, width: 40.0, height: 10.0, baseline: 10.0 }];
    let p = ctx.layout(&Input { boxes: &boxes, ..Input::plain(text, &styles) }, 60.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn max_lines_ends_the_last_line_with_an_ellipsis() {
    let ctx = context();
    let styles = [Style::default()];
    let text = "The quick brown fox jumps over the lazy dog and keeps running.";
    let input = Input { max_lines: 2, ellipsis: true, ..Input::plain(text, &styles) };
    let p = ctx.layout(&input, 120.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn max_lines_one_is_a_single_cut_line() {
    let ctx = context();
    let styles = [Style::default()];
    let text = "Supercalifragilistic expialidocious";
    let input = Input { max_lines: 1, ellipsis: true, ..Input::plain(text, &styles) };
    let p = ctx.layout(&input, 100.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn max_lines_without_an_ellipsis_drops_the_rest() {
    let ctx = context();
    let styles = [Style::default()];
    let text = "one two three four";
    let input = Input { max_lines: 1, ..Input::plain(text, &styles) };
    let p = ctx.layout(&input, 60.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn text_that_fits_is_not_cut() {
    let ctx = context();
    let styles = [Style::default()];
    let input = Input { max_lines: 2, ellipsis: true, ..Input::plain("short", &styles) };
    let p = ctx.layout(&input, 120.0).unwrap();
    assert_snapshot!(dump(&ctx, "short", &p));
}

#[test]
fn styles_pick_family_weight_and_italic() {
    let ctx = context();
    let styles = [
        Style::default(),
        Style { weight: 600.0, ..Style::default() },
        Style { weight: 800.0, ..Style::default() },
        Style { italic: true, ..Style::default() },
        Style { family: Family::Code, size: 13.0, ..Style::default() },
        Style { family: Family::Code, size: 13.0, weight: 700.0, ..Style::default() },
    ];
    // The code font has one face, so its bold is synthesised.
    let text = "plain semi bold slant code() bold";
    let runs = [
        StyleRun { start: 6, len: 4, style: 1 },
        StyleRun { start: 11, len: 4, style: 2 },
        StyleRun { start: 16, len: 5, style: 3 },
        StyleRun { start: 22, len: 6, style: 4 },
        StyleRun { start: 29, len: 4, style: 5 },
    ];
    let p = ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 1000.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}

#[test]
fn hard_breaks_end_lines() {
    assert_snapshot!(plain("one\ntwo\n\nfour\n", 1000.0));
}

#[test]
fn an_rtl_cut_line_aligns_with_the_lines_above() {
    let ctx = context();
    let styles = [Style::default()];
    let text = "\u{5E9}\u{5DC}\u{5D5}\u{5DD} \u{5E2}\u{5D5}\u{5DC}\u{5DD} \u{5E9}\u{5DC}\u{5D5}\u{5DD} \u{5E2}\u{5D5}\u{5DC}\u{5DD}";
    let input = Input { max_lines: 2, ellipsis: true, ..Input::plain(text, &styles) };
    let p = ctx.layout(&input, 50.0).unwrap();
    assert_snapshot!(dump(&ctx, text, &p));
}
