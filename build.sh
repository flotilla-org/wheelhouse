#!/bin/bash
set -eu
cd "$(dirname "$0")"
repo_root="$(pwd)"

# --- Unpack Arguments --------------------------------------------------------
for arg in "$@"; do declare $arg='1'; done
if [ -z "${gcc+x}" ];     then clang=1; fi
if [ -z "${release+x}" ]; then debug=1; fi
if [ -n "${debug+x}" ];   then echo "[debug mode]"; fi
if [ -n "${release+x}" ]; then echo "[release mode]"; fi
if [ -n "${clang+x}" ];   then compiler="${CC:-clang}"; echo "[clang compile]"; fi
if [ -n "${gcc+x}" ];     then compiler="${CC:-gcc}"; echo "[gcc compile]"; fi

# --- Unpack Command Line Build Arguments -------------------------------------
auto_compile_flags=''
cleat_link=''

if [ -n "${cleat+x}" ]; then
  cleat_dir="${UISHELL_CLEAT_DIR:-$repo_root/../cleat}"
  cleat_features="${UISHELL_CLEAT_FEATURES:-ghostty-vt}"
  cleat_profile="debug"
  cleat_profile_flags=""
  if [ -n "${release+x}" ]; then
    cleat_profile="release"
    cleat_profile_flags="--release"
  fi
  cleat_target_dir="${UISHELL_CLEAT_TARGET_DIR:-$cleat_dir/target}"
  cleat_lib_dir="$cleat_target_dir/$cleat_profile"
  echo "[cleat provider: $cleat_dir]"
  (cd "$cleat_dir" && cargo build -p cleat --locked $cleat_profile_flags --features "$cleat_features")
  auto_compile_flags="$auto_compile_flags -DUISHELL_USE_CLEAT_PROVIDER=1 -I$cleat_dir/crates/cleat/include"
  cleat_link="-L$cleat_lib_dir -lcleat -Wl,-rpath,$cleat_lib_dir"
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

# --- Build Everything (@build_targets) ---------------------------------------
cd build
sign_app_debug()
{
  if [ "$host_os" = "Darwin" ]; then
    codesign_identity="${UISHELL_CODESIGN_IDENTITY:--}"
    codesign_entitlements="${UISHELL_CODESIGN_ENTITLEMENTS:-../src/mac/uishell_debug.entitlements}"
    codesign --force --sign "$codesign_identity" --entitlements "$codesign_entitlements" "$1"
  fi
}
if [ -n "${uishell+x}" ];             then didbuild=1 && $compile ../src/uishell/uishell_main.c                                  $compile_link $link_os_gfx $link_render $link_font_provider $cleat_link $out uishell; sign_app_debug uishell; fi
if [ -n "${bundle+x}" ];              then didbuild=1; if [ "$host_os" != "Darwin" ]; then echo "[ERROR] bundle target is only supported on Darwin."; exit 1; fi; $compile ../src/uishell/uishell_main.c $compile_link $link_os_gfx $link_render $link_font_provider $cleat_link $out uishell; sign_app_debug uishell; rm -rf "UI Shell.app"; mkdir -p "UI Shell.app/Contents/MacOS" "UI Shell.app/Contents/Resources"; cp ../src/mac/uishell_Info.plist "UI Shell.app/Contents/Info.plist"; cp ../src/mac/uishell.icns "UI Shell.app/Contents/Resources/uishell.icns"; cp uishell "UI Shell.app/Contents/MacOS/uishell"; chmod +x "UI Shell.app/Contents/MacOS/uishell"; sign_app_debug "UI Shell.app/Contents/MacOS/uishell"; sign_app_debug "UI Shell.app"; fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [ -z "${didbuild+x}" ]
then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`./build.sh uishell\` or \`./build.sh bundle\`."
  exit 1
fi
