# Core shader language bake-off: Slang and WGSL

[ADR 0015 §5](../adr/0015-window-manager-and-render-backends-are-added-beside-each-other.md#5-where-a-common-shader-language-would-come-in)
stages a common shader language for the core shaders and names a bake-off
between WGSL and Slang. This note records that bake-off for the rect and blur
shaders. Slug glyph curves stay out of it until Slug's licence is checked.
**Outcome:** Slang, for the core shaders and later the effects; recorded in
ADR 0015 §5.

## What was built

- **Sources:**
  - `native/src/shaders/slang/{common,rect,blur}.slang` (Slang modules).
  - `native/src/shaders/wgsl/{common,rect,blur}.wgsl` (WGSL; `common.wgsl` is
    concatenated in front, as the effects prelude is).
  - Both implement the meaning `--shared_ui_diagnostics` checks on every
    backend (#705):
    - radius, border and softness scale with the transform;
    - group opacity is applied once;
    - zero softness gives a hard edge;
    - the blur is Metal's: tap pairs packed for bilinear sampling, a widened
      horizontal pass, and masking only in the vertical pass.
- **Generation:** `tools/generate-core-shaders.sh` writes GLSL 330, HLSL SM5
  and MSL for both languages into `native/src/shaders/generated/`, plus one C
  embed header per language. The paths:
  - **Slang:** HLSL and MSL come straight from `slangc`. GLSL goes
    Slang → SPIR-V → `spirv-cross`, because Slang's GLSL output is Vulkan GLSL,
    not the 330 core OpenGL needs, and OpenGL isn't a supported Slang target.
  - **WGSL:** naga → SPIR-V → `spirv-cross` for all three, as the effects do.
- **OpenGL wiring:** `R_OGL_CORE_SHADERS` builds the OpenGL backend's rect
  and blur programs from either language's GLSL:
  - 0 is hand-written (the default), 1 is Slang, 2 is WGSL;
  - pass it as `WHEELHOUSE_CFLAGS=-DR_OGL_CORE_SHADERS=1 native/build.sh wheelhouse`;
  - the generated shaders read one std140 uniform block each
    (`R_OGL_CoreRectUniforms`, 160 bytes; `R_OGL_CoreBlurUniforms`, 576 bytes).

  D3D11 and Metal are not wired yet. Their generated HLSL and MSL exist and the
  HLSL compiles; see "Not yet done".

Tools used: Vulkan SDK 1.4.363.1 for Linux (`slangc` 2026.17, `spirv-cross`,
`glslang` and DXC for validation; tarball SHA-256
`b7867d8ca658563813af666f34b93db87f1b1c7d9d69d97e924c210c7ba33eb8`, from
`sdk.lunarg.com`), and naga-cli 30.0.1 (`cargo install naga-cli`).

## Results

### Parity

OpenGL (EGL, Mesa software rasteriser under xvfb), `--shared_ui_diagnostics`:

| Shaders | blur mask | rect semantics | coloured corner in case A |
|---|---|---|---|
| hand-written (0) | 0 failures | 0 failures | rgb 0.00 0.03 0.97 |
| Slang (1) | 0 failures | 0 failures | rgb 0.06 0.03 0.97 |
| WGSL (2) | 0 failures | 0 failures | rgb 0.06 0.03 0.97 |

- Both generated sets give identical numbers on every case.
- Their corner interpolation matches what D3D11 and Metal measured on #705,
  where the hand-written GLSL differed slightly.
- The full Linux UI diagnostic list (`ci/run-ui-diagnostics.py --os linux`)
  also passes with each generated set; the border diagnostics go through
  readback.

### Validation of the other targets

- `glslang` accepts every generated GLSL file except Slang's two vertex shaders.
  - Slang's `SV_VertexID` has D3D semantics: it excludes the base vertex. On
    SPIR-V that means `VertexIndex - BaseVertex`, which `spirv-cross` writes
    for GLSL 330 as `gl_VertexID - gl_BaseVertexARB`, behind an extension
    check, or else a `SPIRV_Cross_BaseVertex` uniform that defaults to 0.
  - Mesa compiles it, and with base vertex 0 the result is right. `glslang`
    rejects it in a 330 profile, a validator quirk.
  - `SV_VulkanVertexID` would avoid it, but Slang passes it into HLSL output,
    where it isn't valid.
- DXC (SM 6.0, used as a stand-in for D3D11's SM 5 compiler) compiles all
  eight HLSL files.
- The MSL hasn't been compiled. That needs a Mac.

### Source

| | Slang | WGSL |
|---|---|---|
| Lines of code: common / rect / blur | 21 / 111 / 60 | 20 / 108 / 59 |
| Sharing code | `import common;` (modules) | concatenation in the build script |
| Overloads | yes (`linear_from_srgb` for float and float4) | no |
| Closeness to today's shaders | D3D11's HLSL ports almost line for line | rewritten |
| Constraints met | HLSL output needs a semantic on every varying (`TEXCOORDn`); Slang won't invent them | uniformity analysis: the rect shader must sample its texture unconditionally, since `textureSample` isn't allowed under a per-instance branch |

### Generated code

| | Slang | WGSL |
|---|---|---|
| Lines of code, rect (GLSL / HLSL / MSL, vs+fs) | 236 / 301 / 349 | 218 / 307 / 281 |
| DXIL instructions, rect vs / fs | 159 / 194 | 159 / 182 |
| DXIL instructions, blur vs / fs | 68 / 100 | 68 / 106 |
| Readability | source names kept (`corner_radius_0`), `#line` back to the `.slang` file | decompiled from SPIR-V: temporaries (`_303`, `_337`), structured but terse |

The two are equally fast by this measure. The rect fragment shader's
difference comes from the unconditional texture sample WGSL requires.

### Toolchain

| | Slang | WGSL |
|---|---|---|
| Tools | `slangc` + `spirv-cross` (GLSL only) | naga + `spirv-cross` |
| Size | 44 MB (`slangc` and its libraries) + 3.8 MB | 6.3 MB + 3.8 MB |
| How to get them | Vulkan SDK (one ~370 MB download, both tools inside) or Slang's GitHub releases | `cargo install naga-cli`; `spirv-cross` from the SDK or a distro |
| Already in the tree | no | yes, for the effects |

Only someone changing a shader needs either set: the generated sources are
committed, and `tools/check-generated.py` doesn't regenerate them.

## Assessment

Both work end to end, with identical output and equal code quality. They differ
in authoring and in debugging:

- **For Slang:**
  - The existing core shaders are HLSL, so porting them is mostly copying.
  - Modules replace build-script concatenation, and that matters more as
    shared signed-distance helpers grow (fillet, glyph curves).
  - Its HLSL and MSL keep our names and map back to source lines, so a D3D11
    or Metal compile error points into `rect.slang`.
  - It can also emit WGSL, so a later wgpu or Dawn backend stays open.
- **For WGSL:**
  - It's already in the tree for the effects, and its tools are small and
    install from crates.io.
  - It's what wgpu or Dawn would consume natively.
  - Its uniformity rule is a real constraint on shader structure. It cost
    nothing here, but it will shape glyph and SDF code.

The recommendation was **Slang for the core shaders**, and it was taken.
Effects follow once the core shaders have moved, so there is one shader
language; until then the effects stay WGSL. The cost is a pinned 44 MB tool,
which is needed only to change a shader.

## Not yet done

- Wire the generated HLSL into D3D11 and the MSL into Metal behind the same
  switch, and run `--shared_ui_diagnostics` there. The uniform structs already
  match the layout those backends use, which is Metal's, and the HLSL keeps
  D3D11's input semantics (`POS`, `TEX`, `COL0`…).
- Pin `slangc` and `spirv-cross` where CI or a script can check them.
- Drop the WGSL ports and their generated output once D3D11 and Metal pass
  with the Slang ones; port the effects and their contract to Slang.
- Delete the hand-written rect and blur shaders per backend once the generated
  ones pass there (ADR 0015 §5, stage 2).
- Metal's blur kernel isn't scaled by the backing scale, unlike OpenGL's, so a
  blur on a Retina display is half as wide in points. The generated OpenGL path
  keeps OpenGL's scaling; settle which is meant before Metal moves over.
