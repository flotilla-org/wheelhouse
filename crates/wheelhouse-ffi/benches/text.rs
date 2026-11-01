//! wheelhouse-text's resize sweep and streaming tail through wh_text_layout,
//! with the input's conversion and the copy into an arena the host owns.
#[path = "../../wheelhouse-text/benches/common/mod.rs"]
mod common;
use std::{ffi::c_void, mem::MaybeUninit, ptr, time::Instant};

use common::{document, font, ms, summary, Lcg, Para, FONTS, WORDS};
use wheelhouse_ffi::*;

struct Arena {
    buffer: Vec<u8>,
    used: usize,
}

unsafe extern "C" fn arena_alloc(user: *mut c_void, size: usize, align: usize) -> *mut c_void {
    // SAFETY: user is the Arena the benchmark passes.
    let arena = unsafe { &mut *user.cast::<Arena>() };
    let base = arena.buffer.as_mut_ptr() as usize;
    let at = (base + arena.used).next_multiple_of(align) - base;
    if at + size > arena.buffer.len() {
        return ptr::null_mut();
    }
    arena.used = at + size;
    arena.buffer[at..].as_mut_ptr().cast()
}

fn text(s: &str) -> Text {
    Text { data: s.as_ptr(), len: s.len() }
}

fn main() {
    let ctx = wh_text_create(0);
    for (id, name) in FONTS.iter().enumerate() {
        let bytes = font(name);
        // SAFETY: ctx is live and bytes outlives the call.
        unsafe { wh_text_register_font(ctx, bytes.as_ptr(), bytes.len(), id as u64) };
    }
    let style = |family, size, weight| WhTextStyle {
        family,
        size,
        weight,
        italic: 0,
        features: ptr::null(),
        feature_count: 0,
        letter_spacing: 0.0,
    };
    let styles =
        [style(WH_TEXT_FAMILY_MAIN, 16.0, 400.0), style(WH_TEXT_FAMILY_CODE, 14.0, 400.0), style(WH_TEXT_FAMILY_MAIN, 16.0, 600.0)];
    let mut arena = Arena { buffer: vec![0; 64 << 20], used: 0 };
    let layout = |p: &Para, width: f32, arena: &mut Arena| -> f32 {
        let runs = p
            .runs
            .iter()
            .map(|&(start, len, style)| WhTextStyleRun { start: start as u32, len: len as u32, style: style as u32 })
            .collect::<Vec<_>>();
        let input = WhTextInput {
            text: text(&p.text),
            runs: runs.as_ptr(),
            run_count: runs.len(),
            styles: styles.as_ptr(),
            style_count: styles.len(),
            boxes: ptr::null(),
            box_count: 0,
            max_lines: 0,
            ellipsis: 0,
            align: WH_TEXT_ALIGN_START,
            tab_size: 0,
        };
        arena.used = 0;
        let alloc = WhTextAllocator { alloc: Some(arena_alloc), user: ptr::from_mut(arena).cast() };
        let mut out = MaybeUninit::<WhTextLayout>::uninit();
        let mut error = ptr::null_mut();
        // SAFETY: every pointer is live for the call.
        let ok = unsafe { wh_text_layout(ctx, &input, width, alloc, out.as_mut_ptr(), &mut error) };
        assert_eq!(ok, 1);
        // SAFETY: wh_text_layout returned 1, so it wrote out.
        unsafe { out.assume_init() }.height
    };
    let (doc, _) = document();
    for p in doc.iter().take(200) {
        layout(p, 800.0, &mut arena);
    }

    let mut tail = Para { text: String::new(), runs: vec![] };
    let mut times = vec![];
    let mut words = Lcg(44);
    while tail.text.len() < 8000 {
        if !tail.text.is_empty() {
            tail.text.push(' ');
        }
        tail.text.push_str(WORDS[words.next(WORDS.len())]);
        let started = Instant::now();
        layout(&tail, 800.0, &mut arena);
        times.push(started.elapsed());
    }
    println!("streaming tail through the FFI: {} appends to {} bytes at 800 px", times.len(), tail.text.len());
    println!("  {}, last {:.3} ms", summary(&times), ms(*times.last().unwrap()));

    let mut times = vec![];
    for width in (300..=1600).step_by(10) {
        let started = Instant::now();
        let mut height = 0.0;
        for p in &doc {
            height += layout(p, width as f32, &mut arena);
            if height >= 1200.0 {
                break;
            }
        }
        times.push(started.elapsed());
    }
    println!("resize sweep through the FFI: {} widths 300..1600 px, the paragraphs filling 1200 px", times.len());
    println!("  a width: {} (a 120 Hz frame is 8.3 ms)", summary(&times));
    // SAFETY: ctx came from wh_text_create.
    unsafe { wh_text_free(ctx) };
}
