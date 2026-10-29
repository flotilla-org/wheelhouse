//! Builds the C oracle (native/src/shell/tests/arrangement_oracle.c) as the
//! native shell's headless tests build it (tools/test-docking.py): the base
//! layer, config and a stub window manager in one unit.
fn main() {
    println!("cargo:rustc-check-cfg=cfg(oracle)");
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let native = root.join("native/src");
    let source = native.join("shell/tests/arrangement_oracle.c");
    for dir in ["base", "config", "mdesk", "window_manager", "linux", "mac", "shell", "third_party", "uishell/uishell_renderers.h"] {
        println!("cargo:rerun-if-changed={}", native.join(dir).display());
    }
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if !matches!(target_os.as_str(), "linux" | "macos") {
        // The oracle's tests run where CI runs `cargo test` (core.yml).
        return;
    }
    let mut build = cc::Build::new();
    build
        .file(&source)
        .include(&native)
        // shell_arrangement.h names the core's edit document.
        .include(root.join("crates/wheelhouse-ffi/include"))
        .define("_GNU_SOURCE", None)
        // Its own `main` would be the test binary's.
        .define("main", "wh_oracle_base_main")
        .opt_level(0)
        .warnings(false)
        .extra_warnings(false)
        // The oracle is the C rules' meaning, not a compiler's: no fused
        // multiply-adds (clang contracts by default on arm64).
        .flag_if_supported("-ffp-contract=off")
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
    build.compile("wheelhouse_arrangement_oracle");
    println!("cargo:rustc-cfg=oracle");
}
