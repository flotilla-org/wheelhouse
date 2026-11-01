//! Builds native's glyph cache (native/src/font_cache/font_glyph_cache.c)
//! with its test shim and the base layer, as the arrangement oracle's
//! build.rs builds its C.
fn main() {
    println!("cargo:rustc-check-cfg=cfg(glyph_cache)");
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let native = root.join("native/src");
    for dir in ["base", "linux", "mac", "font_cache", "font_provider", "third_party/xxHash"] {
        println!("cargo:rerun-if-changed={}", native.join(dir).display());
    }
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if !matches!(target_os.as_str(), "linux" | "macos") {
        // The tests run where CI runs `cargo test` (core.yml).
        return;
    }
    let mut build = cc::Build::new();
    build
        .file(native.join("font_cache/tests/glyph_cache_shim.c"))
        .include(&native)
        .define("_GNU_SOURCE", None)
        // Its own `main` would be the test binary's.
        .define("main", "wh_glyph_cache_base_main")
        .opt_level(1)
        .warnings(false)
        .extra_warnings(false)
        .flag_if_supported("-Wno-initializer-overrides")
        .flag_if_supported("-Wno-unused-value")
        .flag_if_supported("-Wno-incompatible-pointer-types-discards-qualifiers")
        .flag_if_supported("-Wno-discarded-qualifiers");
    if target_os == "macos" {
        build.flag("-x").flag("objective-c");
        println!("cargo:rustc-link-lib=framework=Cocoa");
        println!("cargo:rustc-link-lib=framework=Security");
    } else {
        println!("cargo:rustc-link-lib=dl");
    }
    build.compile("wheelhouse_glyph_cache");
    println!("cargo:rustc-cfg=glyph_cache");
}
