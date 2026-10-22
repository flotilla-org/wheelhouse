# Web terminal renderer prototype: notes

PROTOTYPE, throwaway. This is for "The web terminal renderer" (wheelhouse#431, map wheelhouse#421).

## Running it

```sh
# 1. Record synthetic terminal output (asciicast v3, 120x36).
python3 record/record.py record/demo.cast 120 36 python3 record/demo.py
# 2. Play it through Cleat's Ghostty engine into screen-state frames (CBOR).
cd convert && CLEAT_GHOSTTY_PREFIX=~/dev/cleat/.tools/ghostty-install cargo build --release
DYLD_LIBRARY_PATH=~/dev/cleat/.tools/ghostty-install/lib ./target/release/cast-to-screen ../record/demo.cast ../web/public/demo.cbor
# 3. Run the renderers.
cd ../web && pnpm install && pnpm dev    # http://localhost:5179/?r=canvas or ?r=webgl
```

`convert/` uses the Cleat checkout Wheelhouse builds against (`cleat-worktrees/wheelhouse-osc52-169`, 00c072b).

## The question

Which rendering API should the web terminal use: canvas 2D or WebGL2? WebGPU is deferred, below.

## What it is

- **`record/demo.py`** writes varied terminal output:
  - 16, 256 and truecolor colours;
  - attributes and an OSC 8 link;
  - CJK, emoji (including a ZWJ sequence), combining marks and box drawing;
  - a 4000-line coloured scroll;
  - 6 s of full-screen truecolor animation at 30 fps in the alternate screen, using synchronized output.
- **`convert/`** feeds that recording through Cleat's own `GhosttyVtEngine`.
  - It takes a render update every 1/60 s of recording time while output arrived, holding frames during synchronized output as Cleat's host does.
  - It writes the frames as a CBOR sequence in the shape from "The screen-state wire format" (#430): integer-keyed maps, interned styles, per-row text arrays and style runs.
- **`web/`** decodes the frames (`cbor-x`), applies them to a cell model, and draws the dirty rows once per animation frame. It has two renderers over the same model:
  - **canvas 2D:** background runs plus `fillText`, with same-style ASCII runs drawn as one string;
  - **WebGL2:** a glyph atlas rasterised on demand through a 2D canvas, one instanced quad per cell, a background pass and a glyph pass, and only dirty rows uploaded.

  Modes: real-time playback, as fast as possible (with a raster or GPU sync per frame), and `__multi(n)`, which plays N terminals at once.

## Findings (23 October 2026, Chrome via Playwright, Apple M4, ANGLE Metal, dpr 2)

**1. Wire size.**
- 187 frames: 8.8 MB of VT input becomes **3.4 MB of CBOR**.
- The same updates in Cleat's current postcard form are **25 MB**, because every cell carries its full style.
- Interning styles is the big win.
- Decoding with `cbor-x` costs 0.09 to 0.13 ms per frame.

**2. Style interning needs eviction.** The truecolor animation made 939 distinct styles in 9 s. The protocol needs id reuse or a bounded table that the client can be told to drop entries from.

**3. One terminal: both renderers are easily fast enough.**

| | per present (real time, CPU) | as fast as possible |
|---|---|---|
| Canvas 2D | 1.44 ms mean, 2.3 ms p95 | 582 frames/s |
| WebGL2 | 0.22 ms mean, 0.4 ms p95 | 816 frames/s |

There were no late animation frames with either renderer.

**4. Many terminals separate them.** N terminals all running the full-screen animation, each with its own canvas:

| | 16 terminals | 32 terminals |
|---|---|---|
| Canvas 2D, per frame | 7.1 ms mean, 11.8 ms p95 | **37.5 ms mean; 128 of 129 frames late** |
| WebGL2, per frame | 1.4 ms mean, 3.0 ms p95 | 2.3 ms mean, but **only 16 contexts survive** |

- Chrome caps live WebGL contexts at 16 and drops the oldest ones, so a context per terminal cannot work for an overview.
- WebGL therefore needs **one shared context**, drawing every visible terminal into its own region. That is like native, where one renderer draws all Views.
- Canvas 2D degrades badly past about 16 busy terminals.

**5. Rendering quality** was correct in both renderers on the test screen: palette, truecolor, bold, dim, italic, underline, curly, inverse, strike, wide CJK, colour emoji including ZWJ, and combining marks. Not done:
- curly versus straight underline is not distinguished;
- links are not drawn differently;
- no selection, no cursor styles (the cursor is a DOM overlay), no images.

**6. WebGPU is not tried.** WebGL2 runs everywhere, including Safari. Its cell-instancing design ports to WebGPU later if wanted.

## Verdict

_Pending with Robert:_ WebGL2, with one shared context per page and a canvas 2D fallback?
