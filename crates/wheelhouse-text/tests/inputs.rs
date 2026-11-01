mod common;
use std::{path::Path, sync::Arc, thread};
use wheelhouse_text::{Error, Family, FontSource, InlineBox, Input, Style, StyleRun, TextContext};

#[test]
fn non_finite_numbers_are_refused() {
    let ctx = common::context();
    let text = "a box";
    for (style, error) in [
        (Style { size: f32::NAN, ..Style::default() }, Error::BadSize(0)),
        (Style { size: 0.0, ..Style::default() }, Error::BadSize(0)),
        (Style { weight: f32::NAN, ..Style::default() }, Error::BadStyle(0)),
        (Style { weight: 0.0, ..Style::default() }, Error::BadStyle(0)),
        (Style { letter_spacing: f32::INFINITY, ..Style::default() }, Error::BadStyle(0)),
    ] {
        let styles = [style];
        assert_eq!(ctx.layout(&Input::plain(text, &styles), 100.0), Err(error));
    }
    let styles = [Style::default()];
    let good = InlineBox { id: 1, offset: 2, width: 10.0, height: 10.0, baseline: 10.0 };
    for b in [
        InlineBox { width: f32::NAN, ..good },
        InlineBox { height: f32::INFINITY, ..good },
        InlineBox { baseline: f32::NAN, ..good },
        InlineBox { width: -1.0, ..good },
    ] {
        let boxes = [b];
        assert_eq!(ctx.layout(&Input { boxes: &boxes, ..Input::plain(text, &styles) }, 100.0), Err(Error::BadBox(0)));
    }
    assert_eq!(ctx.layout(&Input::plain(text, &styles), f32::NAN), Err(Error::BadWidth));
    let runs = [StyleRun { start: 0, len: 9, style: 0 }];
    assert_eq!(ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 100.0), Err(Error::RunOutOfBounds(0)));
    let boxes = [good];
    assert!(ctx.layout(&Input { boxes: &boxes, ..Input::plain(text, &styles) }, 100.0).is_ok());
    let many = vec![Style::default(); 65537];
    assert_eq!(ctx.layout(&Input::plain(text, &many), 100.0), Err(Error::TooManyStyles));
}

// By hand, as CI machines have few fonts: `cargo test -p wheelhouse-text -- --ignored`.
#[test]
#[ignore]
fn system_fonts_resolve_to_their_files() {
    let ctx = TextContext::new(true);
    let styles = [Style::default(), Style { family: Family::Code, ..Style::default() }];
    let text = "Latin \u{5E9}\u{5DC}\u{5D5}\u{5DD} \u{645}\u{631}\u{62D}\u{628}\u{627} \u{6F22}\u{5B57} \u{E2A}\u{E27}\u{E31}\u{E2A}\u{E14}\u{E35} \u{1F600} code";
    let runs = [StyleRun { start: text.len() - 4, len: 4, style: 1 }];
    let p = ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 400.0).unwrap();
    for run in &p.runs {
        let face = ctx.font(run.font).unwrap();
        println!("{:?} {} glyphs", face.source, run.glyph_count);
        assert!(!face.data.as_ref().is_empty());
        if let FontSource::Path(path) = &face.source {
            assert!(Path::new(path.as_ref()).is_file(), "{path}");
        }
    }
}

// Threads lay out with one context while another changes its families and
// finds its fonts again: each layout is whole, and one made at a settled
// epoch matches a layout made alone.
#[test]
fn threads_share_one_context() {
    let ctx = Arc::new(common::context());
    let styles = [Style::default(), Style { family: Family::Code, ..Style::default() }];
    let text = "Concurrent \u{5E9}\u{5DC}\u{5D5}\u{5DD} layout \u{1F600} with `code` in it";
    let runs = [StyleRun { start: 39, len: 6, style: 1 }];
    let alone = ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 120.0).unwrap();
    let workers = (0..4)
        .map(|_| {
            let ctx = ctx.clone();
            thread::spawn(move || {
                let styles = [Style::default(), Style { family: Family::Code, ..Style::default() }];
                for _ in 0..200 {
                    let p = ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 120.0).unwrap();
                    assert_eq!(p.lines.last().unwrap().text_end as usize, text.len());
                    assert!(p.runs.iter().all(|r| ctx.font(r.font).is_some()));
                }
            })
        })
        .collect::<Vec<_>>();
    for i in 0..10 {
        ctx.set_family(Family::Code, if i % 2 == 0 { "JetBrainsMono Nerd Font" } else { "Atkinson Hyperlegible Next" });
        ctx.fonts_changed();
    }
    ctx.set_family(Family::Code, "JetBrainsMono Nerd Font");
    for worker in workers {
        worker.join().unwrap();
    }
    let settled = ctx.layout(&Input { runs: &runs, ..Input::plain(text, &styles) }, 120.0).unwrap();
    assert_eq!((settled.lines.clone(), settled.clusters.clone()), (alone.lines.clone(), alone.clusters.clone()));
    assert!(settled.epoch > alone.epoch);
    let fresh = TextContext::new(false);
    assert_eq!(fresh.epoch(), 0);
}
