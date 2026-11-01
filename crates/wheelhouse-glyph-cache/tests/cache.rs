//! The glyph cache's contract (#560): a key is compared whole, a hit never
//! rasterizes again, a page used this frame is never cleared, and every glyph
//! handed out this frame still holds its own pixels in its page when the
//! frame ends.
#![cfg(glyph_cache)]
use hegel::generators as gs;
use hegel::TestCase;
use wheelhouse_glyph_cache::{pattern, pen_split, zoom_bucket, Cache, Config, Glyph, Request, COLOR, EMBOLDEN, MASK};

fn config(dim: i32, budget: u32) -> Config {
    Config { mask_dim: dim, mask_budget: budget, color_dim: dim, color_budget: budget, gamma: 1.0, collide: false }
}

fn glyph(id: u32, w: i32, h: i32) -> Request {
    Request { tag: [7, 9], glyph_id: id, size_px: 16.0, kind: MASK, width: w, height: h, ..Request::default() }
}

/// Whether the glyph's region holds exactly its pattern (through `lut`),
/// framed by a clear pixel on every side.
fn holds(cache: &Cache, r: &Request, g: &Glyph, lut: &dyn Fn(u8) -> u8) -> bool {
    let Some((pixels, dim, kind)) = cache.page(g.page) else { return false };
    let bpp = if kind == COLOR { 4 } else { 1 };
    let at = |x: i32, y: i32, c: i32| pixels[((y * dim + x) * bpp + c) as usize];
    for y in g.y0 - 1..g.y1 + 1 {
        for x in g.x0 - 1..g.x1 + 1 {
            let inside = x >= g.x0 && x < g.x1 && y >= g.y0 && y < g.y1;
            let want = if inside { lut(pattern(r.glyph_id, r.size_px, x - g.x0, y - g.y0)) } else { 0 };
            for c in 0..bpp {
                if at(x, y, c) != want {
                    return false;
                }
            }
        }
    }
    true
}

fn same(v: u8) -> u8 {
    v
}

#[test]
fn a_second_request_hits_and_a_changed_field_misses() {
    // With every hash alike too, so that the fields alone tell keys apart.
    for collide in [false, true] {
        changed_fields_miss(Cache::new(&Config { collide, ..config(256, 2) }));
    }
}

fn changed_fields_miss(mut cache: Cache) {
    let base = glyph(3, 10, 12);
    let first = cache.get(&base);
    assert!(first.ok == 1 && first.rasterized == 1);
    assert_eq!(cache.get(&base), Glyph { rasterized: 0, ..first });
    // Every field of the key tells keys apart.
    let variants = [
        Request { tag: [8, 9], ..base.clone() },
        Request { face_index: 1, ..base.clone() },
        Request { glyph_id: 4, ..base.clone() },
        Request { size_px: 16.0 + 1.0 / 64.0, ..base.clone() },
        Request { subpixel_x: 0.25, ..base.clone() },
        Request { coords: vec![8192], ..base.clone() },
        Request { synthesis: EMBOLDEN, ..base.clone() },
        Request { skew: 14.0, ..base.clone() },
        Request { hinting: 1, ..base.clone() },
    ];
    for v in &variants {
        assert_eq!(cache.get(v).rasterized, 1, "{v:?} should miss");
    }
    for v in &variants {
        assert_eq!(cache.get(v).rasterized, 0, "{v:?} should hit");
    }
    // Sizes within the same 1/64 px, and fractions within the same quarter, share a raster.
    assert_eq!(cache.get(&Request { size_px: 16.0 + 0.2 / 64.0, ..base.clone() }).rasterized, 0);
    assert_eq!(cache.get(&Request { subpixel_x: 0.1, ..base.clone() }).rasterized, 0);
    assert_eq!(cache.stats().misses, 1 + variants.len() as u64);
}

#[test]
fn keys_whose_hashes_collide_stay_distinct() {
    let mut cache = Cache::new(&Config { collide: true, ..config(256, 1) });
    let (a, b) = (glyph(1, 8, 8), glyph(2, 9, 7));
    let ga = cache.get(&a);
    let gb = cache.get(&b);
    assert_eq!(gb.rasterized, 1, "b is not a's hit");
    assert_ne!((ga.x0, ga.y0), (gb.x0, gb.y0));
    assert_eq!(cache.get(&a), Glyph { rasterized: 0, ..ga });
    assert_eq!(cache.get(&b), Glyph { rasterized: 0, ..gb });
    assert!(holds(&cache, &a, &ga, &same) && holds(&cache, &b, &gb, &same));
}

#[test]
fn a_full_atlas_clears_its_least_recently_used_page_but_never_one_used_this_frame() {
    // Each page holds one 30x30 glyph (32x32 with its border); two pages.
    let mut cache = Cache::new(&config(32, 2));
    let (a, b, c) = (glyph(1, 30, 30), glyph(2, 30, 30), glyph(3, 30, 30));
    let ga = cache.get(&a);
    cache.frame();
    let gb = cache.get(&b);
    assert!(ga.ok == 1 && gb.ok == 1 && ga.page != gb.page);
    // Both pages are now used this frame: c can't go anywhere.
    assert_eq!(cache.get(&a).rasterized, 0);
    assert_eq!(cache.get(&c).ok, 0);
    assert!(holds(&cache, &a, &ga, &same) && holds(&cache, &b, &gb, &same));
    // Next frame, with b used again, a's page is the one cleared.
    cache.frame();
    assert_eq!(cache.get(&b).rasterized, 0);
    let gc = cache.get(&c);
    assert_eq!((gc.ok, gc.page), (1, ga.page));
    assert_eq!(cache.stats().evictions, 1);
    // a was dropped with its page: it rasterizes again.
    cache.frame();
    assert_eq!(cache.get(&a).rasterized, 1);
}

#[test]
fn colour_glyphs_get_their_own_pages_and_skip_gamma() {
    let mut cache = Cache::new(&Config { gamma: 2.0, ..config(64, 2) });
    let mask = glyph(1, 6, 5);
    let color = Request { kind: COLOR, ..glyph(2, 6, 5) };
    let (gm, gc) = (cache.get(&mask), cache.get(&color));
    assert_eq!((gm.kind, gc.kind), (MASK, COLOR));
    assert_ne!(gm.page, gc.page);
    let lut = |v: u8| (255.0 * (v as f32 / 255.0).powf(0.5)).round() as u8;
    assert!(holds(&cache, &mask, &gm, &lut), "masks go through the gamma curve");
    assert!(holds(&cache, &color, &gc, &same), "colour is written as given");
    // A new curve drops the old masks; colour stays.
    cache.set_gamma(1.0);
    assert_eq!(cache.get(&mask).rasterized, 1);
    assert_eq!(cache.get(&color).rasterized, 0);
}

#[test]
fn a_glyph_bigger_than_a_page_fails_once_and_is_remembered() {
    let mut cache = Cache::new(&config(32, 1));
    let big = glyph(1, 31, 4);
    assert_eq!(cache.get(&big).ok, 0);
    assert_eq!(cache.get(&big).rasterized, 0);
    // An empty glyph (a space) is fine and takes no page.
    let space = glyph(2, 0, 0);
    let g = cache.get(&space);
    assert_eq!((g.ok, g.page), (1, 0));
    assert_eq!(cache.page_count(MASK), 0);
}

// Sizes land on 2^(k/4), the nearest such size.
#[hegel::test(test_cases = 200)]
fn zoom_buckets_are_quarter_octaves(tc: TestCase) {
    let size = tc.draw(gs::floats::<f32>().min_value(0.5).max_value(512.0));
    let bucket = zoom_bucket(size);
    let k = (bucket.log2() * 4.0).round();
    assert!((bucket - 2f32.powf(k / 4.0)).abs() <= bucket * 1e-5);
    let ratio = (size / bucket).log2().abs();
    assert!(ratio <= 0.125 + 1e-4, "{size} -> {bucket}");
}

// The pen splits into a whole pixel and a quarter that add back to within an eighth.
#[hegel::test(test_cases = 200)]
fn the_pen_splits_into_a_pixel_and_a_quarter(tc: TestCase) {
    let x = tc.draw(gs::floats::<f32>().min_value(-4096.0).max_value(4096.0));
    let (whole, sub) = pen_split(x);
    assert!((0.0..1.0).contains(&sub) && (sub * 4.0).fract() == 0.0, "{x} -> {sub}");
    assert!((whole as f32 + sub - x).abs() <= 0.125 + 1e-3, "{x} -> {whole} + {sub}");
}

/// A random workload: frames of requests over a small universe of keys
/// (repeats hit), of both kinds and sizes up to a whole page, against small
/// pages and budgets so that atlases fill and evict often. After every
/// request: what came back is in bounds, the page budget holds, and every
/// glyph handed out this frame (this one included) is still intact and
/// overlaps no other.
#[hegel::test(test_cases = 300)]
fn packing_and_eviction_keep_this_frames_glyphs(tc: TestCase) {
    let dim = tc.draw(gs::sampled_from(vec![32, 48, 64]));
    let budget = tc.draw(gs::integers::<u32>().min_value(1).max_value(3));
    let mut cache = Cache::new(&config(dim, budget));
    let universe = tc.draw(gs::integers::<u32>().min_value(1).max_value(24));
    let frames = tc.draw(gs::integers::<usize>().min_value(1).max_value(8));
    for _ in 0..frames {
        let mut live: Vec<(Request, Glyph)> = vec![];
        for _ in 0..tc.draw(gs::integers::<usize>().min_value(0).max_value(16)) {
            let id = tc.draw(gs::integers::<u32>().min_value(0).max_value(universe - 1));
            // Size and kind follow the id, as a real glyph's do.
            let r = Request {
                kind: if id.is_multiple_of(5) { COLOR } else { MASK },
                ..glyph(id, (id * 7 % (dim as u32 - 1)) as i32, (id * 3 % 20) as i32 + 1)
            };
            let g = cache.get(&r);
            for kind in [MASK, COLOR] {
                assert!(cache.page_count(kind) <= budget);
            }
            if g.ok == 0 || g.page == 0 {
                continue;
            }
            assert!(g.x0 >= 1 && g.y0 >= 1 && g.x1 < dim && g.y1 < dim, "{g:?} outside a {dim} page");
            assert_eq!((g.x1 - g.x0, g.y1 - g.y0), (r.width, r.height));
            live.push((r, g));
            for (i, (ra, ga)) in live.iter().enumerate() {
                assert!(holds(&cache, ra, ga, &same), "{ra:?} lost its pixels this frame");
                for (_, gb) in &live[i + 1..] {
                    // Each with its clear border.
                    let (a, b) = ((ga.x0 - 1, ga.y0 - 1, ga.x1 + 1, ga.y1 + 1), (gb.x0 - 1, gb.y0 - 1, gb.x1 + 1, gb.y1 + 1));
                    let apart = ga.page != gb.page || a == b || a.2 <= b.0 || b.2 <= a.0 || a.3 <= b.1 || b.3 <= a.1;
                    assert!(apart, "{ga:?} overlaps {gb:?}");
                }
            }
        }
        cache.frame();
    }
}
