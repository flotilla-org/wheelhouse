//! DirectWrite and CoreText take variations in user units, layout gives them
//! normalized and after avar; native undoes avar and fvar's scaling
//! (font_provider.c). Whatever fvar and avar say, a user value normalized and
//! mapped through avar as the font's own rules do comes back as itself.
#![cfg(glyph_cache)]
use hegel::generators as gs;
use hegel::TestCase;
use wheelhouse_glyph_cache::user_axis_values;

struct Axis {
    min: f32,
    default: f32,
    max: f32,
    /// avar's (from, to) pairs, both increasing, through (-1,-1), (0,0) and (1,1).
    map: Vec<(f32, f32)>,
}

fn f2dot14(v: f32) -> i16 {
    (v * 16384.0).round() as i16
}

fn draw_axis(tc: &TestCase) -> Axis {
    // Whole user units on either side of a default, as fvar ranges are.
    let default = tc.draw(gs::integers::<i32>().min_value(-500).max_value(500)) as f32;
    let min = default - tc.draw(gs::integers::<i32>().min_value(1).max_value(500)) as f32;
    let max = default + tc.draw(gs::integers::<i32>().min_value(1).max_value(500)) as f32;
    // Extra points on each side of 0, each segment at least 1/8 wide both ways
    // so the maps stay invertible within F2Dot14's precision.
    let mut map = vec![(-1.0, -1.0), (0.0, 0.0), (1.0, 1.0)];
    for side in [-1.0f32, 1.0] {
        if tc.draw(gs::booleans()) {
            let from = tc.draw(gs::integers::<i32>().min_value(1).max_value(7)) as f32 / 8.0;
            let to = tc.draw(gs::integers::<i32>().min_value(1).max_value(7)) as f32 / 8.0;
            map.push((side * from, side * to));
        }
    }
    map.sort_by(|a, b| a.0.total_cmp(&b.0));
    Axis { min, default, max, map }
}

fn avar_forward(map: &[(f32, f32)], v: f32) -> f32 {
    for w in map.windows(2) {
        let ((f0, t0), (f1, t1)) = (w[0], w[1]);
        if v <= f1 {
            return t0 + (v - f0) * (t1 - t0) / (f1 - f0);
        }
    }
    v
}

fn fvar(axes: &[Axis]) -> Vec<u8> {
    let mut b = vec![];
    for v in [1u16, 0, 16, 2, axes.len() as u16, 20, 0, 4 + 4 * axes.len() as u16] {
        b.extend(v.to_be_bytes());
    }
    for (i, a) in axes.iter().enumerate() {
        b.extend([b'a', b'x', b's', b'0' + i as u8]);
        for v in [a.min, a.default, a.max] {
            b.extend(((v * 65536.0) as i32).to_be_bytes());
        }
        b.extend([0, 0, 1, 0]);
    }
    b
}

fn avar(axes: &[Axis]) -> Vec<u8> {
    let mut b = vec![];
    for v in [1u16, 0, 0, axes.len() as u16] {
        b.extend(v.to_be_bytes());
    }
    for a in axes {
        b.extend((a.map.len() as u16).to_be_bytes());
        for &(from, to) in &a.map {
            b.extend(f2dot14(from).to_be_bytes());
            b.extend(f2dot14(to).to_be_bytes());
        }
    }
    b
}

#[hegel::test(test_cases = 300)]
fn normalized_coords_come_back_to_user_values(tc: TestCase) {
    let axes = (0..tc.draw(gs::integers::<usize>().min_value(1).max_value(3))).map(|_| draw_axis(&tc)).collect::<Vec<_>>();
    let with_avar = tc.draw(gs::booleans());
    let users =
        axes.iter().map(|a| tc.draw(gs::integers::<i32>().min_value(a.min as i32).max_value(a.max as i32)) as f32).collect::<Vec<_>>();
    let coords = axes
        .iter()
        .zip(&users)
        .map(|(a, &u)| {
            let v = if u < a.default { (u - a.default) / (a.default - a.min) } else { (u - a.default) / (a.max - a.default) };
            f2dot14(if with_avar { avar_forward(&a.map, v) } else { v })
        })
        .collect::<Vec<_>>();
    let avar = if with_avar { avar(&axes) } else { vec![] };
    let got = user_axis_values(&fvar(&axes), &avar, &coords);
    assert_eq!(got.len(), axes.len());
    for ((a, &want), &got) in axes.iter().zip(&users).zip(&got) {
        // F2Dot14 rounding, through a segment as much as 8 times steeper.
        let tolerance = (a.max - a.min) * 8.0 / 16384.0 + 0.01;
        assert!((got - want).abs() <= tolerance, "{want} came back as {got}");
    }
}
