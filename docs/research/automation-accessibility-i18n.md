# Research: automation, accessibility and i18n (speculative)

Status: research notes, 10 October 2026. Nothing here is decided. The decisions
are being made on the map "Wheelhouse's UI is machine-readable and
locale-ready".

## a) Accessibility through AccessKit

### What exists

- **No accessibility code at all.** The only mention is the roadmap item
  "Accessibility #92" (`docs/roadmap.md`).
- **Existing surfaces an agent could already use:**
  - **Local HTTP ingress.** The in-repo Rust crate `src/ingress/lib.rs`, with
    C header `src/ingress/ingress.h`, serves `/v1/health`,
    `/v1/metadata/patch` and `/v1/observed/workdirs`
    (`docs/protocol/pm-connect.md`). It starts only when `--andamento_socket`
    is given (`uishell_main.c:235`), and the UI thread polls it
    (`uishell_sidebar.c:5724`).
  - **Named commands.** `uishell_cmd(name, regs…)` (`shell/shell_core.h:1334`)
    already gives a semantic way to act.
  - **In-process diagnostics.** `rd_state->frame_diagnostic` hooks walk
    `UI_Box` trees, but only from compiled-in tests. There is no runtime dump
    of the UI tree.

### AccessKit state

`accesskit` 0.25.1 is current, with the unix adapter at 0.24.0, windows at
0.35.1 and macos at 0.27.1, all released 25 September 2026. `accesskit-c` ships
CMake/Meson packaging and provides:

- `accesskit_{unix,windows,macos}_adapter_new`, plus the subclassing variants;
- `*_update_if_active`, which builds the tree only once an assistive tool is
  connected;
- `accesskit_windows_adapter_handle_wm_getobject`;
- callbacks for activation, actions and deactivation.

### Integration options

- **A: accesskit-c.** Adds a CMake build step, a library to ship, and an FFI
  call per node property.
- **B (preferred): a small Rust shim crate**, built the way
  `wheelhouse_ingress` is: a generated `Cargo.toml` in
  `tools/prepare-andamento-build.py`, linking in `build.sh`, and DLLs copied
  by `build.bat`.
  - C hands the shim a flat array of box records (id, parent, role, name,
    rect, flags, value) and gets back an action queue. The shim works out what
    changed and calls the adapters.
  - The ingress crate is currently built inside the Andamento build. The shim
    probably wants its own crate.

### Where adapters attach (one per `WM_Window`)

| Platform | Window creation | Hook |
|---|---|---|
| Linux (Xlib) | `wm_window_open`, `linux_window_manager.c:162` | The AT-SPI adapter runs its own D-Bus thread and doesn't depend on X. Push window bounds to it on ConfigureNotify. Its callbacks arrive off the UI thread, so wake the loop with `wm_send_wakeup_event`. |
| macOS | `wm_window_open`, `mac_window_manager.c:886`; we own `MAC_WM_ContentView` (`:85`) | Use the subclassing adapter, or override `accessibilityChildren`, `accessibilityHitTest` and `accessibilityFocusedUIElement` |
| Win32 | `CreateWindowExW`, `win32_window_manager.c:1150` | Add `case WM_GETOBJECT` to `w32_wm_wnd_proc` (`:343`) |

On Windows, activation runs synchronously inside `WM_GETOBJECT`. The shim
must keep the last tree (or a placeholder holding only the window) ready to
hand back.

### Building the tree

- **IDs.** `UI_Key` maps onto `accesskit_node_id`, which is a u64.
  - About 81 call sites build boxes with a zero key. Those, and boxes that get
    a zero key because of a duplicate (`ui_core.c:2620`), need IDs derived from
    their parent and child index.
  - Derived IDs are unstable, so those nodes must not be focusable.
- **Roles.** `UI_Box` (`ui_core.h:387`) has flags but no role. It needs a
  `role` field, an equip API, and annotations in the widget builders. For
  unannotated boxes, fall back on these rules:
  - clickable with text → Button;
  - text only → Label;
  - scrolls → ScrollView;
  - layout-only boxes are dropped, and their children reparented.
- **Name, bounds and state.**
  - Name: the display part of the box's string.
  - Bounds: `box->rect`, scaled for the backing store on macOS.
  - Disabled and focus state come from the box flags.
  - `IgnoreInteraction` hides the subtree.
- **Gaps:**
  - Custom-drawn content is opaque.
  - The terminal needs a Document node with text runs built from the Cleat
    cell cache (phase 3).
  - Jackstay views are opaque.
- **Cost.** Walk the tree after `ui_end_build` (`shell_core.c:7945`), only
  while an assistive tool is connected. The shim hashes each node and sends
  only the nodes that changed, so most frames send nothing. Round the bounds
  of animating boxes to avoid churn.

### Actions

- **Queueing.** Queue requests behind a mutex, wake the loop, and drain the
  queue in `ui_begin_build`.
- **Click.** A synthetic press that `ui_signal_from_box` (`ui_core.c:3037`)
  turns into `UI_SignalFlag_KeyboardPressed`. Faking mouse events would break
  under occlusion and while dragging.
- **Focus.** Set the focus-hot key.
- **Scroll.** Adjust `view_off_target`.
- **Set value.** Send a text `UI_Event`.
- **Custom actions.** Map them to `uishell_cmd`.

### Gating

- **Build time:** a `WHEELHOUSE_ACCESSKIT` flag, like `WHEELHOUSE_JACKSTAY`.
- **Run time:** on by default, since lazy activation costs nothing until an
  assistive tool connects, with a config switch to turn it off.

### Size and phases

Roughly 1.5–2.5k lines in total:

| Part | Rough lines |
|---|---|
| Rust shim | 400–600 |
| C bridge | 600–900 |
| Platform hooks | 50–100 each |
| Role field and API | ~100 |
| Widget and shell annotations | 200–400 |

1. **Minimum version:** the window, tab strips, tabs, panels, sidebar rows and
   buttons, with Focus and Click.
2. **Phase 2:** line edits, scroll regions, menus, the command palette, and
   live regions for toasts.
3. **Phase 3:** terminal text.

Risks:

- IDs from zero-keyed boxes are unstable.
- Roles drift as new widgets are added without them.
- macOS flips the y axis.
- Windows activates the adapter synchronously.
- Terminal text can be large.

### Computer-use agents

- **What the accessibility tree gives them:** stock agents get roles, names,
  bounds and actions through AT-SPI, UIA and AX.
- **What else helps:** expose stable IDs as AccessKit `author_id`, give
  icon-only buttons descriptive names, and map custom actions to named
  commands.
- **The cheaper, predictable route:** a semantic tree and command routes on
  the existing ingress endpoint. They behave the same on every platform, need
  no OS accessibility permission, and can serve test harnesses.

## b) Internationalisation

### Current state: none

There are no locale settings, no translation hooks and no gettext. All strings
are English.

- **Already in tables (about 520 strings):**
  - commands in `src/uishell/uishell_commands.h` (31) and
    `src/shell/shell_commands.h` (98), each with a display name and a
    description, where `name` is already a stable ID;
  - 61 settings in `src/uishell/uishell_meta.h`;
  - `UIShell_VocabTable` in `uishell.mdesk`, which has an unused plural
    column;
  - theme presets in `shell.mdesk`;
  - 143 key names in `window_manager.mdesk`.
- **Inline literals (about 250–350 strings):** clustered in `uishell_views.c`,
  `shell_core.c`, `uishell_sidebar.c`, `ui_basic_widgets.c`,
  `uishell_jackstay.c`, `uishell_local_groups.c` and `uishell_hover_cards.c`.
  Some build text in ways that can't be translated:
  - sentences assembled from fragments (`shell_core.c:1729-1741`);
  - fixed word order ("Line: %I64d, Column: %I64d");
  - naive plurals ("%I64d lines");
  - English units ("%I64us ago").
- **Strings that stay English:** the debug HUD, event names, renderer
  errors, and about 1,000 lines of diagnostics.

### Hazards

- **Identity comes from label text.** `ui_key_from_string`
  (`src/ui/ui_core.c:13-57`) hashes the label unless it contains `###`, so
  translating a label changes its key. Hover, focus and animation state are
  lost, and two translations can collide.
- **Themes are matched by display name** (`shell_core.c:9165`).
- **Eight diagnostic tests compare English display strings.**
- **Rust strings are unaudited.** Strings from Andamento and the sidebar core
  were not checked.

### Text pipeline

| Area | State | Blocks |
|---|---|---|
| UTF-8 | Decoded correctly (`font_cache.c:663-676`) | — |
| Shaping | None. Each codepoint is rasterised alone (`font_cache.c:641-780`; `FT_Load_Char` in `font_provider_freetype.c:309`). No kerning, ligatures or combining marks. | Arabic, Hebrew vowel points, Indic scripts, Thai, Khmer, decomposed accents |
| Fonts | Atkinson Hyperlegible Next (Latin) with Noto Symbols, Symbols 2 and Math fallbacks (`data/FONTS.md`), picked per codepoint. No system fallback, and no Cyrillic, Greek or CJK face. | Cyrillic, Greek and CJK draw as missing-glyph boxes |
| Bidi and RTL | None | Arabic, Hebrew, Persian, Urdu |
| Line breaking | At spaces only (`font_cache.c:1003`) | CJK, Thai |
| IME | **X11:** XIM with no preedit, and no `XFilterEvent` call. **macOS:** no `NSTextInputClient`. **Windows:** `WM_CHAR` one UTF-16 unit at a time, so surrogate pairs aren't joined. | Typing CJK. On Windows, typing any character above U+FFFF, including emoji. |

Effort by target:

- **European languages:** a string catalogue and formatting only.
- **Cyrillic and Greek:** also a font.
- **CJK:** fonts, line breaking and IME.
- **RTL and Indic:** also shaping and bidi. Roughly five times the European
  effort.

### Formatting

- **Numbers:** printed with `%I64u`.
- **Sizes and times:** English units, and only English relative times.
- **Plurals:** the plural column is unused, and one form can't express Slavic
  or Arabic plural rules anyway.
- **Keybinding names:** should follow platform convention (⌘⇧ glyphs on macOS)
  more than locale.

### Approaches

- **A. String IDs from an `.mdesk` table, with per-locale tables compiled
  in.** Fits metagen. A build option can choose which locales are compiled in.
- **B. gettext.** Keyed by English text, which is exactly the label-hash
  hazard above.
- **C. Fluent in Rust behind a shim.** Handles plurals and argument order
  properly, at the cost of an FFI call and allocation for every message,
  every frame.
- **D. Hybrid (preferred).** A for static labels, with English always present
  as the fallback; Fluent or ICU4X for plurals and parameterised messages.
  The locale is a runtime setting with a fallback chain (`de-CH → de → en`).

A pseudo-locale (accented text, padded about 40%) would surface truncation
problems cheaply.

### Phases

1. **Extract strings into a table, English only.** Also add `###` keys so UI
   identity stops depending on labels, rewrite the fragmented sentences, and
   key themes by code name. About 1.5–2.5k lines; low risk, and useful
   without any translation.
2. **Runtime catalogue, locale setting, plurals and formatting.** About
   0.8–1.5k lines.
3. **CJK fonts and line breaking.** About 1k lines.
4. **Shaping.** About 2–3k lines. High risk to terminal performance and to
   caret and selection maths.
5. **Bidi and RTL layout mirroring.** About 2k lines.
6. **IME on three platforms.** About 1.5k lines.

## c) kb_text_shape

[JimmyLefevre/kb](https://github.com/JimmyLefevre/kb) provides `kb_text_shape.h`.

- **What it does:** Unicode segmentation (direction, line, script, word,
  grapheme) and OpenType shaping in the style of HarfBuzz, including complex
  scripts and ligatures.
- **What it doesn't do:** no rasterisation, no system font selection, no
  paragraph layout. It outputs positioned glyphs on one endless line.
- **Form:** a single C header under the Zlib licence.
- **Maturity:** young. About 1.2k stars and 80 commits, with no releases.

It suits the RAD style and would cover shaping, line breaking and script
itemisation on every platform with one library. It would need:

- a font cache keyed by glyph index rather than codepoint;
- a rasterise-by-glyph-index path in the font providers.

Not yet checked: whether it implements full bidi, and its allocation model.
