# Window-Manager and Render Backends Are Added Beside Each Other and Chosen Per Run

**Status:** accepted 2026-11-01. The first enabling step, the OpenGL
backend's Linux part on EGL behind `wm_native_surface` (X11 only), landed
with this record (#642). The window manager's generated dispatch and a first
native Wayland window manager followed; see "Built so far".

## Context

The native app has one window-manager backend and one render backend per OS,
both chosen at compile time:

- `native/src/window_manager/window_manager_inc.{h,c}` includes the Win32,
  X11 (`linux/window_manager`), Cocoa or stub window manager with `#if`.
  `window_manager.h` declares 81 `wm_*` functions; the `@os_hooks` sections
  are the per-backend ones, the rest are shared helpers in `window_manager.c`.
- `native/src/render/render_inc.{h,c}` picks D3D11, OpenGL, Metal or the stub
  with `R_BACKEND`. `render_core.h` declares 31 `r_hook` (`C_LINKAGE`)
  functions.
- The `.mdesk` files generate the key, format and pass tables
  (`generated/render.meta.h`, `window_manager.meta.h`). The hook lists
  themselves are still written by hand in the headers.

On Linux the app is an X11 client. Under Xwayland, KDE gives X11 clients one
global scale, so per-monitor and fractional scaling need a native Wayland
window manager. Robert wants one Linux binary that can run either. Later he
may want a Vulkan renderer beside OpenGL, and DX12 beside D3D11 on Windows.
If the drawing primitives grow (GPU text with Slug, inner rounded corners,
better 3D), a common shader source language becomes likely.

Before this change, the GL renderer's Linux part used GLX, read
`lnx_wm_state` and `LNX_WM_Window` directly, and acked `_NET_WM_SYNC_REQUEST`
(#608) by calling `lnx_wm_window_presented` after `glXSwapBuffers`. A Wayland
window manager could not have been added without forking that file.

## Decision

**Keep options open.** A new backend is added beside the existing ones and
chosen per run. No backend replaces another by decree. A candidate becomes a
default only after it has passed the bar under "How a candidate proves
itself", and that change of default is a separate, recorded decision.

wgpu and Dawn could be candidates, but only as `r_*` backends that prove
themselves on real workloads beside D3D11, Metal, OpenGL and any native
Vulkan. They are never adopted up front as a wholesale switch. A layer that
covers several OS APIs at once is exactly the kind of choice that needs
measuring per OS. Skia sits a level above: it would replace the draw layer
(`dr_*`, `draw/`), not be an `r_*` backend. It is not ruled out, and is worth
a look only if its features become needed anyway.

### 1. One dispatch mechanism for both layers

- **The hook lists move into `.mdesk`.** `window_manager.mdesk` gains a
  `WM_HookTable` and `render_core.mdesk` an `R_HookTable`, each row giving
  return type, name and parameters. Metagen generates:
  - a backend table type (`WM_Backend`, `R_Backend`) with one function
    pointer per hook;
  - one filled table per compiled-in backend, pointing at that backend's
    prefixed functions (`wm_x11_window_open`, `wm_wayland_window_open`,
    `r_ogl_window_equip`, `r_vk_window_equip`);
  - the public `wm_*` and `r_*` functions every caller already uses.
- **Exactly one backend compiled in:** each public function is a direct call
  to that backend's function, so the unity build inlines it. This costs
  nothing over today, and is what every platform build produces until a
  second backend exists.
- **More than one:** each public function calls through the table chosen at
  startup. Handles (`WM_Window`, `R_Handle`) stay opaque to their owning
  backend. Only one backend per layer is live in a process, and it never
  changes while the process runs.
- **Choosing at startup.** A flag (`--window_manager:x11|wayland`,
  `--render_backend:opengl|vulkan|d3d11|d3d12|metal`) wins over a setting,
  and the setting over the platform's fallback order. The first fallback
  order is the existing backend first (X11 then Wayland, OpenGL then Vulkan,
  D3D11 then DX12). Changing it is the separate decision mentioned above.
- **Fallback on failure.** Each backend's init returns success instead of
  calling `abort_self`. The window manager is chosen first. Then the first
  render backend in order that accepts the window manager's native surface
  kind (see 2) and initialises is used. If none does, the window manager
  shuts down and the next one is tried. The fatal message appears only when
  every pair has failed, and it lists each pair's reason. A backend that
  needs system libraries the others don't (libwayland-client, libxkbcommon,
  libwayland-egl, libvulkan) loads them with `dlopen` in its init. A missing
  library is then an init failure that falls back, not a binary that won't
  start.
- *Built for the window manager.* The hooks are listed in `WM_HookTable` in
  `window_manager.mdesk`. They are still declared, with their comments, in
  `window_manager.h`, and the compiler checks the two against each other in
  every build with several backends. Rather than renaming functions by hand,
  each backend's source is compiled with `WM_HOOK_NAME` set to its prefix, so
  its `wm_window_open` becomes `wm_x11_window_open`; a single-backend build
  defines none of this. `wm_init(CmdLine *)` returns whether the window
  manager started, and on Linux it does the choosing (`--window_manager:` or
  `WHEELHOUSE_WINDOW_MANAGER`, then X11 and Wayland in that order). There is
  no setting yet. The render layer gets the same treatment when it gains a
  second backend in one build.

### 2. The seams every backend shares

- **`wm_native_surface(window)`** returns a tagged union: X11 display,
  window and visual; Wayland `wl_display` and `wl_surface`; `HWND`; or
  `NSView` and `CAMetalLayer`. Called with a zero window, it returns the
  connection alone, which a renderer needs before the first window opens.
  A render backend reads windows only through this and `window_manager.h`.
  It never includes a window manager's own header. Each render backend lists
  the surface kinds it accepts: GL through EGL accepts X11 and Wayland, GLX
  only X11, Vulkan all four (Cocoa through MoltenVK). Per-API wrappers belong
  to the render backend: `wl_egl_window` for EGL on Wayland,
  `VkSurfaceKHR` for Vulkan.
  *Built here for X11 and the stub. Win32 and Cocoa gain theirs when a render
  backend there first reads it.*
- **`wm_window_presented(window)`** is called by the frame loop
  (`shell_core.c`, and the presentation diagnostic) after
  `r_window_end_frame`. It lets a window manager tell the compositor that the
  frame for its last configure exists. X11 sets the `_NET_WM_SYNC_REQUEST`
  counter there. Win32 and Cocoa have nothing to do. On Wayland it is where an
  interactive resize stops holding back the next configure. The ack itself
  (`xdg_surface.ack_configure`) goes out when the window manager hands the
  new size to the shell, before the frame's commit.
  *Built here: the GLX call is gone.*
- **Frame pacing stays `r_window_request_frame` / `r_window_frame_ready` /
  `r_window_frame_pending`.** The shell sees one admission API. Where
  admission comes from the swap chain, the render backend answers it: Metal's
  display link, D3D11's waitable swap chain. Where it comes from the surface,
  the render backend forwards it to the window manager: Wayland's
  `wl_surface.frame`. A frame callback is double-buffered surface state, so
  the window manager can request it any time before the commit that
  `eglSwapBuffers` or `vkQueuePresentKHR` makes. EGL on Wayland then runs with
  `eglSwapInterval(0)` and never blocks in the swap. X11 has no admission
  signal and stays always ready.
- **Shaders and assets are compiled per backend and committed.** Today the
  core shaders are written by hand per backend (GLSL 330 in
  `render_opengl.mdesk`, HLSL in `render_d3d11.mdesk`, MSL for Metal). Effects
  are already single-source: WGSL, through naga to SPIR-V, through
  spirv-cross to MSL, GLSL 330 and HLSL SM5 (`native/build.sh effects`), with
  the outputs committed. A new backend adds its own outputs the same way, so
  a normal build needs no shader toolchain. Vulkan would keep the SPIR-V that
  the effects step now deletes. Its core shaders are the trigger in
  section 5.

### 3. The Wayland semantic gaps

These are decisions for the Wayland window manager. None changes X11
behaviour.

- **No global window positions.** `wm_rect_from_window` returns the size at
  origin (0,0). Its one caller (`shell_core.c`, "commit position") saves the
  position to the window's config, so on Wayland the shell skips saving and
  restoring positions; sizes and maximized and fullscreen state still
  round-trip. `wm_window_set_monitor` becomes
  `xdg_toplevel.set_fullscreen(output)` for a fullscreen window and does
  nothing otherwise, since the compositor places windows.
  `wm_window_focus` uses `xdg-activation-v1` with the token from
  `XDG_ACTIVATION_TOKEN` at startup, or one tied to the last input serial.
  Without a token the compositor decides; it may only flag the window for
  attention. A `wm_capabilities()` query reports which of these a backend
  can do, so the shell can hide what it can't offer instead of failing
  silently.
- **Move, resize and decorations.** The custom-border hit test that sends
  X11 presses to `_NET_WM_MOVERESIZE` (`lnx_wm_moveresize_code_from_pos`)
  moves to shared code. On Wayland the same codes map to
  `xdg_toplevel.move` and `xdg_toplevel.resize(edges)`, using the press's
  serial. A right-click on the title bar opens `xdg_toplevel.show_window_menu`.
  The app already draws its own title bar, so a custom-border window asks
  `xdg-decoration` for `client_side`. Without `xdg-decoration` (GNOME) the
  app is client-decorated anyway. If the compositor insists on `server_side`,
  `wm_window_should_draw_custom_title_bar_controls` returns 0 and the shell
  draws no second title bar, just as #608 removed the doubled one on X11.
  Maximized, fullscreen, activated and tiled come from the `xdg_toplevel`
  configure states, not from properties.
- **Fractional scale.** `wp_fractional_scale_v1` gives the preferred scale in
  120ths. The window draws a buffer of `round(logical × scale)` and sets
  `wp_viewporter`'s destination to the logical size. This follows the Win32
  model: the client rect is in buffer pixels, `wm_dpi_from_window` is
  `96 × scale`, `wm_backing_scale_from_window` is 1, and pointer positions
  are scaled from surface-local coordinates. Without the protocol, the
  window uses `wl_surface.preferred_buffer_scale` (wl_compositor v6), or
  else `wl_output.scale`, with `wl_surface.set_buffer_scale`.
- **Clipboard and primary selection.** `wl_data_device` serves CLIPBOARD and
  `zwp_primary_selection_device_v1` serves PRIMARY, with the same
  text-and-HTML model the X11 code keeps (`clipboard_text`,
  `clipboard_html`, `selection_text`). Offering a selection needs the serial
  of a recent input event, so the window manager keeps the last one. Reading
  stays synchronous for the shell: the window manager reads the offer's pipe
  with a bounded wait, as the X11 path waits for `SelectionNotify`. Without
  the primary-selection protocol, `wm_set_selection_text` and
  `wm_get_selection_text` do nothing.
- **Text input.** Keys come from `wl_keyboard`, with the keymap from
  libxkbcommon. With `text-input-v3`, the window enables it while a text
  View has focus, turns `commit_string` into `WM_EventKind_Text`, and reports
  the caret with `set_cursor_rectangle`. Preedit display is out of the first
  cut, as it is on X11 today (the XIC is `XIMPreeditNothing`). Both platforms
  can get it together later.
- **Cursors.** `wp_cursor_shape_v1` maps `WM_Cursor` one to one (default,
  text, ew-resize, ns-resize, nwse-resize, nesw-resize, all-scroll,
  pointer, not-allowed). Without it the window manager draws the cursor from
  the theme through libwayland-cursor, at the window's scale.

### 4. How a candidate backend proves itself

- **Pixel comparison.** Before this work, the raster-baseline check
  (`uishell_check_raster_baselines`) proved glyph coverage per font provider,
  and the readback diagnostics (`r_pass_list_readback` in the border,
  Jackstay and terminal-glyph checks, `tools/validate-terminal-fixture-ppm.py`)
  proved what one backend draws. A `--render_parity_diagnostics` will build
  on both. It draws a fixed set of pass lists: rects with per-corner radii,
  borders and softness; sRGB blending; R8 and RGBA glyph pages; BGRA Jackstay
  frames; blur; the 3D geometry pass; every WGSL effect. It reads each back
  and writes PPMs. A script compares the candidate with the incumbent on the
  same machine: at most 2/255 per channel on at least 99.9% of pixels, with
  a diff image when it fails. Readback doesn't cover the swap-chain path. So
  the parity check also captures the presented window: same profile, same
  size, in a nested compositor, as this ADR's spike did. All 15 Linux UI
  diagnostics, and the checks for each gap decision above (clipboard round
  trip with another client, the resize ack, a scale change), must pass under
  the candidate window manager.
- **Performance, on real workloads.** The candidate is measured on frame
  build and submit time (`frame_metrics`), present-to-present jitter (the
  presentation diagnostic), idle CPU (#605 is why), time to first frame,
  resident and GPU memory, frames per interactive resize step, the
  10k-line Rich Text timing (`RICH_TEXT_BENCH`), and the preview budgets.
  The machines are feta (NVIDIA RTX 4090), one Mesa machine (AMD or Intel),
  software (llvmpipe, lavapipe, WARP) as the CI floor, and Apple silicon
  where Metal is the incumbent. The results go in a note beside this record.
  The bar is no worse than the incumbent beyond noise on any of these, and
  better on whatever motivated the candidate.
- **CI cost per backend.** A backend compiled into the default build costs
  little extra compile time, since the build is one unity compile. Its
  diagnostics cost more. The default pair runs the full UI diagnostics list,
  as now. Each extra pair runs the parity check, presentation and a smoke
  subset, a few minutes per OS job. Its full list runs on PRs that touch
  its paths, and on a schedule. Wayland in CI needs a headless compositor:
  `weston --backend=headless` is the lightest on Ubuntu runners, and
  `kwin_wayland --virtual` is what this spike used locally. Vulkan uses
  lavapipe (`mesa-vulkan-drivers`). DX12 uses WARP, which the Windows
  runners already have.

### 5. Where a common shader language would come in

This is staged, not decided.

- **Now.** Effects are WGSL. Core shaders are hand-written per backend. Every
  primitive in ADR 0007 is a rect instance, so no core shader has to change.
- **Trigger.** Whichever comes first: a backend that needs the core shaders
  in a new form (Vulkan SPIR-V, DX12 SM6), or a primitive that would
  otherwise be written three to five times (Slug glyph curves, the concave
  fillet ADR 0007 defers, lit 3D).
- **Stage 1, when triggered.** Port the rect shader to the common source and
  generate every backend's version from it, committed like the effects.
  Pass the parity check against the hand-written one before it is deleted.
  This is a bake-off between two candidates:
  - **WGSL**, already in the tree through naga. It is what wgpu or Dawn would
    consume if they ever become candidates. It has no modules or generics.
  - **Slang**, which has modules, generics, and outputs to SPIR-V, DXIL,
    HLSL, MSL, GLSL and WGSL. The toolchain is heavier.

  The bake-off ports the rect shader and one Slug-style glyph shader in
  each, and compares the readability of the source, the generated code's
  speed, and the toolchain's cost in CI. Slug's licensing is checked
  before it is adopted.
- **Stage 2.** The remaining core shaders move over one at a time, each
  behind the parity check.

## Consequences

- A future reader will find that render backends never include a window
  manager's header. They see windows only through `wm_native_surface`. The
  OpenGL backend's Linux part follows this from now on, in both its EGL and
  GLX forms.
- The OpenGL backend's OS layer now gets back the handle its own
  `r_ogl_os_window_equip` returned. Before, `r_window_begin_frame` and
  `r_window_end_frame` passed the top-level window handle, which GLX and WGL
  never looked at. The EGL code that came from upstream depended on getting
  it, and failed every swap with `EGL_BAD_SURFACE`.
- EGL is the default on Linux. GLX stays buildable with
  `-DR_OPENGL_LINUX_BACKEND=0` as a fallback until EGL has run for a while on
  real machines. Under this record GLX could also become a run-time choice;
  that needs no further decision.
- Saved window positions mean nothing on Wayland. The shell must keep
  working with origin-less window rects, and asks `wm_capabilities()` before
  saving or restoring a position.
- Wayland support grows the Linux dependencies (libwayland-client,
  libwayland-egl, libxkbcommon, generated protocol code). They are loaded
  with `dlopen`, so an X11-only system still runs the same binary.

## Spike: OpenGL on EGL behind `wm_native_surface`

No behaviour change was intended, and none was found.

- `wm_native_surface` (X11 and stub) and `wm_window_presented` (all four
  window managers) are added. The `_NET_WM_SYNC_REQUEST` ack moves from the
  GLX swap to `wm_window_presented`, called after `r_window_end_frame`.
- `render_opengl_linux_egl.c`, which came from upstream unused, is now the
  default. It gets its display from `eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR)`
  and picks the config whose native visual is the root's, preferring one
  that can also make pbuffers. It keeps #606's NVIDIA fix: a 1×1 pbuffer is
  current until the first window, and again after the current window's
  surface is destroyed. #606's real texture formats live in the shared GL
  code and are unchanged.
- Window surfaces are destroyed on unequip. The upstream EGL code leaked
  them.

Verified on feta (RTX 4090, driver 615.78.08) in an offscreen
`kwin_wayland --virtual --xwayland`. Xwayland there runs on the NVIDIA GPU
for both GLX and EGL. Each check was repeated with llvmpipe
(`LIBGL_ALWAYS_SOFTWARE=1`, GLVND pinned to Mesa):

- Window captures of the pre-change GLX build, the new GLX build and the
  EGL build, from the same profile before and after five resizes, are
  identical pixel for pixel on both GPUs.
- A `_NET_WM_SYNC_REQUEST` sent to the window sets the counter to the
  requested value, including values above 2³², in all three builds.
- `ci/run-ui-diagnostics.py --os linux` passes all 15 diagnostics on the EGL
  build, with an `xvfb-run` stand-in that execs its arguments, on llvmpipe
  and on the NVIDIA GPU.

## Built so far

- **#642:** the spike above.
- **Window-manager dispatch** (section 1). Linux builds X11 and Wayland in
  one binary; Windows, macOS and the stub stay single-backend, with direct
  calls.
- **A first Wayland window manager** (`linux/window_manager/wayland`),
  chosen with `--window_manager:wayland`. It loads libwayland-client and
  libxkbcommon with `dlopen`, and its protocol code is generated by
  `native/build.sh wayland_protocols` and committed. The client headers are
  vendored in `third_party/wayland`, so a build needs no Wayland packages. It
  has `xdg-shell` windows with `xdg-decoration`, the custom border's
  move, resize, window menu and double-click maximize, keyboard input through
  xkbcommon with client-side key repeat, wheel and touchpad scrolling,
  `wp_cursor_shape_v1`, outputs as monitors, and frame pacing by
  `wl_surface.frame` (`wm_window_frame_ready` and `wm_window_frame_pending`,
  forwarded by the GL backend). The EGL layer takes Wayland surfaces through
  `wl_egl_window`, from its own `dlopen` of libwayland-egl.
- **Not built yet** (the rest of section 3): fractional scale with
  viewporter, the clipboard and primary selection shared with other clients
  (copy and paste stay inside the process for now), `text-input-v3`,
  `xdg-activation`, and a themed-cursor fallback. Until those land, X11 stays
  first in the fallback order. Its CI smoke run in a headless compositor is
  also still to come. Two shared-UI checks drive X11 directly, so they run
  only under X11.

