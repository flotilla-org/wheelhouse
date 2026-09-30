#!/bin/bash
set -euo pipefail

# Requires a macOS graphical login; no Accessibility permission is needed.
cd "$(dirname "$0")/../../../.."
mkdir -p build
# Compile only the base/WM layers: sourcing build.sh would run application builds
# and pull in renderer/provider dependencies. These warning exceptions match its
# shared-layer exceptions; other default clang warnings remain enabled.
clang -x objective-c -Isrc -Ilocal -g -O0 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value \
  -Wno-deprecated-declarations -Wno-incompatible-pointer-types-discards-qualifiers \
  src/mac/window_manager/tests/scroll_events.c \
  -framework Cocoa -framework Security -framework QuartzCore \
  -o build/mac_scroll_events
build/mac_scroll_events
