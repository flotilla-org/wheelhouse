#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
export PYTHONDONTWRITEBYTECODE=1

cd "$repo_root"

python3 tools/generate_terminal_emoji_presentation_ranges.py --check \
  --input data/unicode/emoji-data.txt \
  --emoji-sequences-input data/unicode/emoji-sequences.txt \
  --emoji-zwj-sequences-input data/unicode/emoji-zwj-sequences.txt
tools/run-freetype-color-smoke-tests.sh
tools/validate-terminal-fixture-ppm.py --self-test

zig cc -target x86_64-windows-gnu -c -Isrc tools/dwrite_syntax_probe.c \
  -o /tmp/uishell_dwrite_syntax_probe.o \
  -Wno-undefined-internal -Wno-unknown-attributes

zig cc -target x86_64-windows-gnu -c -Isrc tools/d3d11_syntax_probe.c \
  -o /tmp/uishell_d3d11_syntax_probe.o \
  -Wno-undefined-internal -Wno-unknown-attributes -Wno-unused-value
echo "Windows runtime diagnostics are not run on this host; run tools\\run-windows-terminal-glyph-diagnostics.ps1 on Windows for D3D11/DirectWrite runtime evidence." >&2

zig cc -target x86_64-linux-gnu -c -Isrc $(pkg-config --cflags freetype2) tools/freetype_syntax_probe.c \
  -o /tmp/uishell_freetype_syntax_probe_linux.o \
  -Wno-undefined-internal -Wno-unknown-attributes

if [[ "$(uname -s)" == "Darwin" ]]; then
  tools/run-macos-terminal-glyph-diagnostics.sh
else
  echo "Skipping macOS/Metal runtime diagnostics: host is not macOS." >&2
fi

if command -v docker >/dev/null 2>&1; then
  tools/run-linux-opengl-terminal-glyph-diagnostics.sh
else
  echo "Skipping Linux/OpenGL Docker runtime diagnostics: docker is not available." >&2
fi

mac_fixture_ppm="${UISHELL_MACOS_METAL_FIXTURE_PPM:-local/screenshots/macos-metal-terminal-fixture.ppm}"
linux_fixture_ppm="${UISHELL_LINUX_OPENGL_FIXTURE_PPM:-local/screenshots/linux-opengl-terminal-fixture.ppm}"
windows_fixture_ppm="${UISHELL_WINDOWS_D3D11_FIXTURE_PPM:-local/screenshots/windows-d3d11-terminal-fixture.ppm}"
ghostty_reference_image="${UISHELL_GHOSTTY_REFERENCE_IMAGE:-}"
fixture_semantic_ppms=()
if [[ -s "$mac_fixture_ppm" ]]; then
  fixture_semantic_ppms+=("$mac_fixture_ppm")
fi
if [[ -s "$linux_fixture_ppm" ]]; then
  fixture_semantic_ppms+=("$linux_fixture_ppm")
fi
if [[ -s "$windows_fixture_ppm" ]]; then
  fixture_semantic_ppms+=("$windows_fixture_ppm")
fi
if (( ${#fixture_semantic_ppms[@]} > 0 )); then
  tools/validate-terminal-fixture-ppm.py "${fixture_semantic_ppms[@]}"
fi
if [[ -s "$mac_fixture_ppm" && -s "$linux_fixture_ppm" ]]; then
  diff_fixture_ppm="${UISHELL_METAL_OPENGL_DIFF_PPM:-local/screenshots/metal-opengl-terminal-fixture-diff.ppm}"
  tools/compare-terminal-fixture-ppms.py --diff-out "$diff_fixture_ppm" "$mac_fixture_ppm" "$linux_fixture_ppm"
  report_dir="${UISHELL_TERMINAL_FIXTURE_REPORT_DIR:-local/screenshots/terminal-fixture-report}"
  report_args=(--mac "$mac_fixture_ppm" --linux "$linux_fixture_ppm" --out-dir "$report_dir")
  if [[ -s "$windows_fixture_ppm" ]]; then
    report_args+=(--windows "$windows_fixture_ppm")
  fi
  if [[ -n "$ghostty_reference_image" ]]; then
    if [[ ! -s "$ghostty_reference_image" ]]; then
      echo "Ghostty reference image was requested but not found: $ghostty_reference_image" >&2
      exit 1
    fi
    report_args+=(--reference-image "$ghostty_reference_image" --reference-caption "Ghostty Reference")
  fi
  tools/make-terminal-fixture-report.py "${report_args[@]}"
else
  echo "Skipping terminal fixture artifact comparison: both Metal and OpenGL PPMs are not available." >&2
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  bash build.sh uishell
fi

git diff --check
