# UI Scratch

This repository is a native UI shell experiment derived from the RAD Debugger UI stack. The product target is `uishell`; the original debugger repository remains the reference/oracle at `/Users/robert/dev/raddebugger`.

## Build

On macOS/Linux:

```sh
bash build.sh uishell
```

On macOS, build an app bundle with:

```sh
bash build.sh bundle
```

On Windows:

```bat
build uishell
```

## Scope

The shell keeps the platform, windowing, renderer, font, UI, config, panel, tab, text, file-stream, and content-cache layers needed for an empty RAD-style window and file-backed text/binary views.

Debugger app targets and local RAD utility/tool build targets have been removed from this tree. Do not reintroduce them as regression gates; use the original RAD Debugger checkout for debugger behavior comparisons.
