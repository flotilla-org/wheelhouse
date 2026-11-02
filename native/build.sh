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

# --- Translate Effect Shaders -------------------------------------------------
# effects are authored once in WGSL as a single fragment function (see
# native/src/effects/effect_prelude.wgsl for the contract) & translated per backend:
# prelude+effect -> naga -> SPIR-V -> spirv-cross -> MSL / HLSL SM5.0 / GLSL 330.
# (`cargo install naga-cli`, `brew install spirv-cross` or distro equivalent.)
# generated outputs are committed, so this only runs on demand: `native/build.sh effects`
if [ -n "${effects+x}" ]
then
  echo "[translating effect shaders]"
  command -v naga >/dev/null        || { echo "naga not found; cargo install naga-cli"; exit 1; }
  command -v spirv-cross >/dev/null || { echo "spirv-cross not found; brew install spirv-cross"; exit 1; }
  gen=native/src/effects/generated
  mkdir -p "$gen"
  for wgsl in native/src/effects/*.wgsl; do
    name=$(basename "$wgsl" .wgsl)
    if [ "$name" = "effect_prelude" ]; then continue; fi
    cat native/src/effects/effect_prelude.wgsl "$wgsl" > "$gen/$name.full.wgsl"
    # --keep-coordinate-space: the prelude's NDC math already matches every
    # backend's hand-written shaders; nobody gets an injected y-flip
    naga -g --keep-coordinate-space "$gen/$name.full.wgsl" "$gen/$name.spv"
    spirv-cross --msl                      "$gen/$name.spv" --entry vs_main --stage vert --output "$gen/$name.vs.metal"
    spirv-cross --msl                      "$gen/$name.spv" --entry fs_main --stage frag --output "$gen/$name.fs.metal"
    # --flip-vert-y: GL surface textures are stored bottom-up (the flipped stage
    # projection); flipping the fullscreen triangle keeps effect outputs in the
    # same convention the composite's surface-sampling path expects. effect-space
    # v is mirrored on GL as a result - symmetric effects don't notice; an
    # orientation uniform can join the contract when an asymmetric effect needs it
    spirv-cross --version 330 --flip-vert-y "$gen/$name.spv" --entry vs_main --stage vert --output "$gen/$name.vert"
    spirv-cross --version 330              "$gen/$name.spv" --entry fs_main --stage frag --output "$gen/$name.frag"
    spirv-cross --hlsl --shader-model 50   "$gen/$name.spv" --entry vs_main --stage vert --output "$gen/$name.vs.hlsl"
    spirv-cross --hlsl --shader-model 50   "$gen/$name.spv" --entry fs_main --stage frag --output "$gen/$name.fs.hlsl"
    rm "$gen/$name.full.wgsl" "$gen/$name.spv"
    echo "  [$name: msl, glsl330, hlsl-sm5]"
  done
  # embed all generated sources as a C table so backends need no filesystem
  python3 - "$gen" <<'PYEOF'
import sys, os
gen = sys.argv[1]
names = sorted(set(f.split('.')[0] for f in os.listdir(gen) if not f.startswith('effects_embed')))
def lit(path):
    src = open(path).read()
    e = src.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n"')
    return 'str8_lit_comp("%s")' % e
out = []
out.append("// generated by `native/build.sh effects` - do not edit\n")
out.append("typedef struct EFX_EmbeddedEffect EFX_EmbeddedEffect;\n")
out.append("struct EFX_EmbeddedEffect { String8 name; R_EffectSources sources; };\n\n")
out.append("read_only global EFX_EmbeddedEffect efx_embedded_effects[] =\n{\n")
for n in names:
    out.append("  {\n    str8_lit_comp(\"%s\"),\n    {\n" % n)
    for f in ("%s.vs.metal", "%s.fs.metal", "%s.vert", "%s.frag", "%s.vs.hlsl", "%s.fs.hlsl"):
        out.append("      %s,\n" % lit(os.path.join(gen, f % n)))
    out.append("    },\n  },\n")
out.append("};\n")
open(os.path.join(gen, "effects_embed.h"), "w").write("".join(out))
print("  [effects_embed.h: %s]" % ", ".join(names))
PYEOF
fi

# --- Generate Wayland Protocol Code -------------------------------------------
# The Wayland window manager's protocol marshalling, from wayland-scanner and
# the system's protocol XML (wayland, wayland-protocols). Generated outputs are
# committed, so a build needs neither: `native/build.sh wayland_protocols`
if [ -n "${wayland_protocols+x}" ]
then
  echo "[generating wayland protocol code]"
  command -v wayland-scanner >/dev/null || { echo "wayland-scanner not found; install wayland (or libwayland-bin)"; exit 1; }
  wl_core=$(pkg-config --variable=pkgdatadir wayland-scanner 2>/dev/null || echo /usr/share/wayland)
  wl_protocols=$(pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null || echo /usr/share/wayland-protocols)
  gen=native/src/linux/window_manager/wayland/generated
  mkdir -p "$gen"
  # tablet-v2 only provides the interface cursor-shape's get_tablet_tool_v2 names.
  for xml in "$wl_core/wayland.xml" \
             "$wl_protocols/stable/xdg-shell/xdg-shell.xml" \
             "$wl_protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml" \
             "$wl_protocols/staging/cursor-shape/cursor-shape-v1.xml" \
             "$wl_protocols/stable/tablet/tablet-v2.xml"; do
    name=$(basename "$xml" .xml)
    wayland-scanner --include-core-only client-header "$xml" "$gen/$name-protocol.h"
    wayland-scanner private-code "$xml" "$gen/$name-protocol.c"
    # The client headers come from third_party, not the system.
    sed -i.bak 's|#include "wayland-client-core.h"|#include "third_party/wayland/wayland-client-core.h"|; s|#include "wayland-util.h"|#include "third_party/wayland/wayland-util.h"|' "$gen/$name-protocol.h" "$gen/$name-protocol.c"
    rm -f "$gen/$name-protocol.h.bak" "$gen/$name-protocol.c.bak"
    echo "  [$name]"
  done
  didbuild=1
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
