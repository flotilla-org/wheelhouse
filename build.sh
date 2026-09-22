#!/bin/bash
set -eu
cd "$(dirname "$0")"
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
  cargo build --manifest-path "$jackstay_dir/Cargo.toml" -p jackstay --locked --target-dir "$jackstay_target_dir" $cargo_profile_flags
  auto_compile_flags="$auto_compile_flags -DWHEELHOUSE_JACKSTAY=1 -I$jackstay_dir/crates/jackstay/include"
  jackstay_link="-L$jackstay_target_dir/$cargo_profile -ljackstay -Wl,-rpath,$jackstay_target_dir/$cargo_profile"
fi

# --- Embedded sidebar core ---------------------------------------------------
andamento_link=''
if [ -n "${wheelhouse+x}" ]; then
  andamento_dir="${WHEELHOUSE_ANDAMENTO_DIR:-$repo_root/../andamento}"
  andamento_target=$(rustc -vV | sed -n 's/^host: //p')
  andamento_target_dir="${WHEELHOUSE_ANDAMENTO_TARGET_DIR:-${CARGO_TARGET_DIR:-$andamento_dir/target}}"
  python3 tools/prepare-andamento-build.py "$andamento_dir"
  cargo build --manifest-path "$repo_root/build/andamento/Cargo.toml" -p andamento-ffi -p wheelhouse-native-deps --locked --target "$andamento_target" --target-dir "$andamento_target_dir" $cargo_profile_flags
  andamento_lib_dir="$andamento_target_dir/$andamento_target/$cargo_profile"
  auto_compile_flags="$auto_compile_flags -I$andamento_dir/crates/andamento-ffi/include"
  andamento_link="-L$andamento_lib_dir -landamento_ffi -lwheelhouse_ingress -Wl,-rpath,$andamento_lib_dir"
  python3 tools/embed-sidebar-fixture.py
fi

# --- Get Current Git Commit Id -----------------------------------------------
git_hash=$(git describe --always --dirty 2>/dev/null || echo unknown)
git_hash_full=$(git rev-parse HEAD 2>/dev/null || echo unknown)

# --- Compile/Link Line Definitions -------------------------------------------
host_os=$(uname -s)
clang_common="-I../src/ -I/usr/include/freetype2/ -I../local/ -D_GNU_SOURCE -g -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -Wno-unknown-warning-option -fdiagnostics-absolute-paths -Wall -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Wno-for-loop-analysis -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
if [ "$host_os" = "Darwin" ]; then
  clang_common="$clang_common -x objective-c"
fi
clang_debug="$compiler -g -O0 -DBUILD_DEBUG=1 ${clang_common} ${auto_compile_flags}"
clang_release="$compiler -g -O2 -DBUILD_DEBUG=0 ${clang_common} ${auto_compile_flags}"
clang_link="-lpthread -lm -lrt -ldl"
clang_out="-o"
gcc_common="-I../src/ -I../local/ -g -D_GNU_SOURCE -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -Wno-unknown-warning-option -Wall -Wno-missing-braces -Wno-unused-function -Wno-attributes -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-compare-distinct-pointer-types -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
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
link_os_gfx="-lX11 -lXext"
link_render="-lGL -lEGL"
link_font_provider="-lfreetype"

if [ "$host_os" = "Darwin" ]; then
  link_os_gfx="-framework Cocoa -framework Security"
  link_render="-framework Metal -framework QuartzCore"
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
  $compile_debug ../src/metagen/metagen_main.c $compile_link $out metagen
  ./metagen
  cd ..
fi

# --- Translate Effect Shaders -------------------------------------------------
# effects are authored once in WGSL as a single fragment function (see
# src/effects/effect_prelude.wgsl for the contract) & translated per backend:
# prelude+effect -> naga -> SPIR-V -> spirv-cross -> MSL / HLSL SM5.0 / GLSL 330.
# (`cargo install naga-cli`, `brew install spirv-cross` or distro equivalent.)
# generated outputs are committed, so this only runs on demand: `./build.sh effects`
if [ -n "${effects+x}" ]
then
  echo "[translating effect shaders]"
  command -v naga >/dev/null        || { echo "naga not found; cargo install naga-cli"; exit 1; }
  command -v spirv-cross >/dev/null || { echo "spirv-cross not found; brew install spirv-cross"; exit 1; }
  gen=src/effects/generated
  mkdir -p "$gen"
  for wgsl in src/effects/*.wgsl; do
    name=$(basename "$wgsl" .wgsl)
    if [ "$name" = "effect_prelude" ]; then continue; fi
    cat src/effects/effect_prelude.wgsl "$wgsl" > "$gen/$name.full.wgsl"
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
out.append("// generated by `build.sh effects` - do not edit\n")
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

# --- Build Everything (@build_targets) ---------------------------------------
cd build
sign_app_debug()
{
  if [ "$host_os" = "Darwin" ]; then
    codesign_identity="${WHEELHOUSE_CODESIGN_IDENTITY:--}"
    codesign_entitlements="${WHEELHOUSE_CODESIGN_ENTITLEMENTS:-../src/mac/uishell_debug.entitlements}"
    codesign --force --sign "$codesign_identity" --entitlements "$codesign_entitlements" "$1"
  fi
}
if [ -n "${wheelhouse+x}" ]
then
  didbuild=1
  # Compile to a persistent object first so dsymutil can collect DWARF: a single
  # compile+link invocation uses a temp .o that clang deletes, leaving a broken
  # debug map. Then link, then produce a co-located .dSYM (and the linked cleat
  # dylib's) for profiling/debugging across the wheelhouse+cleat+ghostty stack.
  # Keep required steps separate: set -e ignores failures before &&.
  $compile -c ../src/uishell/uishell_main.c $out uishell_main.o
  $compile -x none uishell_main.o $compile_link $link_os_gfx $link_render $link_font_provider $cleat_link $andamento_link $jackstay_link $out wheelhouse
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
if [ -n "${bundle+x}" ];              then didbuild=1; if [ "$host_os" != "Darwin" ]; then echo "[ERROR] bundle target is only supported on Darwin."; exit 1; fi; if [ ! -f wheelhouse ]; then echo "[ERROR] bundle requires the wheelhouse target (./build.sh wheelhouse bundle)."; exit 1; fi; rm -rf "Wheelhouse.app"; mkdir -p "Wheelhouse.app/Contents/MacOS" "Wheelhouse.app/Contents/Resources"; cp ../src/mac/uishell_Info.plist "Wheelhouse.app/Contents/Info.plist"; cp ../src/mac/uishell.icns "Wheelhouse.app/Contents/Resources/wheelhouse.icns"; cp wheelhouse "Wheelhouse.app/Contents/MacOS/wheelhouse"; chmod +x "Wheelhouse.app/Contents/MacOS/wheelhouse"; sign_app_debug "Wheelhouse.app/Contents/MacOS/wheelhouse"; sign_app_debug "Wheelhouse.app"; fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [ -z "${didbuild+x}" ]
then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`./build.sh wheelhouse\` or \`./build.sh bundle\`."
  exit 1
fi
