#!/usr/bin/env bash
set -eu

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
workspace_root="$(cd "$repo_root/.." && pwd)"
repo_name="$(basename "$repo_root")"
image="${WHEELHOUSE_DOCKER_IMAGE:-rust:1.88-bookworm}"
fixture_ppm="${WHEELHOUSE_LINUX_OPENGL_FIXTURE_PPM:-local/screenshots/linux-opengl-terminal-fixture.ppm}"
uid="${UID:-$(id -u)}"
gid="$(id -g)"

docker run --rm \
  -v "$workspace_root:/work" \
  -w "/work/$repo_name" \
  -e WHEELHOUSE_LINUX_OPENGL_FIXTURE_PPM="$fixture_ppm" \
  "$image" \
  bash -lc "
set -eu
apt-get update >/dev/null
apt-get install -y python3 clang pkg-config libx11-dev libxext-dev libgl1-mesa-dev libegl1-mesa-dev libfreetype6-dev xvfb xauth ca-certificates >/dev/null
if ! getent group $gid >/dev/null; then groupadd -g $gid hostgroup; fi
useradd -m -u $uid -g $gid builder >/dev/null 2>&1 || true
su builder -c 'cd /work/$repo_name && fixture_ppm=\"\$WHEELHOUSE_LINUX_OPENGL_FIXTURE_PPM\" && mkdir -p \"\$(dirname \"\$fixture_ppm\")\" && export HOME=/home/builder PATH=/usr/local/cargo/bin:\$PATH CARGO_HOME=/tmp/cargo CARGO_TARGET_DIR=/tmp/cleat-target WHEELHOUSE_CLEAT_TARGET_DIR=/tmp/cleat-target WHEELHOUSE_CLEAT_FEATURES=none && bash build.sh wheelhouse && xvfb-run -a ./build/wheelhouse --terminal_glyph_diagnostics --terminal_glyph_fixture_ppm:\"\$fixture_ppm\" && test -s \"\$fixture_ppm\" && test \"\$(head -c 2 \"\$fixture_ppm\")\" = P6'
"

if [[ "$(uname -s)" == "Darwin" ]]; then
  bash "$repo_root/build.sh" wheelhouse
fi
