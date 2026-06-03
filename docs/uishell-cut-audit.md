# UI Shell Cut Audit

This pass treats `uishell` as the only product target in this tree. The original RAD Debugger repository remains the external reference/oracle.

## Current Shape

`src/uishell/uishell_main.c` still builds a broad RAD-derived unity:

- platform/window/render/UI: `base`, `win32`, `window_manager`, `font_provider`, `render`, `font_cache`, `draw`, `ui`
- shell data/file views: `content`, `file_stream`, `text`, `mutable_text`
- config/panels/commands: `config`, `mdesk`, `raddbg_core`, `raddbg_eval`, `raddbg_widgets`, shell metadata/dispatch/views
- debugger/debug-info scaffolding: `dbg_engine`, `eval`, `eval_visualization`, `dbg_info`, `arch`, `rdi`

The visible shell currently uses text/binary file views, panels, menus, config, windowing, rendering, and file/content caches. The binary file view is a raw byte/hex viewer; executable/object-format readers are no longer part of the shell unity build. The broad debugger runtime/debug-info conversion stack is now stubbed or removed; remaining debugger-shaped headers stay only because the temporary RAD register compatibility packet, evaluator scaffolding, fonts/icons/theme tables, and shared widgets still reference those types.

## Landed Cuts

### Drop `app_ui_tables.c` from the shell unity build

Result: succeeded. `raddbg_core.c` no longer compiles the generic watch/query table renderer under `UI_SHELL_APP`; shell tabs named `text` and `binary` dispatch through registered shell view hooks. A shell-only no-op `rd_view_ui__null` definition now supplies the sentinel view hook that RAD normally got from `raddbg_views.c`.

Build gate:

```sh
bash build.sh uishell
```

Implication: `EV_*` table-view interaction types are no longer needed by shell tab rendering itself. `eval`/`eval_visualization` still remain in the shell build because file expressions, text expand hooks, pending file probing, settings evaluation, and generated/scaffolded RAD core paths still reference them.

### Delete inactive RAD view/table files

Result: succeeded. With the local RAD product target removed, `src/app_ui/app_ui_tables.*` and `src/raddbg/raddbg_views.*` were only reachable through inactive non-shell include gates. Those include gates have been removed from `raddbg_inc.*`, and the inactive files have been deleted from this shell tree.

### Remove inactive generic watch/table renderer from RAD core

Result: succeeded. The large `#if !defined(UI_SHELL_APP)` watch/table branch inside `rd_view_ui` has been removed. `raddbg_core.c` no longer references the deleted `APPUI_*` table types or `app_ui_table_*` helpers; the active shell path now flows from `pending` directly to the shell-registered visualizer hooks.

### Specialize RAD-facing scaffolding to the shell preprocessor path

Result: succeeded. `src/raddbg/raddbg_core.c`, `src/raddbg/raddbg_core.h`, `src/raddbg/raddbg_eval.c`, and `src/raddbg/raddbg_widgets.c` have been mechanically run through the `UI_SHELL_APP` preprocessor path. This removes inactive local RAD branches from these files without touching platform/window/render/UI layers.

### Collapse shell entry identity constants

Result: succeeded. `src/uishell/uishell_main.c` no longer defines `UI_SHELL_APP` or carries fallback RAD identity constants. The entry file now directly defines the shell title, config magic, data folder, default user/project paths, and log names.

### Replace runtime-visible inherited RAD identity strings

Result: succeeded. The shell entry now overrides the inherited crash-report link text, macOS POSIX IPC prefix, and Windows crash dump filename. The macOS native app menu also derives its app and quit labels from `BUILD_TITLE` instead of hardcoded RAD Debugger strings. Shared platform files keep neutral macro defaults so the shell values stay product-owned.

### Remove generated RAD command-info table

Result: succeeded. The shell-specialized command path uses `UIShell_CmdInfo`, so the generated `rd_cmd_kind_info_table` was dead. The source generator block was removed from `src/raddbg/raddbg.mdesk`, then `build.sh meta uishell` regenerated `src/raddbg/generated/raddbg.meta.*` without `RD_CmdKindInfo`, `RD_Query`, or the debugger command-info table. The generated UI/theme/icon tables remain because the shell still uses them.

### Make active commands and default bindings shell-owned

Result: succeeded. The remaining enum-based command pushes in shell views now use name-based shell commands, so active command emission no longer depends on the generated `RD_CmdKind` enum. The generated `RD_CmdKind` enum, generated `rd_default_binding_table`, and stale `RD_DefaultBindingTable` `.mdesk` source block have been removed. Default binding reset uses `uishell_default_binding_table`, and shell command query metadata no longer carries the inherited `D_EntityKind` field.

Implication: command catalog, command dispatch, and default keybinding data are now shell-owned in `src/uishell/uishell_commands.h` and `src/uishell/uishell_dispatch.h`. The queue, keymap, query, and menu plumbing is still RAD-derived and name-based.

### Make active vocabulary shell-generated

Result: succeeded. `src/uishell/uishell.mdesk` now owns the active shell vocabulary source and generates `src/uishell/generated/uishell.meta.*`, including `uishell_vocab_info_table`. RAD core consumes this through the app-level `RD_APP_VOCAB_INFO_TABLE` hook and also seeds command labels/icons from `uishell_cmd_info_table`.

The old `RD_VocabTable` source table and `rd_vocab_info_table` data block have been removed from `src/raddbg/raddbg.mdesk`, and regenerated `src/raddbg/generated/raddbg.meta.*` no longer declares or defines the 361-entry debugger vocabulary table. `RD_VocabInfo` is now an explicit shared type in `src/raddbg/raddbg_core.h` because the shell-generated vocabulary table and shared vocabulary map still use that row shape.

### Remove stale RAD fixed-tab, schema, command, register-array, and README metadata

Result: succeeded. `src/raddbg/raddbg.mdesk` no longer defines the old RAD fixed-tab tables, RAD config schema table, generated command table, generated register slot name/range arrays, or generated README markdown. Regenerated `src/raddbg/generated/raddbg.meta.*` no longer emits `RD_CmdKind`, `RD_CmdKindInfo`, `RD_Query`, default bindings, RAD vocabulary data, fixed-tab fast-path arrays, RAD schema metadata, or generated register slot lookup arrays.

Implication: active commands, default bindings, menu/open-tab policy, view listing, schemas, vocabulary, and app register-slot lookup are shell-owned. `src/raddbg/generated/raddbg.meta.*` is now limited to shared icon, code-color, theme, font, and app-icon generated data.

### Move register and vocabulary compatibility types out of RAD metadata

Result: succeeded. `RD_RegSlot`, the temporary `RD_Regs` compatibility packet, and `RD_VocabInfo` are now explicit types in `src/raddbg/raddbg_core.h` instead of generated output from `src/raddbg/raddbg.mdesk`. The RAD metadesk source no longer contains `RD_RegTable`, `@enum RD_RegSlot`, `@struct RD_Regs`, or `@struct RD_VocabInfo`, and regenerated `src/raddbg/generated/raddbg.meta.*` no longer emits those types.

Implication: the remaining temporary register packet is still broad and still carries debugger fields because shared compiled scaffolding references those fields, but it is no longer a generated metadata dependency. Future field removal can now happen with direct code edits and compiler feedback.

### Remove register-layout metadata from shell slot lookup

Result: succeeded. `uishell_app_reg_slot_info_table` now contains only shell slot names, not byte ranges into `RD_Regs`. Query numeric fill for the shell address/file-offset slot now assigns `rd_regs()->vaddr` explicitly instead of copying bytes through an app-provided register range.

Implication: shell register metadata no longer needs to mirror the temporary `RD_Regs` compatibility layout. The generated all-fields `rd_regs_lit_init_top` macro was also removed from `src/raddbg/raddbg.mdesk`; regenerated RAD metadata no longer emits it, and the shell continues to use its explicit `RD_APP_REGS_LIT_INIT_TOP`.

### Trim app-facing register and command flag enums

Result: succeeded. `RD_AppRegSlot` now contains only shell-relevant app register slots. The debugger-only app slots for machine/process/thread/module/control entity, eval/debug-info ranges, debug-info keys, PID, disassembly preference, and similar debugger payloads were dead and have been removed. The old `RD_CmdKindFlag_ListInIPCDocs` flag was also removed because the shell has no IPC documentation command listing path.

Implication: shared command/query metadata still uses the RAD-prefixed adapter types, but those types now describe the shell boundary instead of carrying unused debugger vocabulary.

### Make binding-version remaps shell-generated

Result: succeeded. `src/uishell/uishell.mdesk` now owns the old-keymap command-name compatibility aliases that still apply to the shell: `commands`, `load_user`, `load_profile`, `load_project`, and `open_profile`. RAD core reads them through the `RD_APP_BINDING_VERSION_REMAP_*` app hooks.

Implication: stale debugger-only keybinding aliases such as breakpoint and debug-info source switching remaps are gone from this tree. Keymap loading keeps shell-relevant migration behavior without keeping debugger command compatibility alive.

### Remove stale RAD TODO block from shell entry

Result: succeeded. `src/uishell/uishell_main.c` no longer carries the inherited RAD debugger TODO/recently-completed note block above the build options.

### Stop starting the demon/control runtime

Result: succeeded. `src/uishell/uishell_main.c` no longer calls `dmn_init()`, `d_set_wakeup_hook(...)`, or `d_init()`, so the shell does not start the demon/control runtime or debug-engine runtime. The shell no longer includes the demon layer at all, and the old manual demon/debug-engine init macros are gone from the shell entry point. RAD-derived UI code that still names `D_*`/`DI_*` APIs is served by `src/uishell/uishell_debug_stubs.c`.

### Remove the local `raddbg` build target

Result: succeeded. `build.sh` no longer has a standalone `raddbg` app target. The macOS `bundle` target remains, but now packages `uishell` into `UI Shell.app` using shell-owned plist metadata. The original RAD repository remains the debugger reference.

### Remove non-shell tool build targets

Result: succeeded. `build.sh` now exposes only the product shell app targets (`uishell`, macOS `bundle`) plus the existing `meta` generator path. `build.bat` defaults to and exposes only `uishell` on Windows. Local `radbin`, `torture`, `raddump`, scratch/mule, `radlink`, and debugger targets no longer compile from this repo, so they cannot act as preservation gates for debugger/linker/tooling code.

### Delete unused app/tool source trees

Result: succeeded. The dependency-checked source deletion pass removed `src/radbin`, `src/raddump`, `src/torture`, `src/mule`, `src/scratch`, the old local `src/raddbg/raddbg_main.c` entry point, and old RAD macOS bundle assets. `uishell` and the macOS shell bundle still build after deletion.

### Delete unused RAD debug-data compression helper

Result: succeeded. `src/third_party/rad_lzb_simple` was only referenced by deleted `raddump`/RAD debug-data tooling and docs, so it has been removed from the shell tree.

### Delete stranded linker application sources and BLAKE3

Result: succeeded. The unused linker application/debug-info implementation (`lnk*`, linker-local CodeView/PDB/RDI builders, thread-pool helpers, and linker base-extension files not included by the shell) was removed together with `src/third_party/blake3`. A later dependency check showed the remaining linker helper subset was only retained for deleted object/debug-info readers, so `src/linker`, `src/third_party/radsort`, and `src/third_party/martins_bitscan` have now also been removed.

### Delete stranded library/tool source directories

Result: succeeded. Exact include/reference checks showed `src/obj`, `src/llvm`, `src/strip_lib_debug`, and `src/msvc_crt` were not included by the shell unity build or active product build scripts, so they have been removed. `src/natvis` remains because the Windows build still references `src/natvis/base.natvis`.

### Delete inert debug parser/converter/runtime source trees

Result: succeeded. Dependency checks plus `build.sh meta uishell` showed that the shell product no longer includes or generates from the old debug parser/converter trees. The cleanup removed `src/dwarf`, `src/arch/dwarf`, `src/codeview`, `src/msf`, `src/pdb`, `src/minidump`, `src/rdi_from_*`, `src/rdi_make`, `src/lib_rdi_make`, and `src/third_party/sinfl`.

The pass also removed runtime implementation files that have shell stubs instead: `src/dbg_info/dbg_info.c`, `src/dbg_engine/dbg_engine_*.c`, `src/dbg_engine/dbg_engine_inc.c`, `src/demon`, `src/demon/demon_inc.*`, and the debugger-specific OS demon backend directories under `src/linux/demon`, `src/mac/demon`, and `src/win32/demon`.

Implication: cross-platform shell support now means the platform base/window/font/render layers remain for Windows, Linux, and macOS. The debugger OS backends are no longer part of this product tree.

## Cut Probes

These cuts were checked against the shell target and updated as the runtime stubs landed.

### Remove `stap/stap_parse`

Result: succeeded. Once the shell unity stopped including `demon_inc`/platform demon backends, and later stopped including the no-op demon stub too, the Linux debugger backend no longer constrained the shell build. `src/stap` has been deleted from the shell tree.

Implication: cross-platform shell support is preserved through platform/window/render/font layers, not by keeping debugger-specific Linux demon internals in the product unity.

### Remove `disasm/disasm_inc`

Result: succeeded. Once `dbg_engine_inc.c` was replaced by shell runtime stubs, no included shell source referenced `DASM_*` types or `dasm_*` helpers. `src/disasm`, `src/x64/disasm`, and `src/arm64/disasm` have been deleted.

Implication: Zydis and the ARM64 disassembler are no longer required by the shell product.

### Remove `dbg_engine/dbg_engine_inc`

Result: succeeded for the active shell unity. `src/uishell/uishell_main.c` now includes `dbg_engine_core.h` and `dbg_engine_user.h` directly. `dbg_engine_inc.h` and `dbg_engine_ctrl.h` have been deleted.

Implication: the debugger control layer types for live process memory, call stacks, and debug-control eval spaces are no longer part of the shell compile path. Core/user debug-engine types remain because shared RAD-shaped UI scaffolding still uses `D_Handle`, `D_EntityKind`, `D_Entity`, and a small set of `D_*`/`DI_*` compatibility stubs.

Update: the shell eval-space bridge no longer depends on `D_EvalSpaceKind_FirstUserDefined`, `d_ctrl_eval_space_*`, live process memory reads/writes, thread register reads/writes, call-stack register unwind reads, process async keys, process whole-range mapping, platform TLS conversion, or call-stack query/type hooks.

### Remove dead debug-engine metadata and runtime structs

Result: succeeded. `src/dbg_engine/dbg_engine.mdesk` no longer carries the old debugger command table, `D_CmdKind`, exception-code tables, or exception sub-code metadata. Regeneration leaves only developer toggles and `D_EntityKind` metadata in `src/dbg_engine/generated/dbg_engine.meta.*`.

Result: succeeded. `src/dbg_engine/dbg_engine_core.h` has been reduced to the handle and entity/list/array types still used by the shell compatibility path. Dead debugger runtime payloads such as `D_MsgID`, handle list/array helpers, breakpoint structs, target/path-map structs, trap/spoof/trap-net structs, TLS model fields, entity context storage, and `d_init` have been removed. Matching no-op shell stubs for stale handle-array, address translation, cached register, cached IP/SP, and thread-register write helpers have also been deleted.

Implication: the shell keeps the generic control-entity shape needed by inherited query/title/eval helpers, but no longer preserves the old debugger launch/breakpoint/trap/runtime type surface.

### Remove `eval_visualization/eval_visualization_inc`

Result: failed. `raddbg_core.h` and shell text expand hooks still reference `EV_View`, `EV_StringFlags`, `EV_Key`, `EV_Row`, `EV_BlockRangeList`, and `EV_ExpandRuleTable`.

Implication: eval visualization is still structural because query/table UI and shell text expansion are using `EV_*` types. We need either a shell-local subset or to drop those table/expand paths from shell.

### Remove executable/object-format readers

Result: succeeded. `src/uishell/uishell_main.c` no longer includes COFF, PE, ELF, Mach-O, or GNU readers, and the directories `src/coff`, `src/pe`, `src/elf`, `src/macho`, and `src/gnu` have been deleted. The small `uishell_debug_support.h` DW/EH compatibility shim and its ULEB128 helper were also removed, along with the now-dead `arch_reg_code_from_dw` declaration/implementation.

Implication: binary files are still supported as raw byte/hex views, but executable-format interpretation is no longer a product surface.

### Split evaluator from RDI/debug-info

Result: in progress. The evaluator is now the largest remaining debugger-shaped dependency cluster. It should not be deleted as a unit: part of it is the app/query/list machinery that preferences, command palette, file-open rows, tab commands, and config views still need; another part is the old debugger expression engine that evaluates against RDI debug info, registers, modules, TLS, and process memory.

First slice: `src/uishell/uishell_eval.*` now owns the shell command/view query name providers. The inherited `commands` and `views` `E_TYPE_*` hooks in `src/raddbg/raddbg_eval.c` are still present as compatibility adapters, but their child-name filtering now routes through shell-owned helpers. This is intentionally not a renderer rewrite; it moves ownership of active shell namespaces while keeping existing list/dialog behavior stable.

The current source split is:

- `src/raddbg/raddbg_eval.c`: active shell query/config provider behavior still lives here. It constructs the current `query:commands`, config/settings, theme, view, and metadata rows. This is the first code to fork into `src/uishell`, because it is shell product behavior but still RAD-named and expressed through `E_*`/`RD_EvalSpaceKind_*`.
- `src/eval/eval_parse.c`: mostly reusable parsing/tokenization, but RDI type-name lookup and primary-module architecture assumptions are still embedded. Keep the parser shape only after type lookup becomes provider/type-registry based.
- `src/eval/eval_core.*`: mixed infrastructure. Generic expression/type/value/cache shapes are reusable in principle, but `E_DbgInfo`, `E_Module`, `E_BaseCtx`, `E_IRCtx`, and `E_Cache` still carry debug-info keys, RDI pointers, modules, registers, locals, members, macro maps, TLS conversion, instruction pointer state, and unwind state.
- `src/eval/eval_types.*`: mixed. Basic scalar/array/struct/type formatting can inform the shell value model, but RDI type construction, RDI member expansion, and debug auto-hook behavior are not shell concerns.
- `src/eval/eval_ir.*`: heavily mixed. It builds RDI-flavored IR/op lists for memory/register/type evaluation. Shell provider lookup should bypass or replace most of this path rather than pretending app-state rows are debuggee expressions.
- `src/eval/eval_interpret.*`: mostly debugger/RDI VM. It decodes `RDI_EvalOp`, reads debug constant data, handles register reads through architecture mappings, and interprets frame/module/TLS/process-memory operations. This is the clearest deletion target once shell providers no longer depend on it.
- `src/eval_visualization/eval_visualization_core.*`: mixed UI machinery. Row keys, expansion state, windowed row lists, simple value formatting, and list/tree expansion are useful. Pointer/procedure/source/module decoration and RDI-derived symbol display are debug-only garnish.
- `src/uishell/uishell_views.c`: current shell text/binary views are shell-owned, but list-like dialogs and settings still depend on the inherited query/eval row machinery elsewhere.

The intended replacement is a shell provider layer:

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

This provider layer should evaluate against shell/app state: commands, views, config, settings, recent projects/files, windows, panels, tabs, files, current selection, and app-provided overlay namespaces. It should not expose process memory, registers, modules, debug-info scopes, procedures, globals, TLS, or RDI parsed data.

Cut rule for this cluster: migrate active shell namespaces first, then remove debugger evaluator machinery by compiler/reference proof. Do not restore full watch-window behavior to fix a shell dialog, and do not hand-recreate list dialogs when the existing provider/list machinery can be narrowed.

## Third-Party Status

Currently active in the `uishell` build:

- `stb_sprintf`: core base formatting. Keep for now.
- `martins_hash`: base MD5/SHA helpers. Keep for now unless base hashing is rewritten.
- `xxHash`: base strings, font cache, eval, and eval visualization. Keep for now.
- `stb_image`: used by RAD core icon loading. Could be removed after shell owns icon/window initialization.

Removed:

- `rad_lzb_simple`: old RAD debug-data compression helper; removed after `raddump` and local RAD tooling targets were deleted.
- `blake3`: only used by stranded linker application/debug-info sources, which are no longer part of this shell product tree.
- `radsort`: only used by deleted linker/object helper code.
- `martins_bitscan`: only used by deleted linker/base bit-array helper code.
- `zydis`: x64 disassembler payload; removed after `disasm_inc` left the shell unity.
- `bn_arm64_disasm`: ARM64 disassembler payload; removed after `disasm_inc` left the shell unity.
- `sinfl`: only used by deleted DWARF compressed-section parsing.

## Cut Order

1. The local `raddbg` product build target has been removed. Keep `~/dev/raddebugger` as the reference.
2. Split shell metadata/registers from `raddbg/generated/raddbg.meta.*`.
   - Done for active command info, command flags, default bindings, menus, schemas, vocabulary, binding-version remaps, fixed-tab policy, view listing, app register-slot lookup, and generated register initializer usage.
   - The temporary `RD_RegSlot`/`RD_Regs` compatibility packet has moved out of generated metadata but still needs to be shrunk or replaced.
   - Remaining generated RAD metadata is the shared icon/code-color/theme/font/app-icon data.
3. Split or drop `app_ui_tables` from shell. Done for the active shell unity build.
   - The inactive local source has been removed; use the original RAD repository as reference if needed.
   - Do not reintroduce the generic watch/query table renderer into `UI_SHELL_APP` unless the shell product explicitly needs table-style config/query views.
4. Remove debugger runtime from shell initialization and frame code.
   - Done for the shell product path. `dmn_init`, the debugger wakeup hook, and `d_init` are no longer called by the shell entry point.
   - `dbg_engine_inc.*`, `dbg_engine_ctrl.h`, `dbg_info.c`, `src/demon`, `demon_inc.*`, and the debugger-specific OS demon backends have been deleted from the shell tree; shell stubs provide the remaining `D_*`/`DI_*` APIs used by shared UI scaffolding.
   - This enabled removing `disasm_inc`, `stap`, `zydis`, and `bn_arm64_disasm`.
5. Remove debug-info/object-format stack from shell.
   - Done for debug-info conversion/runtime pieces: `codeview`, `msf`, `pdb`, `minidump`, broad `dwarf`, `rdi_from_*`, `rdi_make`, and `lib_rdi_make` have been deleted.
   - Done for executable/object-format readers: `coff`, `pe`, `elf`, `macho`, and `gnu` have been deleted.
   - The temporary `src/uishell/uishell_debug_support.h` shim was removed once those readers left the unity.
6. Make shell own app icon/window bootstrap.
   - Then remove `stb_image` if no other shell path needs bitmap decoding.
7. Reassess base-level third party.
   - `stb_sprintf`, `xxHash`, and `martins_hash` are small, core-level dependencies. Keep them until there is a specific reason to replace them.

## Build Gate

For this cut-down phase, the required gate is:

```sh
bash build.sh uishell
bash build.sh meta uishell                    # after editing .mdesk inputs
./build/uishell src/uishell/uishell_main.c   # should stay alive when opening a text file
bash build.sh bundle                         # macOS
```

On Windows, the equivalent gate is:

```bat
build uishell
```

There are no local RAD/debugger/tool product build targets. Do not restore them as regression gates; use the original RAD repository as the reference.
