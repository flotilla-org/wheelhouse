# Top-Down UI Shell Cut Plan

## Goal

Build a new native app from the RAD Debugger UI shell while preserving the visual and layout behavior of an empty RAD window: window chrome, menu bar behavior, panels, tabs, widgets, fonts, renderer, and platform/windowing behavior.

The first cut should not remove cross-platform support. Windows, Linux, and macOS platform paths stay in-tree unless a replacement exists and is verified.

## Current Baseline

This repository is now a source copy of `/Users/robert/dev/raddebugger` excluding generated/local outputs. The product target builds with:

```sh
bash build.sh uishell
```

On macOS, the bundle target packages the shell app as `UI Shell.app`:

```sh
bash build.sh bundle
```

On Windows, `build.bat` defaults to the shell target:

```bat
build uishell
```

The first baseline adjustment was that `build.sh` falls back to `unknown` Git metadata when building outside the original Git checkout.

The `uishell` target now compiles from `src/uishell/uishell_main.c`, a forked entry file. It still includes the RAD shell/app layers as scaffolding, but its top-level startup is shell-specific: normal/help modes only, no RAD IPC sender, no radbin utility mode, no auto-run/step/JIT attach handling, and no debugger IPC thread.

`uishell_main.c` no longer includes `radbin/radbin.h` or `radbin/radbin.c`. This is the first debugger utility module removed from the shell unity build after proving the shell no longer references the built-in binary utility mode.

The next shell slice centralizes app menu definitions behind `RD_AppMenuSpec`. The RAD target still receives the full debugger menu set, while the shell target receives File, Window, Panel, Tab, and Help only. Menu item specs now carry command names rather than generated `RD_CmdKind` enum values. RAD still maps those names through the generated command info table at render/dispatch time, while the shell menu definition in `src/uishell/uishell_meta.h` is no longer written against RAD command enum constants. Shell Help menu content is likewise delegated to `uishell_build_help_menu`. The shell target also gates out the center debug-control button cluster through `uishell_should_build_debug_controls` and replaces the RAD command-line help dialog with shell-specific text.

The shell target now owns the runtime-inertness policy as well. The shell entry point no longer calls `dmn_init()`, installs the debugger wakeup hook, or calls `d_init()`, and RAD core no longer carries a shell `debug_runtime_active` branch for target/debug-info/module gathering. `src/uishell/uishell_debug_stubs.c` provides the remaining `D_*`/`DI_*` APIs that RAD-derived UI/eval scaffolding still names, so the debugger runtime does not start.

The shell target now has its own runtime identity constants:

- app data folder: `uishell`
- config magic: `// uishell `
- default user file: `default.uishell_user`
- UI/control logs: `ui_thread.uishell_log` and `ctrl_thread.uishell_log`

New shell windows also reset to a shell-owned panel layout with a main text panel and side output panel, using the existing config-driven panel machinery. The reset implementation is now `uishell_reset_panels` in `src/uishell/uishell_meta.h`; RAD core only delegates to it for shell builds. Non-flag command-line inputs in the shell target are treated as files to open rather than debugger target executables through `uishell_initial_open_file_path_from_args`.

The shell target now owns its active metadata policy in `src/uishell/uishell_meta.h`, `src/uishell/uishell_commands.h`, and `src/uishell/uishell.mdesk`. Shell command-palette lookup, command info, default keybinding reset, keybinding compatibility remaps, menu specs, command-palette roots, view listing, tab fast-path execution, pending file-view choice, and active vocabulary now go through shell-owned helpers/data. Shell-owned listing, menu policy, and shell view command handling are command-name based; `src/uishell` no longer references `RD_CmdKind`, `rd_cmd_kind_info_table`, `rd_default_binding_table`, or `rd_vocab_info_table`. `query:commands` and `query:tab_commands` iterate `uishell_cmd_info_table` directly, active vocabulary comes from generated `uishell_vocab_info_table`, and old shell-relevant keybinding aliases come from generated `uishell_binding_version_remap_*` arrays. The shell helpers allow only shell/window/panel/tab/text/file commands, the `output`/`text`/`binary` tab openers, and the `text` and `binary` views in `query:views`. Debugger tab openers such as targets, breakpoints, disassembly, memory, bitmap, color, and geometry are no longer listed or executed through the tab fast path. The shell palette root helper also drops debugger query roots such as targets, breakpoints, machines, processes, threads, modules, procedures, types, globals, and thread locals.

The shell target also owns its first file-view hook: a `binary` view in `src/uishell/uishell_views.c`. The existing `pending` view still probes file bytes, but under `UI_SHELL_APP` it now asks shell helpers to choose between `text` and `binary` instead of hard-coding the shell fallback in RAD core. The shell `binary` view registers itself through `uishell_register_view_ui_rules`, uses the existing file-stream/content cache, and is now a read-only file hex viewer derived from RAD's memory-grid interaction model rather than the earlier string-row approximation. It keeps per-byte cells, RAD-style fixed 16-byte rows by default, optional auto-sized columns through the view settings, an offset/column/ASCII header, byte-level mouse hit testing in both hex and ASCII regions, byte-range drag selection, keyboard navigation through shared UI events, copy-as-hex text, and a RAD-style bottom status bar. It intentionally does not include debugger memory features such as process address spaces, mutation, address/peek bars, bad/changed memory flags, annotations, rich hover, or debugger eval integration.

The shell target now owns active view registration as well. `raddbg_core.c` still builds the RAD lens table as scaffolding, but under `UI_SHELL_APP` it no longer references or registers the RAD debugger view UI rules (`disasm`, `memory`, `bitmap`, `color`, `geo3d`) into the shell view map. Instead it delegates to `uishell_register_view_ui_rules`, which currently registers shell-owned `text` and `binary` view functions, and to `uishell_register_expand_rule_infos`, which currently preserves a shell-owned text expand-rule hook. The disassembler implementation and third-party disassembler payloads have been removed from this shell tree.

The shell `text` view is implemented in `src/uishell/uishell_views.c` as a file reader using the existing file/content/text cache and scroll-list UI. It intentionally drops source-line/debug-info/margin behavior from the active shell path while preserving the tab expression and panel dispatch model. It supports bottom-bar file/status display, visual-line scrolling, cursor/mark selection, copy, search/find, go-to-line, line-number display, line wrapping, and cursor persistence through the existing view parameter mechanism.

`raddbg_views.h` and `raddbg_views.c` are no longer included in the shell target. The RAD watch/eval table machinery was mechanically split into neutral `src/app_ui/app_ui_tables.h` and `src/app_ui/app_ui_tables.c` during the scaffolding phase, but the active shell target now bypasses that generic watch/query table renderer entirely. The inactive local `raddbg_views.*` and `app_ui_tables.*` files have now been removed from this shell tree; the original RAD repository remains the reference.

The shared command dispatcher now has the first explicit shell boundary. Under `UI_SHELL_APP`, RAD core no longer compiles the debugger run/launch/step default handling, debug-engine command forwarding, external-driver textual debugger commands, source/debug-info disassembly navigation, thread/symbol/watch finding, target creation, JIT debugger registration, or debug-control context selection/frame navigation cases. The default shell dispatcher still keeps generic tab-fast-path handling plus menu, window, panel, tab, query, config, file-open, file-reveal, popup, and window-manager event commands. Shell exit now writes user/project data and quits directly instead of checking for live debuggee processes. The shell command name/kind map has also dropped the old debugger-control compatibility entries, debug-info source/partner-file entries, and inert picker-helper entries, so shell command resolution no longer exposes run/launch/step/kill/halt/target/debug-info helper commands.

The shell target now owns UI-event command dispatch for edit/accept/cancel/focus-menu, directional movement, selection, copy/cut/paste, delete/backspace, insert text, and next/previous navigation in `src/uishell/uishell_dispatch.h`. This helper dispatches by command name rather than `RD_CmdKind`, and the RAD enum switch cases for those commands are excluded from `UI_SHELL_APP`. The RAD target keeps the original enum switch path.

The same shell dispatch helper now owns tab command dispatch for focusing tabs, next/previous tab focus, left/right tab movement, tab construction, duplicate tab, copy tab full path, close tab, move view, tab bar top/bottom, and tab settings. These are handled by command name before the shared RAD enum switch, and their original enum cases are excluded from `UI_SHELL_APP`. The shell dispatcher now also owns the panel layout mutation cluster: reset panels, create/split panels, close panels, panel focus cycling, directional panel focus, explicit panel focus, and panel-column rotation. The shell implementation still reuses the RAD config/panel tree primitives to preserve visual and layout behavior.

The shell dispatch helper also owns the font-size command cluster: increase/decrease window font size and increase/decrease view font size. The shell helper preserves the old window-scoped behavior by clearing view/tab registers while reading the current inherited window font size, and the original RAD enum cases are excluded from `UI_SHELL_APP`.

The shell dispatch helper now owns the window/popup/default-binding command cluster as well: open window, window settings, close window, toggle fullscreen, bring to front, popup accept/cancel, and reset to default bindings. The RAD target keeps its enum switch cases, but they are excluded from `UI_SHELL_APP`; the dead shell branch inside RAD's default-binding reset path has been removed from `raddbg_core.c`.

The shell dispatch helper now owns config persistence as well: opening recent projects, opening/saving user and project files, creating new user/project roots, recording last-opened user and recent projects, and writing user/project data. These commands dispatch by command name in `src/uishell/uishell_dispatch.h`; the RAD enum switch cases remain for the RAD target but are excluded from `UI_SHELL_APP`. The source/debugger path replacement command is also excluded from the shell dispatcher now.

The shell dispatch helper also owns the simple file/query command subset: user settings, project settings, set current path, open file, and show file in the system file explorer. These still use the existing file-expression and tab-building machinery, but the shell target no longer compiles the corresponding RAD enum switch cases.

The shell dispatch helper now owns query lifecycle execution as well: push query, complete query, cancel query, and update query dispatch by command name in `src/uishell/uishell_dispatch.h`. The behavior is intentionally still equivalent to RAD's lister/query mechanics, but `UI_SHELL_APP` no longer compiles those RAD enum switch cases. Query lifecycle emissions now use `rd_cmd_name(...)`, so these commands no longer need generated RAD command enum mapping.

Under `UI_SHELL_APP`, RAD core now runs all shell-owned name dispatch helpers and does not fall through to the RAD enum switch. The shell path no longer has a temporary name/kind bridge, so `rd_cmd_kind_from_string` intentionally returns `RD_CmdKind_Null` for shell builds.

The shell target now owns command-palette/open-tab execution as well. `open_palette`, `run_command`, `open_tab`, and the shell tab fast-path commands `output` and `text` are handled by name in `src/uishell/uishell_dispatch.h`. The old RAD enum cases and generated tab-fast-path table remain for the RAD target but are excluded from `UI_SHELL_APP`. Command-palette/open-tab emissions now use `rd_cmd_name(...)`, so they no longer require shell enum-name mappings.

The shell target also owns lifecycle and window-manager event dispatch: `exit` writes shell user/project data and quits without debugger-process confirmation, and `wm_event` translates platform window events into UI events in `src/uishell/uishell_dispatch.h`. The RAD enum switch cases remain for the RAD target and are excluded from `UI_SHELL_APP`. Lifecycle/startup/autosave/window-event emissions now use `rd_cmd_name(...)`, so `Exit`, `WMEvent`, `OpenUser`, `OpenProject`, `WriteUserData`, `WriteProjectData`, and `InsertText` no longer need entries in the temporary name/kind bridge.

The shell target also catches the reserved app-level no-op commands `undo`, `redo`, `go_back`, and `go_forward` by name. Their RAD enum switch cases remain for the RAD target and are excluded from `UI_SHELL_APP`, and these names no longer need entries in the temporary shell name/kind bridge.

The shell-preprocessed command path has now had debugger command emissions gated out of shared UI. Under `UI_SHELL_APP`, `raddbg_widgets.c` no longer emits breakpoint, watch-pin, thread-selection, or instruction-pointer mutation commands from the shared code-slice widget. The old generic table renderer was removed after the shell text/binary views took over active tab rendering. `raddbg_core.c` hides target/debug-info drop-completion options, the target-oriented getting-started helper, debug-control toolbar command construction, source/debug-info navigation fallbacks, and debug-engine stop/refocus command emissions. Shell/generic command emissions now use names, and shell text/search/navigation local-command routing is name-based.

The temporary shell command name/kind bridge has been removed. A mechanical shell-preprocessed audit now shows zero `rd_cmd_name_from_kind(RD_CmdKind_*)` emissions, zero `case RD_CmdKind_*` labels, and no `rd_shell_cmd_name_kind_table` entries in the shell path.

The shell target now owns active config schema metadata in `src/uishell/uishell_meta.h`. Shell startup, schema type registration, individual config macro registration, and `query:views` iterate `uishell_name_schema_info_table`; the old RAD `RD_SchemaTable`, `RD_NameSchemaInfo`, and generated `rd_name_schema_info_table` have been removed from this tree. The shell schema set is intentionally small: `user`, `project`, `theme_color`, `window`, `tab`, `text`, `binary`, and `recent_project`. This keeps UI defaults, panels, tabs, text files, binary files, and recent-project persistence without registering debugger roots such as targets, breakpoints, watches, debug info, process/thread/machine/module, disassembly, memory, bitmap, color, or geometry. The inherited `environment` set hook is deliberately retained even though no active shell schema constructs it today; in RAD it represented per-target process launch environment strings, and that editable string-list shape may be useful for future shell/app launch or process configuration.

The debugger-specific `watches` and `peek_types` config set hooks have been removed. The shell still uses the inherited `watch` view name as the generic lister/property popup surface for command palettes, settings, and query dialogs, but it no longer registers RAD watch-tab child expressions or memory-view peek-type child rows as evaluator set types. Expressionless generic listers now fall back to `query:views`, a valid shell query root, instead of the removed `query:config.$view.watches` path.

Debugger type-view configuration has also been removed from the active shell surface. The project settings no longer expose default STL/Unreal type visualizer toggles, frame setup no longer constructs immediate type-view configs or registers type-view auto-hook rules, the stale type-view row-title renderer is gone, and the embedded markup helper no longer emits type-view records. The evaluator still contains inert auto-hook internals, but there is no shell producer for type-view rules.

Debugger source path-map and legacy RAD config migration scaffolding has been removed. Shell user/project config loading now always goes through the current shell schema parser; it no longer invokes the pre-0.9.16 RAD migration path for debugger targets, recent projects, or source remaps. The `file_path_map` source-remap helpers and stale row-title rendering are gone.

The shell target now owns active register slot metadata as well. `src/uishell/uishell_meta.h` defines `UIShell_AppRegSlot` and `uishell_app_reg_slot_info_table` for the shell-relevant register subset. Shared register parsing/fill helpers now get slot names through `rd_app_reg_slot_code_name`, and the old generated RAD register slot name/range arrays have been removed from this tree. The shell path adapts from the temporary `RD_RegSlot` compatibility enum to shell metadata in one explicit switch instead of indexing shell tables by `RD_RegSlot_COUNT`. This does not yet replace `RD_Regs` or the compatibility enum, but shell query parsing and numeric slot filling no longer depend on generated RAD slot name/range tables, byte offsets into `RD_Regs`, or shell tables shaped like generated RAD tables.

The shell target now owns command/query metadata types. `src/uishell/uishell_commands.h` defines `UIShell_Query` and `UIShell_CmdInfo`, so shell command definitions no longer use generated `RD_Query` or `RD_CmdKindInfo`. Shared UI paths that only need app command metadata now read through `RD_AppCmdInfo`, which adapts to shell metadata. The generated `RD_CmdKindInfo` type/table has been removed from the shell tree; active shell command listing, command-palette/open-tab execution, shell query setup/completion, menu construction, and schema expand-command search now use shell command rows directly.

The shell command/query metadata also now owns its identifiers. Shell command rows use `UIShell_CmdFlag_*`, `UIShell_QueryFlag_*`, and `UIShell_RegSlot_*` instead of spelling RAD command flags, query flags, or generated register slots directly. `src/uishell/uishell_dispatch.h` compares shell query metadata against these shell identifiers. The remaining bridge back to RAD-shaped shared UI code is explicit: `rd_cmd_flags_from_uishell_cmd_flags`, `rd_query_flags_from_uishell_query_flags`, and `rd_app_reg_slot_from_uishell_reg_slot` adapt shell metadata into `RD_AppCmdInfo` and existing filtering helpers.

`RD_AppCmdInfo` no longer exposes `RD_RegSlot` in its query metadata. It now carries `RD_AppRegSlot`, and that app slot enum is shell-sized instead of carrying debugger-only values. Shell command metadata maps from `UIShell_RegSlot` into that app slot type, and the only conversion back to `RD_RegSlot` for command query completion is now the explicit `rd_reg_slot_from_app_reg_slot` call at the `rd_regs_fill_slot_from_string` boundary.

The shell target also no longer uses or generates the old all-fields register initializer. `src/raddbg/raddbg_core.h` defines `RD_APP_REGS_LIT_INIT_TOP` directly and copies only the shell UI/config/file/query fields by default. Explicit initializers can still set full `RD_Regs` fields while the compatibility packet exists, but ordinary shell `RD_RegsScope`, `rd_push_regs`, and `rd_set_autocomp_regs` no longer implicitly preserve debugger execution fields such as machine, process, thread, debug-info keys, virtual offset ranges, or disassembly preferences. Named command emissions now use the shell-owned `UISHELL_REGS_LIT_INIT_TOP` packet instead.

The debugger runtime implementation has now left the shell unity build. `src/uishell/uishell_main.c` still includes the narrowed debug-engine compatibility declarations used by shared scaffolding, but it no longer includes `dbg_info/dbg_info.h`, and the `DI_*` shell stubs are gone. The runtime/debug-parser implementation files and directories are physically gone from the shell tree: `src/demon`, `demon_inc.*`, platform demon backends, `dbg_engine_*.c`, `dbg_engine_inc.*`, `dbg_engine_ctrl.h`, `dbg_info.c`, `src/dbg_info`, PDB/CodeView/MSF/minidump, RDI conversion, RDI make, disassembly, STAP, and broad DWARF.

The executable/object-format readers are no longer shell surface. `coff`, `pe`, `elf`, `macho`, and `gnu` have left the unity build and their source directories have been deleted, along with the temporary DW/EH compatibility shim that existed only for those readers. The shell binary view remains a raw byte/hex file viewer. `lib_rdi` still remains because the evaluator still uses the RDI eval opcode format as its internal bytecode representation.

The shell eval-space bridge has been cut away from live debugger memory. `RD_EvalSpaceKind_*` now starts from the generic evaluator user-defined range instead of `D_EvalSpaceKind_FirstUserDefined`, and the shell `rd_eval_space_gen/read/write`, async key, whole-range, and TLS conversion hooks no longer route unknown spaces through debug-control memory/register/process callbacks. Config, file, and hash-store spaces remain active; process memory, thread register blocks, call-stack-backed register unwinds, call-stack query expansion, and meta-control-entity reads are no longer shell behavior.

The remaining debug-engine metadata has been narrowed to the pieces still used by shared shell scaffolding. `src/dbg_engine/dbg_engine.mdesk` no longer defines the debugger command table, `D_CmdKind`, exception-code metadata, or exception sub-code metadata; generated debug-engine metadata now contains only developer toggles and `D_EntityKind` tables. `src/dbg_engine/dbg_engine_core.h` has likewise dropped debugger runtime payloads such as message IDs, breakpoints, targets, path maps, traps, spoof/trap nets, TLS model data, entity context storage, and `d_init`, leaving the handle/entity/list types used by the compatibility layer.

The remaining explicit debugger register emissions have now been gated out of the shell-preprocessed shared UI path as well. Under `UI_SHELL_APP`, the code-slice widget no longer emits thread/entity hover, drag, selected-line debug-info payloads, or `.lines` payloads in text context queries. The shared table path keeps config/file/command behavior for shell rows, but no longer emits control-entity, machine/process/module/thread, address-range, eval-space, source-UI-key, or no-rich-tooltip register payloads for shell builds. Meta-control-entity writes, control-entity query completion, unattached-process query completion, module-scoped table row registers, and cache-line debugger row decorations are now `raddbg`-only. A mechanical preprocessed shell audit now shows zero debugger-field `RD_Regs` initializers in active shell command/register emissions and zero RAD/Demon command-name bridge emissions.

Shell register snapshots are now also copied through a shell-sized boundary. `src/uishell/uishell_meta.h` defines `UIShell_Regs`, a shell-owned register packet with only UI/config/file/query fields, plus explicit conversion helpers between the temporary `RD_Regs` compatibility packet and `UIShell_Regs`. Under `UI_SHELL_APP`, `rd_regs_copy_contents` now packs through `UIShell_Regs` and unpacks back into the compatibility packet, so command lists, popup commands, pushed register scopes, query state, autocompletion state, hover state, and drag/drop state no longer preserve debugger fields by structural copy. Query-slot filling for shell builds no longer compiles machine/process/module/thread/control-entity parsers, debugger-only numeric slots, or thread-register promotion. The generated-slot/app-slot adapter is shell-sized under `UI_SHELL_APP`, so debugger register slots map to `Null` instead of remaining active shell metadata. The active shell storage boundary is now shell-owned even though many shared call sites still accept `RD_Regs *` while the compatibility layer is being unwound.

Command queue storage is the first stable owner moved onto that shell packet. `RD_Cmd.regs` now uses an app-specific `RD_CmdRegs` alias: `UIShell_Regs` for `UI_SHELL_APP`, and `RD_Regs` for the original RAD target. The common `rd_cmd_name(...)` emission macro now builds a `UIShell_Regs` literal and pushes it through `rd_push_stored_cmd`, so named command emissions no longer construct a temporary `RD_Regs` packet. Direct shell command emissions that already pass the live register stack through `rd_push_cmd(name, rd_regs())` still accept temporary `RD_Regs *`, then pack into `UIShell_Regs` at the queue boundary. Replaying or deferring an already queued command uses `rd_push_stored_cmd`, preserving the shell-owned packet without reinterpreting it as a debugger packet. Popup-confirmed commands are captured directly as `UIShell_Regs`, so they no longer allocate an intermediate `RD_Regs` snapshot before entering popup command storage. Top-level shell command dispatch unpacks the queued `UIShell_Regs` into the temporary compatibility `RD_Regs` only for the duration of dispatch. A preprocessed shell audit confirms the command list stores `RD_CmdRegs *`, the insertion path uses `uishell_regs_copy`, and queued-command replay no longer passes `UIShell_Regs *` through the old `RD_Regs *` push path.

The first shell regression repair pass restored important empty-window behavior without hand-tuning against screenshots. `src/uishell/uishell_meta.h` now exposes the user, project, and window settings commands plus user/project config file commands through the shell menu specs, restoring the preference entry points used for macOS window decorations and native-menu positioning. The macOS `window_close_menu` event now maps to shell `close_window` under `UI_SHELL_APP`, while the original RAD target keeps its close context-menu behavior. The shell text and binary file views also use the `selection` color with reduced alpha for cursor/selection row backgrounds, instead of using the stronger cursor theme color.

The shell text view now owns a small code-view fork in `src/uishell/uishell_views.c`. It keeps file/content/text-cache loading and the existing tab/view/config plumbing, but renders through a shell-owned visual-line array rather than the earlier one-source-line-per-scroll-row file reader. The fork preserves cursor/mark as `TxtPt`, stores `cursor_line`, `cursor_column`, `mark_line`, and `mark_column`, uses the existing text navigation/copy helper, honors the `show_line_numbers` and `line_wrapping` view settings, and scrolls over wrapped visual rows. It deliberately does not transplant RAD debugger code-slice features such as breakpoints, instruction pointers, watch pins, source/debug-info margins, or disassembly mappings. The shell text and binary file views now use RAD-style bottom status-bar geometry instead of top metadata rows.

Shell menu-launched commands now preserve their command name through `run_command`. This fixes menu `exit` and similar no-query UI commands, which previously cleared `cmd_name` before pushing the resolved command.

## Constraints

- Preserve `rd_window_frame` as the shell authority until we have an equivalent shell.
- Keep config-driven panel layout initially; do not hand-tune layout from screenshots.
- Use screenshots only to confirm structural parity with RAD, not to hill-climb arbitrary CSS-style values.
- Keep platform, window manager, renderer, font provider/cache, draw, UI, config, text, mutable text, file stream, and content-style infrastructure until there is a proven smaller boundary.
- Keep debugger subsystems compiled at first if that lets the shell stay faithful; make them inert before deleting them.

## Important Cut Points

- `src/raddbg/raddbg_core.c:18554`: `rd_frame` renders all configured windows and calls `rd_window_frame`.
- `src/raddbg/raddbg_core.h:750`: `rd_window_frame` is the shell draw path to preserve.
- `src/raddbg/raddbg_core.c:6754`: in-window menu bar is built inline when the platform does not use a native app menu.
- `src/raddbg/raddbg_core.c:10456`: native menu construction has parallel menu definitions for platforms like macOS.
- `src/uishell/uishell_meta.h`: shell reset-panel layout is rebuilt from config commands through `uishell_reset_panels`.
- `src/uishell/uishell_dispatch.h`: shell-owned `build_tab` creates tabs from view name plus expression for the shell target.
- `src/raddbg/raddbg_core.c:15450`: `Open` already maps a file path to a new tab expression.
- `src/raddbg/raddbg_core.c:12796`: top-level command dispatch still lives in RAD core, but debugger-only command clusters are now excluded from `UI_SHELL_APP`.
- `src/uishell/uishell_dispatch.h`: shell-owned name-based dispatch for lifecycle/window-manager events, UI-event commands, command-palette/open-tab execution, the main tab command cluster, panel layout/focus commands, and config persistence commands that used to live only in the RAD enum switch.
- `src/raddbg/raddbg_core.c`: debug engine ticking and shell `debug_runtime_active` target/debug-info/module gathering have been removed from the active shell path.
- `src/raddbg/raddbg_core.c:1638`: view dispatch resolves a tab expression to a view UI rule.
- The old RAD query/table helper is no longer present locally; use `/Users/robert/dev/raddebugger` if that implementation is needed as reference.

## First Slice

1. Keep the full copied build compiling.
2. Add a new app-shell build target rather than deleting RAD code in place.
3. In that target, keep `rd_init`, `rd_frame`, `rd_window_frame`, config loading, menu rendering, panel layout, tab movement, and view dispatch.
4. Make debugger control inactive at the frame boundary: skip target/breakpoint gathering and `d_tick`, while leaving required types/functions linked.
5. Reduce menus to shell/file/panel/view/window/help commands through one shared menu definition that can feed both inline and native menus.
6. Replace debugger default panels with an empty-shell default layout using the same config tree mechanics.
7. Keep file opening: `Open` should build a tab for a file path, with text/binary rendering handled by shell-owned file views.

## Cut Direction

`uishell` is the product target. Local RAD/debugger/tool build targets have been removed so they cannot become accidental preservation constraints. Use `/Users/robert/dev/raddebugger` as the debugger reference source/oracle. When a cut makes local debugger code incompatible with shell extraction, prefer preserving `uishell` and the cross-platform shell/platform layers.

The practical order is:

1. Keep `uishell` building on the current host after every cut.
2. Keep Windows/Linux/macOS platform, window, renderer, font, UI, config, and file-system paths represented in source and build scripts.
3. Move shell-owned behavior out of `src/raddbg` names into `src/uishell` names at stable boundaries.
4. Make debugger systems inert from the shell target before deleting their source or third-party dependencies.
5. Delete debugger-only code when the shell no longer includes or references it, even if that means the original `raddbg` target no longer builds.

## Concrete Cut Plan

The next cuts should be dependency-driven, not screenshot-driven. A preprocessed `uishell_main.c` pass now shows that debugger command emissions have been gated out of the shell path, the temporary shell command bridge is gone, the shell top-level command path no longer falls through to the RAD enum switch, active shell schema loops use shell-owned schema metadata, active register slot metadata goes through an explicit app boundary without `RD_Regs` layout offsets, shell command/query metadata uses shell-owned structs plus shell-owned command/query/slot identifiers, ordinary shell register scopes use a shell-sized default initializer, active shell command/register emissions no longer explicitly initialize debugger register fields, shell register snapshots/fill/adapters preserve only shell fields, and the generic watch/query table renderer is no longer compiled for `UI_SHELL_APP`. The compatibility register packet has started shrinking: stale debugger slots for unwind/inline-depth, debug-info keys, voff/ranges, PID, line lists, and rich-tooltip suppression are gone; window-local query/autocomplete snapshots, hover snapshots, and drag/drop snapshots now store `UIShell_Regs`. The current blocker to replacing the register layer is the remaining live `RD_Regs` stack used by inherited scope macros and widget APIs. The remaining generated RAD metadata is now visual data: icon/theme/font/app-icon tables.

Debugger-only RAD markup annotations have been removed from the shell path. `raddbg_watch`, `raddbg_pin`, `raddbg_entry_point`, explicit breakpoint add/remove markup, and virtual-address range annotations are gone, and the code-slice no longer renders breakpoint/watch-pin margin glyphs or inline `raddbg_pin(...)` evaluations. Keep the generic markup helpers that are still useful for app instrumentation: thread naming, thread coloring, `raddbg_log`, and the local break helpers.

The order from here is:

1. **Continue shell-neutralizing query/config scaffolding.** Shell config registration, active slot table lookups, active command/query metadata, binding remaps, and default register-scope copying now use shell-owned boundaries. The remaining register compatibility packet is explicit code now, not generated metadata, and the debugger-only tail has been removed.
2. **Fork or shrink the register/query layer.** `RD_Regs` still exists because live register scopes and widget APIs expect one packet. Continue replacing stable shell-facing storage sites with `UIShell_Regs`, keeping window, panel, tab, view, file path, cursor/mark, text key, UI key, expression, string, and window-manager event data.
3. **Remove generated RAD command-table dependencies from the shell target.** With command execution name-based and app-owned slot metadata in place, keep shell command metadata in `src/uishell` and stop including generated RAD command metadata in the shell build once remaining type references are isolated.
4. **Continue shrinking debugger-shaped type scaffolding.** Runtime implementations, debug parsers/converters, executable/object-format readers, the demon layer, OS demon backends, disassembler/STAP payloads, generated debug-engine command/exception metadata, debugger launch/breakpoint/trap structs, debugger-only RAD markup annotations, linker/object helper tails, and their third-party payloads have been deleted. The active eval-space bridge no longer uses debug-control memory/register callbacks, and the shell no longer includes `dbg_engine_inc.h`/`dbg_engine_ctrl.h`. The next work is reducing the remaining header/type dependencies from `dbg_info`, the remaining handle/entity subset in `dbg_engine_core`, `dbg_engine_user`, `eval`, and `eval_visualization` where they are still only serving shared RAD UI scaffolding.
5. **Rename the app layer.** After the shell is no longer depending on RAD command/debugger scaffolding, move remaining reusable app-shell code out of `src/raddbg` names where it improves clarity. Keep lower platform/window/render/font/UI/config/file/text layers intact and cross-platform.
6. **Do not restore a local `raddbg` gate.** The required invariant is: `uishell` builds, the bundle target packages the shell app on macOS, the Windows/Linux/macOS shell platform paths remain represented, and the empty-window/panel/menu layout remains visually faithful.

## Next Cuts

1. Split the evaluator boundary into shell app-state providers and debugger/RDI machinery. The shell wants expressions over app state, config, files, windows, panels, tabs, current selection, and app-provided overlays; it does not want the RDI bytecode interpreter, process/register memory spaces, debug modules, or debug-info symbol lookup.
2. Continue moving current shell query providers out of `raddbg_eval.c` into the shell-owned provider layer. Command, view, theme, config-child, schema-expansion, and query-root registration now live in `uishell_eval.*`; the remaining work is adapting the schema/config evaluator/list wrappers and moving file-system rows. Preserve the `environment` editable string-list hook as generic scaffolding unless a shell-owned replacement exists.
3. Keep the list/property rendering behavior, but make it consume shell provider rows instead of debugger-shaped `E_Eval`/RDI type payloads. This is the principled replacement for the remaining generic property/list dialogs, not a recreation of watch-window debugger behavior.
4. Keep query completion and shell command metadata on shell-owned packets; do not revive the old debugger watch/query table path unless a concrete shell feature needs it.
5. Continue replacing shell-facing `RD_Regs *` storage declarations with `UIShell_Regs *` at stable ownership points, next targeting query state, hover state, and drag/drop state.
6. Continue isolating the generic control-entity query/title/eval helpers so `D_EntityKind` can become shell-owned or disappear from the shell path.
7. Re-run the shell-preprocessed command/schema/register/eval audit after each metadata cut.
8. Remove remaining debugger-shaped headers from `uishell_main.c` only after the shell-preprocessed build proves they are no longer referenced.
9. Continue expanding shell text/binary file views after the scaffolding cut is stable: precise wrapped-column hit testing, copy-offset variants, go-to-offset/line with shell terminology, richer find state, optional binary zoom if it remains file-view scoped, and command-palette access through shell metadata.
10. Continue forking the panel/config path: keep RAD's panel mechanics, but make shell persistence and reset variants fully owned by `src/uishell`.

## Evaluator Direction

The evaluator should become an app-state query layer. Its long-term input is an expression plus a shell context packet: current window, panel, tab, view, file, cursor/mark or byte selection, active query string, active lister row, and app-specific overlay state. Its output should be a generic value/list/object model that shell views can render as lists, properties, tables, text, or specialized visualizers.

The provider shape should be shell-owned and small:

```c
typedef struct UIShell_EvalProvider UIShell_EvalProvider;
struct UIShell_EvalProvider
{
  String8 namespace_name;
  UIShell_EvalResolveFunc *resolve;
  UIShell_EvalChildrenFunc *children;
  UIShell_EvalReadFunc *read;
  UIShell_EvalWriteFunc *write;
};
```

The built-in providers should describe shell/application state, not debuggee state:

- `query:commands`: shell command metadata and runnable command rows.
- `query:views`: registered shell views.
- `query:config`: config schemas and concrete settings.
- `query:user_settings` / `query:project_settings`: editable settings rows.
- `query:recent_projects` and future recent files.
- `query:tabs`, `query:windows`, `query:panels`: live shell UI state.
- file providers: file-system rows, open file content summaries, text/binary selection context.

Future app providers can layer in domain state such as `query:assets`, `query:entities`, or `query:documents` without bringing back debugger concepts. Specialized views can also expose view-local providers for selection, cursor, marked ranges, filters, and view settings.

The current evaluator code splits into these buckets:

- Keep or rename: expression tokenization/parsing for literals and simple paths, basic value formatting, config/file/hash-store spaces, list/property expansion mechanics when they are separated from debugger types.
- Fork into shell ownership: query namespace resolution, config/schema row production, value/type descriptors for shell rows, selection/context packets, provider registration, and row editing.
- Delete after migration: `RDI_EvalOp` bytecode interpretation, process-memory-style reads/writes if they are replaced by shell providers, and remaining RDI parsed-data plumbing. Debug-info lookup, `DI_Key` plumbing, debug module/TLS/register evaluation context, and register identifier resolution have already been removed from the active shell evaluator.

The near-term sequence is:

1. Define `uishell_eval.*` with provider registration and a minimal value/list row model.
2. Move the active shell query providers from `raddbg_eval.c` into `uishell_eval.*` without changing their UI behavior.
3. Adapt list-like dialogs and preferences to consume shell provider rows.
4. Replace shell `E_BaseCtx` setup with a shell context packet.
5. Stop including `eval_interpret.c` in the shell unity once no active provider needs RDI bytecode evaluation.
6. Then remove `lib_rdi`, `rdi`, `dbg_info`, `arch`, `arm64`, and `x64` only if exact include/reference scans and the build prove they are no longer active.

## Deletion Rule

Only delete a third-party or debugger subsystem after:

- the app-shell target builds on the current host,
- the equivalent Windows/Linux compile path is either preserved or has an explicit replacement,
- no included source file references the subsystem in the app-shell target,
- the removal does not change panel/menu/window rendering.
