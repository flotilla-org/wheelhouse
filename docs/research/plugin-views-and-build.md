# Research: plugin views and the build (speculative)

Status: research notes, 10 October 2026. Nothing here is decided.

## c) Agent-written views that Wheelhouse can use safely

### What exists

- **The Jackstay view is consumer-only.** It takes CPU RGBA frames, or D3D11
  shared textures on Windows, and sends cooperative input back
  (`src/jackstay/`, `uishell_jackstay.c`). The only producers in this repo are
  test tools (`tools/jackstay-frame-source.c`).
- **Jackstay's C ABI** (`capture_transfer.h`, v0.14, not yet stable):
  - Producers are single-stream (`ft_cpu_producer_*`) and publish CPU BGRA8 or
    RGBA8, either as a copy or written in place.
  - Bootstrap sets up input and affordances. Input authority is granted
    separately from media.
  - There is no registry: a stream is found only by its endpoint name.
  - The producer lifecycle toolkit (`jackstay-producer`) exists for Rust only.
  - GPU frames are supported (IOSurface, dmabuf, D3D11 surface pools via
    `NativeArenaProducer`). The C ABI exposes the GPU *consumer* side
    (`ft_native_attach_*`, D3D11 acquisition), so a C host publishing its own
    GPU textures is a Wheelhouse/ABI integration gap rather than a missing
    capability. Some streaming support exists; the cross-host bridge lives in
    Porthole.
- **SDL programs:** Jackstay has no helper that turns an SDL app into a
  producer. Katzensteg's LD_PRELOAD `jackstay-source` covers this, including
  input (`scripts/katzensteg/test_jackstay*.py`).
- **Luchs** shows HTML as a Jackstay source, with input, affordances and
  reload on file change (`--watch`). It runs on macOS only (WKWebView); Linux
  has no renderer.
- **flotilla-viz** (Godot) runs on fixture data only. Its DESIGN.md already
  expects to show terminals and other content through Jackstay. Consuming
  Jackstay would need a GDExtension.
- **Nothing in any of these repos** covers plugins, wasm, dlopen of views or
  hot reload.

### Wheelhouse layering, and where a library would cut

| Layer | Lines | Split |
|---|---|---|
| base (with OS), window_manager, render, font_provider, font_cache, draw, ui | ~32k | clean library cut |
| config, content, text, mdesk, eval, eval_visualization | ~20k | RAD-generic, only needed by shell |
| shell (`rd_*`), uishell (views, terminal, sidebar), jackstay, ingress | ~61k + 247k generated | app |

- **What a clean cut needs:** `base_entry_point.c` checks for app headers at
  compile time (`SHELL_CORE_H` and others), and the `entry_point` and `frame`
  hooks would have to become app-supplied.
- **Global state:** render and font are per-process singletons. UI state can
  already be switched (`ui_state_alloc`, `ui_select_state`).
- **The View interface is `ui(E_Eval, Rng2F32)`.** It is registered through an
  X-macro and tied to eval, cfg IDs and `rd_state`, so it is not a plugin
  boundary as it stands. The terminal view is about 16k lines bound to `rd_*`.
- **Offscreen surfaces already exist.** `UI_BoxFlag_RenderToSurface` uses
  `dr_surface_begin` and `dr_surface_end_cached`, backed by
  `r_tex2d_alloc_render_target` (`shell_core.c:2289-2330, 8133, 8398`), and
  `r_pass_list_readback` reads them back. This is where Jackstay output and
  in-process plugin surfaces would both hook in.
- **A UI render pass is plain data:** `R_BatchGroup2DList` of `R_Rect2DInst`
  with texture handles.

### Options

**(i) Out-of-process producers (SDL, Luchs, anything) shown in a Jackstay view.**

- Safety comes from process isolation, a same-user peer check, and input
  granted separately from media. This is already the supported route.
- The gaps are tooling, not architecture:
  1. A small C helper library, such as `jackstay_sdl.h`. It would read the SDL
     renderer or a surface into `ft_cpu_producer_publish`, and turn
     `jackstay_input` events into SDL events. An agent should be able to write
     a view in about 50 lines without learning bootstrap and leases.
  2. **Wheelhouse owns the producer's lifecycle.** A view spec of
     `{command, endpoint, cwd, env}` that Wheelhouse launches, restarts and
     stops with the view. Without it, "dynamically used" means manual setup.
     This is a small, local form of Jackstay's planned
     publication/registration split.
  3. Affordances (preferred size, focus) should pass through to the view.
- Costs: one copy per frame, and a process per view. Programs don't look
  native, because they get no Wheelhouse fonts or theme unless the theme is
  passed to them, for example as env or affordance data.

**(ii a) Library split plus Wheelhouse as a Jackstay producer (a "view server").**

- **Jackstay output doesn't depend on the split.** It is a separate,
  smaller step: render a View to a surface, read it back, and call
  `ft_cpu_producer_publish`.
  - Use one endpoint per exported view, or one listener that picks the stream
    after a short exchange (Porthole's pattern, using ABI 0.11 local
    connection read/write).
  - Input received from consumers is routed to that view just like local
    input.
  - It is useful in Wheelhouse itself: detached or mirrored views, flotilla-viz
    showing live terminals, recording, and remote viewing through Porthole.
- **Costs:**
  - GPU→CPU readback per changed frame. The cached-surface hash gives damage
    skipping for free.
  - A C port of the producer lifecycle that `jackstay-producer` does for Rust.
  - Later, an app-texture export ABI in Jackstay (IOSurface, dmabuf, D3D11
    shared handles) to drop the readback.
- **The library split itself** is the ~32k-line cut above. It is cheap for the
  lower layers, and its main value is small native C programs that draw like
  Wheelhouse: same fonts, theme and ui widgets. Those programs would still
  show up in Wheelhouse through (i). Splitting the shell or terminal View into
  a library is a big refactor, gated on decoupling it from RAD's eval and cfg
  model.

**(ii b) Wasm plugins running in process.** Candidate boundaries, from
cheapest to richest:

1. **Pixel buffer.** The plugin fills an RGBA buffer, and the host uploads it
   as it does a Jackstay CPU frame. Its contract matches (i), so the same
   plugin could also run out of process. The plugin gets nothing native.
2. **Draw list, imported from the host.** The plugin calls host functions
   shaped like `dr_rect`, `dr_text(font_tag, …)`, `dr_img(handle)` and
   `dr_surface_*`. The host checks the calls and turns them into
   `R_Rect2DInst` batches inside the view's surface. The plugin gets native
   fonts, theme colours and atlases without owning any GPU state. **This
   looks like the sweet spot.**
3. **UI-tree level.** Host imports for `ui_*` box building, so the plugin gets
   real widgets, layout, hit-testing and the theme. The surface is richer but
   the interface is much bigger, and ui's thread-local state would need a
   per-plugin `UI_State`.
4. **Compile the C layers into wasm** (render to a software target). Rejected:
   it duplicates fonts and the renderer in every plugin.

- **Runtime:** WAMR, wasm3 (an interpreter, small and easy to embed) or the
  wasmtime C API. The C ABI choice works with all three.
- **Safety:** memory isolation; imports are capabilities, so there is no file
  or network access unless the host grants it; fuel or epoch interruption stops
  a plugin that hangs a frame.
- **Hot reload comes for free:** swap the module and keep the view's state
  outside it, or serialise it across the swap.
- **Plugin language:** agents can write C, Zig or Rust and compile with
  `zig cc -target wasm32-wasi`, wasi-sdk or `cargo --target wasm32-wasip1`.

### A unifying shape

Define one **View Source contract**: pixels or draw-list frames, plus input
events, plus affordances, using Jackstay's input and affordance vocabulary.
Implement it twice:

- **Remote:** a Jackstay endpoint, as in (i).
- **Local:** a wasm instance, as in (ii b).

Wheelhouse's *outbound* Jackstay producer (ii a) then exports any View,
whether native, wasm or remote. So (ii a) and (ii b) compose rather than
compete.

**Suggested order:**

1. **(i) tooling:** a C/SDL producer helper, and a view spec with a lifecycle
   that Wheelhouse launches. This is the cheapest and unblocks agents now.
2. **(ii a) outbound producer** for one View type (a terminal), consumed by the
   SDL reference viewer and then flotilla-viz.
3. **(ii b) wasm with a draw-list boundary**, as a spike: one hard-coded view
   type that loads a `.wasm`.
4. **Library split** of base through ui, when a native C program actually
   needs Wheelhouse's look.

## d) Build: zig build compared with the current scripts

**Measured** on Linux with 32 cores and clang 23; a debug build of
`bash build.sh wheelhouse` with nothing changed:

| Step | Time |
|---|---|
| Whole run | 9.0 s |
| clang `-c uishell_main.c` (unity build, ~131k lines) | 8.9 s, 1.8 GB |
| Front end only (`-fsyntax-only`) | 6.3 s |
| `-g0` saves | 0.75 s |
| Link | 0.07 s |
| Cargo no-ops for all three siblings | about 0.1 s |
| Python steps | about 0.03 s |
| `zig cc` 0.16 (its bundled clang 21), same flags + `-fno-sanitize=undefined` | 13.3 s (≈50% slower) |
| `zig cc` with nothing changed (its cache) | 0.05 s |

**Follow-up trace (`-ftime-trace`):** about 75% of the compile is embedded
fonts. `@embed_file` in `shell.mdesk` generates hex arrays in `shell.meta.h`:
about 15.6 MB of fonts, of which NotoColorEmoji alone is 9.9 MB and 6.6 s.
With those arrays stubbed out:

- the full compile drops from 8.7 s to 2.1 s;
- `-fsyntax-only` drops from 6.3 s to 0.54 s;
- the object file shrinks from 22.8 MB to 7.0 MB.

Moving the fonts to C23 `#embed`, `.incbin` in a separate object, or loading
them at run time is the single biggest build win, and needs no build-system
change. After that the backend (about 1.1 s) dominates. About 19% of
per-function time goes to `*_diagnostics.c`, which ships in the main binary.

**Conclusions:**

- **The edit loop is bounded by the one unity file.** Changing the build
  tool alone cannot help real edits. Only these do:
  - **Skip when nothing changed.** `build.sh` always recompiles; 9 s → ~0.2 s.
  - **Split the unity build into two units:** a library unit (base through ui,
    rarely edited) and an app unit (shell and uishell). This speeds up edits
    to the app.
- **Against zig build:**
  - Zig ships a new minor version with breaking `build.zig` API changes, and
    ghostty already ties cleat to particular zig versions (0.15.2 locally,
    0.16 in CI).
  - The bundled clang is slower for this file.
  - Windows defaults to `cl` with MSVC-ABI Rust DLLs; with zig, MSVC needs
    the `-msvc` target and drops `cl`, natvis and VS debugging.
  - On macOS, `dsymutil` and codesign stay as shell steps.
- **For zig build:**
  - One graph for the library, the app, plugins and wasm.
  - wasi-libc is bundled.
  - Content-hash caching.
  - Zig is already a CI dependency.
- **Recommendation:**
  - Replace `build.sh` and `build.bat` with a single `build.py`. Python is
    already required everywhere, and the tests, including
    `test-build-failures.py`, are Python. Keep the target names, add skipping
    unchanged builds by depfile/hash, then split into library and app units.
  - Use `zig cc` (or wasi-sdk) **only** as the compiler for the wasm32 plugin
    target.
  - Look at `build.zig` again near zig 1.0, or once targets multiply.
  - A nob.h-style C builder is a plausible alternative to Python that fits the
    codebase's style.
