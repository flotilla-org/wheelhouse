#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
export PYTHONDONTWRITEBYTECODE=1
cc="${CC:-clang}"
tmp_dir="${TMPDIR:-/tmp}/uishell-freetype-color-smoke"
smoke_bin="$tmp_dir/freetype_color_smoke"
colr_v0_font="$tmp_dir/uishell_colr_v0_smoke.ttf"
colr_v1_font="$tmp_dir/uishell_colr_v1_smoke.ttf"
svg_font="$tmp_dir/uishell_svg_smoke.ttf"

mkdir -p "$tmp_dir"
cd "$repo_root"

"$cc" $(pkg-config --cflags freetype2) tools/freetype_color_smoke.c $(pkg-config --libs freetype2) -o "$smoke_bin"

"$smoke_bin" data/NotoColorEmoji.ttf 0x1f642 --expect-fixed

python3 tools/generate_freetype_colr_smoke_font.py "$colr_v0_font" --colr-version 0
"$smoke_bin" "$colr_v0_font" 0xe000 --expect-scalable

python3 tools/generate_freetype_colr_smoke_font.py "$colr_v1_font" --colr-version 1
"$smoke_bin" "$colr_v1_font" 0xe001 --expect-scalable --expect-no-bgra

python3 tools/generate_freetype_svg_smoke_font.py "$svg_font"
"$smoke_bin" "$svg_font" 0xe002 --expect-scalable --expect-load-fail
