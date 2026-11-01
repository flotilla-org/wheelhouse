//! Test-only: native's C glyph cache (native/src/font_cache/font_glyph_cache.c)
//! over CPU pages, through its shim (native/src/font_cache/tests/glyph_cache_shim.c).
//! Built by build.rs on Linux and macOS, where CI runs `cargo test`;
//! elsewhere this crate is empty.
#![cfg(glyph_cache)]
use std::{ffi::c_void, ptr, slice};

pub const MASK: u32 = 0;
pub const COLOR: u32 = 1;
pub const EMBOLDEN: u32 = 1;
pub const SUBPIXEL_BINS: u32 = 4;

#[repr(C)]
#[derive(Clone, Debug, Default)]
struct RawRequest {
    tag: [u64; 2],
    face_index: u32,
    glyph_id: u32,
    size_px: f32,
    subpixel_x: f32,
    coords: *const i16,
    coord_count: u64,
    synthesis: u32,
    skew: f32,
    hinting: u32,
    kind: u32,
    width: i32,
    height: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Glyph {
    pub ok: u32,
    pub kind: u32,
    pub page: u64,
    pub x0: i32,
    pub y0: i32,
    pub x1: i32,
    pub y1: i32,
    pub left: i32,
    pub top: i32,
    /// Whether this call rasterized it: a miss.
    pub rasterized: u32,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Request {
    pub tag: [u64; 2],
    pub face_index: u32,
    pub glyph_id: u32,
    pub size_px: f32,
    pub subpixel_x: f32,
    pub coords: Vec<i16>,
    pub synthesis: u32,
    pub skew: f32,
    pub hinting: u32,
    /// What the rasterizer gives on a miss.
    pub kind: u32,
    pub width: i32,
    pub height: i32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Stats {
    pub hits: u64,
    pub misses: u64,
    pub evictions: u64,
    pub failures: u64,
}

extern "C" {
    fn gc_new(mask_dim: i32, mask_budget: u32, color_dim: i32, color_budget: u32, gamma: f32, collide: u32) -> *mut c_void;
    fn gc_free(h: *mut c_void);
    fn gc_frame(h: *mut c_void);
    fn gc_set_gamma(h: *mut c_void, gamma: f32);
    fn gc_get(h: *mut c_void, r: *const RawRequest, out: *mut Glyph);
    fn gc_page(h: *mut c_void, page: u64, pixels: *mut *const u8, dim: *mut i32, kind: *mut u32) -> u64;
    fn gc_page_count(h: *mut c_void, kind: u32) -> u32;
    fn gc_stats(h: *mut c_void, out: *mut u64);
    fn gc_zoom_bucket(size: f32) -> f32;
    fn gc_pen_split(x: f32, whole: *mut i32, subpixel_x: *mut f32);
    fn gc_user_axis_values(
        fvar: *const u8,
        fvar_size: u64,
        avar: *const u8,
        avar_size: u64,
        coords: *const i16,
        count: u64,
        out: *mut f32,
    ) -> u64;
}

pub struct Config {
    pub mask_dim: i32,
    pub mask_budget: u32,
    pub color_dim: i32,
    pub color_budget: u32,
    pub gamma: f32,
    /// Every key hashes alike.
    pub collide: bool,
}

/// Not Send: the C keeps per-thread scratch arenas.
pub struct Cache(*mut c_void);

impl Cache {
    pub fn new(c: &Config) -> Self {
        Self(unsafe { gc_new(c.mask_dim, c.mask_budget, c.color_dim, c.color_budget, c.gamma, c.collide as u32) })
    }

    pub fn frame(&mut self) {
        unsafe { gc_frame(self.0) }
    }

    pub fn set_gamma(&mut self, gamma: f32) {
        unsafe { gc_set_gamma(self.0, gamma) }
    }

    pub fn get(&mut self, r: &Request) -> Glyph {
        let raw = RawRequest {
            tag: r.tag,
            face_index: r.face_index,
            glyph_id: r.glyph_id,
            size_px: r.size_px,
            subpixel_x: r.subpixel_x,
            coords: if r.coords.is_empty() { ptr::null() } else { r.coords.as_ptr() },
            coord_count: r.coords.len() as u64,
            synthesis: r.synthesis,
            skew: r.skew,
            hinting: r.hinting,
            kind: r.kind,
            width: r.width,
            height: r.height,
        };
        let mut out = Glyph::default();
        unsafe { gc_get(self.0, &raw, &mut out) };
        out
    }

    /// (pixels, width, kind).
    pub fn page(&self, page: u64) -> Option<(&[u8], i32, u32)> {
        let (mut pixels, mut dim, mut kind) = (ptr::null(), 0, 0);
        let len = unsafe { gc_page(self.0, page, &mut pixels, &mut dim, &mut kind) };
        (len > 0).then(|| (unsafe { slice::from_raw_parts(pixels, len as usize) }, dim, kind))
    }

    pub fn page_count(&self, kind: u32) -> u32 {
        unsafe { gc_page_count(self.0, kind) }
    }

    pub fn stats(&self) -> Stats {
        let mut out = [0u64; 4];
        unsafe { gc_stats(self.0, out.as_mut_ptr()) };
        Stats { hits: out[0], misses: out[1], evictions: out[2], failures: out[3] }
    }
}

impl Drop for Cache {
    fn drop(&mut self) {
        unsafe { gc_free(self.0) }
    }
}

/// The shim's rasterizer's pixel, before gamma.
pub fn pattern(glyph_id: u32, size_px: f32, x: i32, y: i32) -> u8 {
    let size_64 = (size_px * 64.0).round() as i32 as u32;
    ((glyph_id.wrapping_mul(31).wrapping_add(x as u32 * 7).wrapping_add(y as u32 * 13).wrapping_add(size_64)) % 255 + 1) as u8
}

pub fn zoom_bucket(size: f32) -> f32 {
    unsafe { gc_zoom_bucket(size) }
}

pub fn pen_split(x: f32) -> (i32, f32) {
    let (mut whole, mut subpixel_x) = (0, 0.0);
    unsafe { gc_pen_split(x, &mut whole, &mut subpixel_x) };
    (whole, subpixel_x)
}

/// Native's normalized-to-user conversion (font_provider.c), for DirectWrite
/// and CoreText: the user value of each axis, from fvar and avar's bytes.
pub fn user_axis_values(fvar: &[u8], avar: &[u8], coords: &[i16]) -> Vec<f32> {
    let mut out = vec![0.0; 64];
    let n = unsafe {
        gc_user_axis_values(
            fvar.as_ptr(),
            fvar.len() as u64,
            avar.as_ptr(),
            avar.len() as u64,
            coords.as_ptr(),
            coords.len() as u64,
            out.as_mut_ptr(),
        )
    };
    out.truncate(n as usize);
    out
}
