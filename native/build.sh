#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
repo_root="$(pwd)"

# --- Unpack Arguments --------------------------------------------------------
for arg in "$@"; do declare $arg='1'; done
if [ -n "${bundle+x}" ]; then wheelhouse=1; fi
if [ -z "${gcc+x}" ];     then clang=1; fi
if [ -z "${release+x}" ]; then debug=1; fi
if [ -n "${debug+x}" ];   then echo "[debug mode]"; fi
if [ -n "${release+x}" ]; then echo "[release mode]"; fi
if [ -n "${clang+x}" ];   then compiler="${CC:-clang}"; echo "[clang compile]"; fi
if [ -n "${gcc+x}" ];     then compiler="${CC:-gcc}"; echo "[gcc compile]"; fi

# --- Unpack Command Line Build Arguments -------------------------------------
auto_compile_flags=''
cleat_link=''

cargo_profile="debug"
cargo_profile_flags=""
if [ -n "${release+x}" ]; then
  cargo_profile="release"
  cargo_profile_flags="--release"
fi

needs_cleat=0
if [ -n "${cleat+x}" ]; then needs_cleat=1; fi
if [ -n "${wheelhouse+x}" ] || [ -n "${bundle+x}" ]; then needs_cleat=1; fi

if [ "$needs_cleat" = "1" ]; then
  cleat_dir="${WHEELHOUSE_CLEAT_DIR:-$repo_root/../cleat}"
  cleat_features="${WHEELHOUSE_CLEAT_FEATURES-ghostty-vt}"
  cleat_feature_flags=()
  if [ -n "$cleat_features" ] && [ "$cleat_features" != "none" ]; then
    cleat_feature_flags=(--features "$cleat_features")
  fi
  cleat_target_dir="${WHEELHOUSE_CLEAT_TARGET_DIR:-${CARGO_TARGET_DIR:-$cleat_dir/target}}"
  cleat_lib_dir="$cleat_target_dir/$cargo_profile"
  echo "[cleat provider: $cleat_dir]"
  if [ -n "$cleat_features" ] && [ "$cleat_features" != "none" ]; then
    echo "[cleat features: $cleat_features]"
  else
    echo "[cleat features: none]"
  fi
  (cd "$cleat_dir" && CARGO_TARGET_DIR="$cleat_target_dir" cargo build -p cleat --locked --no-default-features $cargo_profile_flags "${cleat_feature_flags[@]}")
  if [ -n "${cleat+x}" ]; then didbuild=1; fi
  auto_compile_flags="$auto_compile_flags -I$cleat_dir/crates/cleat/include"
  cleat_link="-L$cleat_lib_dir -lcleat -Wl,-rpath,$cleat_lib_dir"
fi

# --- Jackstay CPU stream consumer (Unix C ABI) --------------------------------
jackstay_link=''
if [ -n "${wheelhouse+x}" ]; then
  jackstay_dir="${WHEELHOUSE_JACKSTAY_DIR:-$repo_root/../jackstay}"
  jackstay_target_dir="${WHEELHOUSE_JACKSTAY_TARGET_DIR:-$jackstay_dir/target}"
  jackstay_native_flags=()
  if [ "$(uname -s)" = Darwin ]; then jackstay_native_flags=(--features backend-macos); fi
  cargo build --manifest-path "$jackstay_dir/Cargo.toml" -p jackstay --locked --target-dir "$jackstay_target_dir" $cargo_profile_flags "${jackstay_native_flags[@]}"
  auto_compile_flags="$auto_compile_flags -DWHEELHOUSE_JACKSTAY=1 -I$jackstay_dir/crates/jackstay/include"
  jackstay_link="-L$jackstay_target_dir/$cargo_profile -ljackstay -Wl,-rpath,$jackstay_target_dir/$cargo_profile"
fi

# --- Embedded sidebar core ---------------------------------------------------
core_link=''
if [ -n "${wheelhouse+x}" ]; then
  # The core and the ingress are members of this repo's Cargo workspace (#440).
  if [ -n "${WHEELHOUSE_ANDAMENTO_DIR:-}" ]; then
    echo "build.sh: ignoring WHEELHOUSE_ANDAMENTO_DIR: the core is in this repo now (#440)" >&2
  fi
  core_target=$(rustc -vV | sed -n 's/^host: //p')
  # WHEELHOUSE_ANDAMENTO_TARGET_DIR stays a while as an alias for CARGO_TARGET_DIR.
  core_target_dir="${WHEELHOUSE_ANDAMENTO_TARGET_DIR:-${CARGO_TARGET_DIR:-$repo_root/target}}"
  # DAILY_DRIVER_WATCHER: daily-driver.py relies on this build including the native git watcher.
  cargo build --manifest-path "$repo_root/Cargo.toml" -p wheelhouse-ffi -p wheelhouse-ingress -p wheelhouse-git-watcher --locked --target "$core_target" --target-dir "$core_target_dir" $cargo_profile_flags
  core_lib_dir="$core_target_dir/$core_target/$cargo_profile"
  # binnacle.h comes with the binnacle-ffi the core links (#506).
  binnacle_include=$(python3 "$repo_root/tools/binnacle-include.py")
  auto_compile_flags="$auto_compile_flags -I$repo_root/crates/wheelhouse-ffi/include -I$repo_root/crates/wheelhouse-ingress/include -I$binnacle_include"
  core_link="-L$core_lib_dir -lwheelhouse_ffi -lwheelhouse_ingress -Wl,-rpath,$core_lib_dir"
  python3 tools/embed-sidebar-fixture.py
fi

# --- Get Current Git Commit Id -----------------------------------------------
git_hash=$(git describe --always --dirty 2>/dev/null || echo unknown)
git_hash_full=$(git rev-parse HEAD 2>/dev/null || echo unknown)

# --- Compile/Link Line Definitions -------------------------------------------
host_os=$(uname -s)
# The Wayland window manager's protocol code, generated from the system's
# protocol XML on every Linux build (tools/wayland-include.py).
if [ "$host_os" = "Linux" ]; then
  wayland_include=$(python3 "$repo_root/tools/wayland-include.py")
  auto_compile_flags="$auto_compile_flags -I$wayland_include"
fi
# Extra compiler flags for one build, e.g. WHEELHOUSE_CFLAGS=-DSOME_SWITCH=1.
auto_compile_flags="$auto_compile_flags ${WHEELHOUSE_CFLAGS:-}"
clang_common="-I../native/src/ -I/usr/include/freetype2/ -I../local/ -D_GNU_SOURCE -g -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -Wno-unknown-warning-option -fdiagnostics-absolute-paths -Wall -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Wno-for-loop-analysis -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
if [ "$host_os" = "Darwin" ]; then
  clang_common="$clang_common -x objective-c"
fi
clang_debug="$compiler -g -O0 -DBUILD_DEBUG=1 ${clang_common} ${auto_compile_flags}"
clang_release="$compiler -g -O2 -DBUILD_DEBUG=0 ${clang_common} ${auto_compile_flags}"
clang_link="-lpthread -lm -lrt -ldl"
clang_out="-o"
gcc_common="-I../native/src/ -I../local/ -g -D_GNU_SOURCE -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -Wno-unknown-warning-option -Wall -Wno-missing-braces -Wno-unused-function -Wno-attributes -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-compare-distinct-pointer-types -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
gcc_debug="$compiler -g -O0 -DBUILD_DEBUG=1 ${gcc_common} ${auto_compile_flags}"
gcc_release="$compiler -g -O2 -DBUILD_DEBUG=0 ${gcc_common} ${auto_compile_flags}"
gcc_link="-lpthread -lm -lrt -ldl"
gcc_out="-o"

if [ "$host_os" = "Darwin" ]; then
  clang_link="-lpthread -lm"
  gcc_link="-lpthread -lm"
fi

# --- Per-Build Settings ------------------------------------------------------
link_dll="-fPIC"
link_os_gfx="-lX11 -lXext -lXrandr"
link_render="-lGL -lEGL"
link_font_provider="-lfreetype"

if [ "$host_os" = "Darwin" ]; then
  link_os_gfx="-framework Cocoa -framework Security"
  link_render="-framework Metal -framework QuartzCore -framework IOSurface"
  link_font_provider="-framework CoreText -framework CoreGraphics -framework CoreFoundation"
fi

# --- Choose Compile/Link Lines -----------------------------------------------
if [ -n "${gcc+x}" ];     then compile_debug="$gcc_debug"; fi
if [ -n "${gcc+x}" ];     then compile_release="$gcc_release"; fi
if [ -n "${gcc+x}" ];     then compile_link="$gcc_link"; fi
if [ -n "${gcc+x}" ];     then out="$gcc_out"; fi
if [ -n "${clang+x}" ];   then compile_debug="$clang_debug"; fi
if [ -n "${clang+x}" ];   then compile_release="$clang_release"; fi
if [ -n "${clang+x}" ];   then compile_link="$clang_link"; fi
if [ -n "${clang+x}" ];   then out="$clang_out"; fi
if [ -n "${debug+x}" ];   then compile="$compile_debug"; fi
if [ -n "${release+x}" ]; then compile="$compile_release"; fi

# --- Prep Directories --------------------------------------------------------
mkdir -p build
mkdir -p local

# --- Build & Run Metaprogram -------------------------------------------------
if [ -n "${meta+x}" ]
then
  echo "[doing metagen]"
  cd build
  $compile_debug ../native/src/metagen/metagen_main.c $compile_link $out metagen
  ./metagen
  cd ..
fi

# --- Build Everything (@build_targets) ---------------------------------------
cd build
sign_app_debug()
{
  if [ "$host_os" = "Darwin" ]; then
    codesign_identity="${WHEELHOUSE_CODESIGN_IDENTITY:--}"
    codesign_entitlements="${WHEELHOUSE_CODESIGN_ENTITLEMENTS:-../native/src/mac/uishell_debug.entitlements}"
    codesign --force --sign "$codesign_identity" --entitlements "$codesign_entitlements" "$1"
  fi
}
if [ -n "${wheelhouse+x}" ]
then
  didbuild=1
  # The embedded fonts and icon (~15 MB of generated byte arrays) are their own
  # object, rebuilt only when their sources are newer. It holds data only, so one
  # object serves debug and release; the harnesses below link it too.
  if [ ! -f shell_embed.o ] || [ ../native/src/shell/shell_embed.c -nt shell_embed.o ] || [ ../native/src/shell/generated/shell.embed.meta.c -nt shell_embed.o ]; then
    echo "[compiling embedded data]"
    $compile_debug -c ../native/src/shell/shell_embed.c $out shell_embed.o.tmp
    mv shell_embed.o.tmp shell_embed.o
  fi
  # Compile to a persistent object first so dsymutil can collect DWARF: a single
  # compile+link invocation uses a temp .o that clang deletes, leaving a broken
  # debug map. Then link, then produce a co-located .dSYM (and the linked cleat
  # dylib's) for profiling/debugging across the wheelhouse+cleat+ghostty stack.
  # Keep required steps separate: set -e ignores failures before &&.
  # CI enforces the headless contract; developer builds opt in explicitly.
  if [ "${CI:-}" = "true" ] || [ "${WHEELHOUSE_DOCKING_TESTS:-0}" = "1" ]; then
    CC="$compiler" python3 ../tools/test-docking.py "$core_lib_dir"
    # fontique's font picks beside fontconfig's or CoreText's (#560): the tables
    # it prints are docs/research/fontique-parity.md's evidence. It uses no
    # Hegel, so HEGEL_C_LIB_DIR skips building Hegel's engine.
    HEGEL_C_LIB_DIR="$core_target_dir" cargo test --manifest-path "$repo_root/Cargo.toml" -p wheelhouse-text --test parity --locked \
      --target "$core_target" --target-dir "$core_target_dir" $cargo_profile_flags -- --ignored --nocapture
    if [ "$host_os" = "Linux" ]; then
      CC="$compiler" python3 ../tools/test-docking-integration.py "$core_lib_dir" "$cleat_lib_dir" \
        --core-include "$repo_root/crates/wheelhouse-ffi/include" --cleat-include "$cleat_dir/crates/cleat/include"
      # Restart behaviour against golden logical-state snapshots (#314).
      CC="$compiler" python3 ../tools/test-state-behaviour.py "$core_lib_dir" "$cleat_lib_dir" \
        --core-include "$repo_root/crates/wheelhouse-ffi/include" --cleat-include "$cleat_dir/crates/cleat/include"
      # Ingress, its recorder, and connector supervision (crates/wheelhouse-ingress).
      cargo test --manifest-path "$repo_root/Cargo.toml" -p wheelhouse-ingress --lib --locked \
        --target "$core_target" --target-dir "$core_target_dir" $cargo_profile_flags
    fi
  fi
  $compile -c ../native/src/uishell/uishell_main.c $out uishell_main.o
  $compile -x none uishell_main.o shell_embed.o $compile_link $link_os_gfx $link_render $link_font_provider $cleat_link $core_link $jackstay_link $out wheelhouse
  if [ "$host_os" = "Darwin" ]; then
    dsymutil wheelhouse
    rm -f uishell_main.o
    [ -f "$cleat_lib_dir/libcleat.dylib" ] && dsymutil "$cleat_lib_dir/libcleat.dylib"
    # The ghostty dSYM lives under cleat's .tools/ dot-dir, which Spotlight
    # never indexes, so Instruments can't find it by UUID. Copy it into an
    # indexed location and force-index all three dSYMs so attaching symbolicates
    # the whole wheelhouse+cleat+ghostty stack.
    mkdir -p dsyms
    ghostty_dsym=$(ls -d "$cleat_dir"/.tools/ghostty-install/lib/libghostty-vt*.dylib.dSYM 2>/dev/null | head -1)
    if [ -n "$ghostty_dsym" ]; then
      rm -rf "dsyms/$(basename "$ghostty_dsym")"
      cp -R "$ghostty_dsym" dsyms/
    fi
    if command -v mdimport >/dev/null 2>&1; then
      mdimport wheelhouse.dSYM "$cleat_lib_dir/libcleat.dylib.dSYM" dsyms/*.dSYM >/dev/null 2>&1
    fi
  fi
  sign_app_debug wheelhouse
fi
# The bundle wraps the wheelhouse target's binary (built above with a persistent
# object + co-located dSYM) rather than one-shot compiling its own: a separate
# compile gets a different UUID with a broken debug map, so app-bundle launches
# would profile/debug an unsymbolicatable (& possibly stale) binary.
if [ -n "${bundle+x}" ];              then didbuild=1; if [ "$host_os" != "Darwin" ]; then echo "[ERROR] bundle target is only supported on Darwin."; exit 1; fi; if [ ! -f wheelhouse ]; then echo "[ERROR] bundle requires the wheelhouse target (native/build.sh wheelhouse bundle)."; exit 1; fi; rm -rf "Wheelhouse.app"; mkdir -p "Wheelhouse.app/Contents/MacOS" "Wheelhouse.app/Contents/Resources"; cp ../native/src/mac/uishell_Info.plist "Wheelhouse.app/Contents/Info.plist"; cp ../native/src/mac/uishell.icns "Wheelhouse.app/Contents/Resources/wheelhouse.icns"; cp wheelhouse "Wheelhouse.app/Contents/MacOS/wheelhouse"; chmod +x "Wheelhouse.app/Contents/MacOS/wheelhouse"; sign_app_debug "Wheelhouse.app/Contents/MacOS/wheelhouse"; sign_app_debug "Wheelhouse.app"; fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [ -z "${didbuild+x}" ]
then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`native/build.sh wheelhouse\` or \`native/build.sh bundle\`."
  exit 1
fi
