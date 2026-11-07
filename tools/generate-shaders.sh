#!/usr/bin/env bash
# Generate every shader for every backend from its Slang source (ADR 0015
# section 5). The generated files are committed, so only someone changing a
# shader needs the tools.
#
# Core shaders: native/src/shaders/{rect,blur}.slang ->
#   native/src/shaders/generated/<shader>.<stage>.<ext>, plus one C header
#   per target (core_shaders_glsl.h, _hlsl.h, _msl.h), so each backend embeds
#   only its own sources.
# Effects: native/src/effects/*.slang (each includes effect_prelude.slang) ->
#   native/src/effects/generated/<effect>.<stage>.<ext>, plus effects_embed.h,
#   which gives only the build's own backend real sources.
#
#   SLANGC=...       slangc
#   SPIRV_CROSS=...  spirv-cross
#
# Both come from the pinned Vulkan SDK (sdk.lunarg.com, Linux tarball
# vulkansdk-linux-x86_64-1.4.363.1, SHA-256
# b7867d8ca658563813af666f34b93db87f1b1c7d9d69d97e924c210c7ba33eb8; the macOS
# and Windows SDKs of the same version carry the same tools). A different
# version is refused, because its output would differ from the committed
# files; set ALLOW_SHADER_TOOL_MISMATCH=1 to generate anyway, e.g. when
# moving the pin.
#
# Paths: HLSL SM5 and MSL come straight from slangc. GLSL 330 goes
# Slang -> SPIR-V -> spirv-cross, because OpenGL is not a Slang target.
# GLSL 330 links vertex outputs to fragment inputs by name, and the two
# stages name them differently, so every varying is renamed by its location
# (v0, v1, ...) on both sides. Effects' GLSL vertex stage is also flipped
# (--flip-vert-y): GL surface textures are stored bottom-up, and flipping the
# fullscreen triangle keeps effect output in the convention the composite's
# surface sampling expects (effect-space v is mirrored on GL as a result).
set -euo pipefail

pinned_slang="2026.17-25-gc7954ebd1"
pinned_spirv_cross="vulkan-sdk-1.4.363.0"

repo_root=$(cd "$(dirname "$0")/.." && pwd)
src="$repo_root/native/src/shaders"
out="$src/generated"
SLANGC=${SLANGC:-slangc}
SPIRV_CROSS=${SPIRV_CROSS:-spirv-cross}
for tool in "$SLANGC" "$SPIRV_CROSS"; do
  command -v "$tool" >/dev/null || { echo "missing tool: $tool (see the header of $0)"; exit 1; }
done
slang_version=$("$SLANGC" -version 2>&1 | head -1)
spirv_cross_version=$("$SPIRV_CROSS" --revision 2>&1 | sed -n 's/^Git commit: \([^ ]*\).*/\1/p')
if [ "$slang_version" != "$pinned_slang" ] || [ "$spirv_cross_version" != "$pinned_spirv_cross" ]; then
  echo "shader tools: slangc $slang_version (pinned $pinned_slang), spirv-cross $spirv_cross_version (pinned $pinned_spirv_cross)"
  if [ "${ALLOW_SHADER_TOOL_MISMATCH:-}" != "1" ]; then
    echo "refusing to generate with unpinned tools; set ALLOW_SHADER_TOOL_MISMATCH=1 to override"
    exit 1
  fi
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
rename_out=(); rename_in=()
for i in $(seq 0 15); do
  rename_out+=(--rename-interface-variable out "$i" "v$i")
  rename_in+=(--rename-interface-variable in "$i" "v$i")
done

generate()
{
  # generate <dir> <name> <out-dir> [extra spirv-cross flags for the GLSL vertex stage]
  local dir=$1 name=$2 dst=$3; shift 3
  (cd "$dir" &&
   "$SLANGC" "$name.slang" -I . -target spirv -fvk-use-entrypoint-name -o "$tmp/$name.spv" &&
   "$SLANGC" "$name.slang" -I . -target hlsl -entry vs_main -stage vertex -o "$dst/$name.vs.hlsl" &&
   "$SLANGC" "$name.slang" -I . -target hlsl -entry fs_main -stage fragment -o "$dst/$name.fs.hlsl" &&
   "$SLANGC" "$name.slang" -I . -target metal -entry vs_main -stage vertex -o "$dst/$name.vs.metal" &&
   "$SLANGC" "$name.slang" -I . -target metal -entry fs_main -stage fragment -o "$dst/$name.fs.metal")
  "$SPIRV_CROSS" --version 330 --no-420pack-extension "$tmp/$name.spv" --entry vs_main --stage vert "$@" "${rename_out[@]}" --output "$dst/$name.vert"
  "$SPIRV_CROSS" --version 330 --no-420pack-extension "$tmp/$name.spv" --entry fs_main --stage frag "${rename_in[@]}" --output "$dst/$name.frag"
}

# slangc runs inside each source directory with relative paths, so the #line
# directives in its output name rect.slang, not this machine's checkout path.
find "$out" -maxdepth 1 -type f -delete 2>/dev/null || true
mkdir -p "$out"
for name in rect blur; do
  generate "$src" "$name" "$out"
done
efx_src="$repo_root/native/src/effects"
efx_out="$efx_src/generated"
find "$efx_out" -maxdepth 1 -type f -delete 2>/dev/null || true
mkdir -p "$efx_out"
efx_names=()
for f in "$efx_src"/*.slang; do
  name=$(basename "$f" .slang)
  [ "$name" = "effect_prelude" ] && continue
  efx_names+=("$name")
  generate "$efx_src" "$name" "$efx_out" --flip-vert-y
done

python3 - "$out" "$efx_out" "$slang_version" "$spirv_cross_version" "${efx_names[@]}" <<'PYEOF'
import os, sys
out, efx_out, slang_version, spirv_cross_version = sys.argv[1:5]
efx_names = sys.argv[5:]
def lit(path):
    src = open(path).read()
    return 'str8_lit_comp("%s")' % src.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n"')
stamp = "// slangc %s, spirv-cross %s\n" % (slang_version, spirv_cross_version)
targets = {
    "glsl": {"vs": "%s.vert", "fs": "%s.frag"},
    "hlsl": {"vs": "%s.vs.hlsl", "fs": "%s.fs.hlsl"},
    "msl":  {"vs": "%s.vs.metal", "fs": "%s.fs.metal"},
}
for target, files in targets.items():
    lines = ["// generated by tools/generate-shaders.sh from native/src/shaders/*.slang - do not edit\n", stamp]
    for name in ("rect", "blur"):
        for stage in ("vs", "fs"):
            lines.append("read_only global String8 r_core_%s_%s_%s = %s;\n" % (name, stage, target, lit(os.path.join(out, files[stage] % name))))
    open(os.path.join(out, "core_shaders_%s.h" % target), "w").write("".join(lines))

# effects: R_EffectSources holds every backend's pair; only this build's
# backend gets real sources, the rest stay empty
lines = ["// generated by tools/generate-shaders.sh from native/src/effects/*.slang - do not edit\n", stamp,
         "#if R_BACKEND == R_BACKEND_METAL\n# define EFX_MSL(s) s\n#else\n# define EFX_MSL(s) {0}\n#endif\n",
         "#if R_BACKEND == R_BACKEND_OPENGL\n# define EFX_GLSL(s) s\n#else\n# define EFX_GLSL(s) {0}\n#endif\n",
         "#if R_BACKEND == R_BACKEND_D3D11\n# define EFX_HLSL(s) s\n#else\n# define EFX_HLSL(s) {0}\n#endif\n\n",
         "typedef struct EFX_EmbeddedEffect EFX_EmbeddedEffect;\n",
         "struct EFX_EmbeddedEffect { String8 name; R_EffectSources sources; };\n\n",
         "read_only global EFX_EmbeddedEffect efx_embedded_effects[] =\n{\n"]
for name in sorted(efx_names):
    lines.append("  {\n    str8_lit_comp(\"%s\"),\n    {\n" % name)
    for macro, f in (("EFX_MSL", "%s.vs.metal"), ("EFX_MSL", "%s.fs.metal"), ("EFX_GLSL", "%s.vert"),
                     ("EFX_GLSL", "%s.frag"), ("EFX_HLSL", "%s.vs.hlsl"), ("EFX_HLSL", "%s.fs.hlsl")):
        lines.append("      %s(%s),\n" % (macro, lit(os.path.join(efx_out, f % name))))
    lines.append("    },\n  },\n")
lines.append("};\n#undef EFX_MSL\n#undef EFX_GLSL\n#undef EFX_HLSL\n")
open(os.path.join(efx_out, "effects_embed.h"), "w").write("".join(lines))
PYEOF
echo "generated: $(cd "$out" && ls -1 | tr '\n' ' ')"
echo "generated: $(cd "$efx_out" && ls -1 | tr '\n' ' ')"
