# Rich-text renderer: prior art (researched 2026-10-31)

Scope: shaping/layout stacks, virtualized transcript UIs, streaming markdown, TUI markdown,
"one core, many frontends" precedents, accessibility text model. Facts below were read from
primary sources (repo source via `gh api`, docs.rs, project docs) on 2026-10-31 unless marked
**[memory]** (my prior knowledge, not re-verified today).

---

## 1. Text layout / shaping stacks

### Feature matrix (what each layer actually does)

| Stack | Lang / licence | Shaping | Bidi | Line break | Para layout | Inline boxes | Hit test / selection rects | C ABI |
|---|---|---|---|---|---|---|---|---|
| parley 0.12 (+fontique, harfrust, skrifa, ICU4X) | Rust, Apache-2.0/MIT | yes (HarfRust) | yes | yes (ICU4X, CSS line-break) | yes (CSS-ish line boxes, align, justify) | **yes** (`InlineBox`, baselines, vertical-align) | **yes** (`Cursor`, `Selection::from_point`, `Selection::geometry_with`) | none; you'd write one |
| parley_engine 0.12 (new) | Rust | yes | levels | break opportunities | **no** (caller does it) | n/a | n/a | none |
| cosmic-text 0.19 | Rust, Apache-2.0/MIT | HarfRust | yes | yes | yes (per paragraph `BufferLine`) | **no** (not in README/src listing) | yes (Buffer hit, cursor) | none |
| HarfBuzz 15.0.0 | C/C++, Old-MIT | yes | no | no | no | n/a | n/a | native |
| HarfRust 0.15.0 | Rust, MIT | yes (port of HB 15) | no | no | no | n/a | n/a | none |
| kb_text_shape 2.28g | C single header, Zlib | yes | simplified (LTR/RTL runs; **no** explicit embeddings, overrides or isolates) | break flags only | **no** | n/a | n/a | native |
| SkParagraph (Skia) | C++, BSD-3 | HarfBuzz | ICU/SkUnicode | yes | yes | **yes** (`addPlaceholder`, `getRectsForPlaceholders`) | **yes** (`getRectsForRange`, `getGlyphPositionAtCoordinate`) | C++ only, drags Skia |
| Pango 1.5x | C, LGPL-2.1+ | HarfBuzz | FriBidi | yes | yes | **yes-ish** (`PangoAttrShape` w/ ink+logical rect) | yes (`xy_to_index`, `index_to_pos`) | native but GLib |
| ICU4X 2.4 | Rust, Unicode-3.0 | no | bidi props (+ unicode-bidi crate for UBA) | **yes** (icu_segmenter: line/word/grapheme/sentence, dictionary/LSTM for SEA) | no | n/a | n/a | **official C/C++ FFI** (Diplomat, `ffi/capi`) |
| unicode-bidi 0.3.18 | Rust, MIT/Apache | – | UBA | – | – | – | – | none |

### parley (linebender) — the most complete Rust rich-text layout
- Repo: https://github.com/linebender/parley — v0.12.0 released 2026-10-09; last commit 2026-10-30.
  Changelog: https://github.com/linebender/parley/blob/main/CHANGELOG.md
- Shaping moved from swash to **HarfRust**; font parsing via **skrifa/read-fonts**; Unicode analysis
  via **ICU4X**; font enumeration/fallback via **fontique** (system backends incl. DWrite/CoreText/
  fontconfig). swash is no longer in the shaping path (it remains a rasterizer elsewhere).
- Architecture (docs.rs/parley 0.12): `FontContext` (font DB) + `LayoutContext` (scratch) are long-lived;
  build a `Layout` with `RangedBuilder` (flat spans), `TreeBuilder` (nested spans, CSS whitespace collapse)
  or `StyleRunBuilder` (style table + runs). `Layout` does shaping, line breaking, bidi reorder,
  alignment; can **re-break/re-align without reshaping**, but any text/style change requires a rebuild.
  Unit of work is one paragraph-ish `Layout` — there is no document model, no incremental append.
- 0.12 highlights: new **`parley_engine`** crate (analysis + shaping with caller-provided font selection;
  line breaking/stacking left to caller — aimed at people who want their own line breaker; "reshaping
  of lines" at ligature/cursive breaks still in progress). CSS-Text-4 whitespace hanging, CSS 2.2
  `vertical-align` for spans **and inline boxes**, inline box baselines, `Cluster` = extended grapheme
  cluster, CSS line-box model for mixed-size lines.
- Inline boxes: `InlineBox` with `InlineBoxKind` (in-flow / out-of-flow / custom-out-of-flow); you
  give id + size (+ baseline in 0.12); after layout you read its position back. Exactly the
  "placeholder for an embedded view" primitive.
- Editing/hit-test: `parley::editing::{Cursor, Selection, PlainEditor}`; `Selection::from_point(layout,x,y)`,
  `Selection::geometry_with(layout, |rect, line_ix|)` (allocation-free, made for immediate-mode).
- **AccessKit integration was removed from parley in 0.12** (moved into the `editor` example to copy/adapt);
  they instead made public the pieces needed (`Cursor::upstream_cluster`, `ClusterPath::new`, etc.).
- Consumers: Slint now defaults to `shared-parley` (internal/core/Cargo.toml has
  `parley = 0.12`, plus `accessibility-text = [shared-parley, accesskit]`); Floem uses parley; Blitz
  (Dioxus HTML/CSS engine) uses parley; Xilem/Masonry use parley.
- What it gets wrong / gaps for us: churny API (many breaking changes per release); no C ABI; one
  `Layout` per paragraph so a document is your problem; MSRV 1.88; full ICU4X data → binary size.

### cosmic-text (System76)
- https://github.com/pop-os/cosmic-text — tag 0.19.0; last commit 2026-10-27. Apache-2.0/MIT.
- Shaping HarfRust, rasterization swash, own bidi layout, Chromium/Firefox fallback lists.
- Data structure: `Buffer { lines: Vec<BufferLine>, metrics, width/height, scroll, wrap, ... }`;
  `BufferLine { text: String, attrs_list, shape_opt: Cached<ShapeLine>, layout_opt: Cached<Vec<LayoutLine>> }`
  — i.e. per-paragraph shape cache + per-width layout cache, invalidated by `set_text`. Good model for
  logs (append paragraph = push BufferLine). Has `shape_run_cache.rs`.
- Weaknesses: `Vec<BufferLine>` (no tree, O(n) prefix sums for height); single buffer `Metrics`
  (line height) historically; no inline boxes; README roadmap still lists bidi selection,
  hinting etc. as not-done. Built for an editor/terminal, not for rich documents.
- Zed's `gpui_web` uses cosmic-text compiled to wasm as its web text system (see §5).

### HarfBuzz 15.0.0 / HarfRust 0.15.0
- HarfBuzz (released 2026-10-22): the reference shaper, C, Old-MIT. Shaping only; no bidi, no
  line breaking, no layout.
- HarfRust (https://github.com/harfbuzz/harfrust, 0.15.0 2026-10-22, MIT): fork of RustyBuzz moved
  onto `read-fonts`; tracks HarfBuzz 15.0.0; no FreeType/CoreText/DWrite/ICU/Graphite glue; errors on
  malformed fonts rather than falling back. Now under the harfbuzz org = de facto Rust shaper.

### kb_text_shape (JimmyLefevre/kb)
- https://github.com/JimmyLefevre/kb — `kb_text_shape.h` v2.28g (commit 2026-10-20), ~31.9k lines,
  Zlib, ~1.2k stars. Single-header C, `kbts_` prefix, custom allocator hooks (`KBTS_MALLOC/FREE`,
  `KB_TEXT_SHAPE_NO_CRT`).
- Does: simplified bidi (LTR/RTL runs; **no** explicit embeddings, overrides or isolates per the header's "OTHER LIMITATIONS", `kb_text_shape.h:1537-1547` at 2.28g; corrected 2026-10-31, an earlier version of this note said otherwise),
  script/grapheme/word/line(soft/hard) segmentation emitted as `kbts_break_flags`, OpenType shaping
  for complex scripts (Arabic, Indic, Khmer, Myanmar, USE…), font coverage checks.
- Does **not**: rasterize, select system fonts / fallback, or do paragraph layout ("glyphs on a
  single unbounded line"). You write line fitting, alignment, hit testing.
- Header says: **"NO SECURITY GUARANTEE whatsoever. DO NOT use it on untrusted font files."**
  Relevant if Wheelhouse renders arbitrary system/user fonts (mostly trusted system fonts — OK-ish).
- Fit for Wheelhouse: very good stylistically (C, arena-friendly, single header, no deps) — the C
  analogue of `parley_engine`. Would replace FreeType-side shaping; DWrite/CoreText backends could
  keep platform shaping or also use kbts for consistency.

### SkParagraph
- https://github.com/google/skia/blob/main/modules/skparagraph/include/Paragraph.h
- API: `layout(width)`, `paint`, `getRectsForRange(start,end,heightStyle,widthStyle)`,
  `getRectsForPlaceholders()`, `getGlyphPositionAtCoordinate(dx,dy)` → `PositionWithAffinity`,
  `getWordBoundary`, `getLineMetrics`, `getClosestGlyphClusterAt`, `visit/extendedVisit` per run,
  experimental `updateFontSize/updateForegroundPaint` (re-style without rebuild).
  Placeholders via `ParagraphBuilder::addPlaceholder(PlaceholderStyle)` with alignment kinds
  (baseline / above / below / top / bottom / middle) **[memory for alignment list]**.
- This is the engine under Flutter's text (and Flutter's `WidgetSpan` = placeholder). The best
  reference API shape for "paragraph with embedded widgets". Cost: Skia + ICU dependency, C++.

### Pango
- C, LGPL; HarfBuzz + FriBidi; `PangoLayout` with `pango_layout_xy_to_index`,
  `pango_layout_index_to_pos`, line iterators. Inline objects via `PangoAttrShape`
  (`pango_attr_shape_new_with_data`: ink/logical rect + user data per *character* — you insert U+FFFC
  and attach a shape attr). Works, but GLib/fontconfig heavy and LGPL; poor fit for an arena C
  codebase targeting Win/Mac. https://docs.gtk.org/Pango/type_func.AttrShape.new_with_data.html

### ICU4X / unicode-bidi
- ICU4X 2.4.0 (2026-10-22): `icu_segmenter` (line/word/grapheme/sentence incl. dictionary/LSTM for
  Thai/Lao/Khmer/Burmese), properties incl. bidi class; **official C/C++ bindings** via Diplomat
  (`ffi/capi` in repo). The only Unicode-segmentation library here with a supported C ABI besides ICU4C.
- unicode-bidi 0.3.18 (servo) — UBA implementation used by many Rust stacks.

### Other C-side options worth knowing **[memory]**
- SheenBidi (C, Apache-2.0) — UBA; libunibreak (C, zlib) — UAX#14/29; libraqm (C, MIT) —
  HarfBuzz+bidi glue for a single run, no line breaking. A C-only stack = kb_text_shape alone, or
  HarfBuzz + SheenBidi + libunibreak + own line fitter.

---

## 2. Virtualized rich document / transcript UIs

### Zed GPUI `list` (variable height) — most directly relevant
Source: https://github.com/zed-industries/zed/blob/main/crates/gpui/src/elements/list.rs (read 2026-10-31)
- `ListState(Rc<RefCell<StateInner>>)`; items in a **`SumTree<ListItem>`** with summary
  `{count, rendered_count, unrendered_count, height, has_focus_handles, has_unknown_height}`;
  seeks by `Count` and `Height` dimensions → O(log n) index↔pixel.
- `enum ListItem { Unmeasured { size_hint: Option<Size>, focus_handle }, Measured { size, focus_handle } }`.
  Unmeasured items contribute `size_hint` or **0** (so scrollbar is wrong until measured;
  `is_scrolled_to_end()` returns `None` if any item has unknown height). `with_uniform_item_height`
  seeds hints. Width change drops hints → re-measure.
- Scroll position is logical: `ListOffset { item_ix, offset_in_item }` (anchor), not pixels.
  `splice(range, count)` shifts the anchor index; if the anchor was inside the replaced range it
  snaps to range start.
- `remeasure()` (all, proportional anchoring: keeps fraction-within-item) vs
  `remeasure_items(range)` (absolute pixel anchoring). Test: 40px into 100px item → 20px into 50px.
- `overdraw` (pixels measured beyond viewport both directions) avoids pop-in.
- **Measurement readiness predicate**: `set_item_measurement_readiness(|ix| ready)`; if not ready
  the measurement is stored only as a hint and the anchor is "provisional" → no scroll jumps while
  an item's async content (markdown parse) is pending.
- **`FollowMode::Tail`**: following snaps logical top to item count; any upward scroll stops
  following; re-engages when a non-provisional layout ends within ~1px of bottom or the scrollbar is
  dragged to the end; `pause_following_tail()`. Scrollbar drag freezes reported content height.
- `uniform_list`: measures one item, `height*count`, renders visible range only.
- SumTree (crates/sum_tree): B+-tree, `TREE_BASE = 6`, traits `Item::summary`,
  `Summary::add_summary`, `Dimension`, `SeekTarget` — same structure backs Zed's rope, display
  maps, etc. Warp's editor used the same SumTree design (Nathan Sobo) per
  https://www.warp.dev/blog/how-warp-works (2021).

### Zed agent panel (how it uses the list)
Source: crates/agent_ui/src/conversation_view.rs, conversation_view/thread_view.rs
- `ListState::new(0, ListAlignment::Top, px(2048.0))` (2048px overdraw!), `set_follow_mode(Tail)`,
  readiness = `!entry.has_pending_markdown_render()`.
- `AcpThreadEvent::NewEntry` → `splice_focusable(ix..ix, [focus])`;
  `AcpThreadEvent::EntryUpdated(ix)` → `remeasure_items(ix..ix+1)`. I.e. **one list item per
  message/tool call; the streaming message is re-measured as a whole each update**.
- Expanding a collapsed element while following → `pause_following_tail()` so the user isn't yanked.
- Restores `ui_scroll_position: ListOffset` per thread.

### Zed `markdown` crate (streaming)
Source: crates/markdown/src/markdown.rs (~363KB file)
- `append(&mut self, text)` = `self.source = source + text; self.parse()` → **full reparse of the
  whole message** on a background thread with pulldown-cmark; coalesces (`should_reparse`) if a
  parse is in flight; keeps old render visible until new parse completes.
- Code-block highlighting reused across streaming reparses (test
  `test_code_block_highlights_reused_when_streaming`); mermaid render cache keyed by contents;
  per-block state (scroll, wrap, mermaid zoom) keyed by **source offset**.
- No special partial-markdown handling (no "remend"-style closing).
- Rendered as one `MarkdownElement` with `RenderedMarkdown` → lines (`WrappedLineLayout`), with
  `source_index_for_position`/`position_for_source_index` mapping for selection across the whole
  message; `copy_as_markdown` uses source ranges. Selection does not span list items (messages).
- Get-wrong: full reparse per token is O(n) per chunk → O(n²) per message; tolerable because
  messages are bounded and parse is off-thread.

### VS Code chat panel
Source: src/vs/workbench/contrib/chat/browser/widget/chatListWidget.ts, chatListRenderer.ts
- A `WorkbenchObjectTree` over VS Code's ListView (DOM-recycling virtual list). `ChatListDelegate
  extends CachedListVirtualDelegate`: `hasDynamicHeight = true`, `estimateHeight = element.currentRenderedHeight ?? defaultElementHeight`;
  measured heights fed back via `updateElementHeight` from ResizeObservers.
- Stick-to-bottom: `_scrollLock` (default true) + `isScrolledToBottom = scrollTop+renderHeight >= scrollHeight-2`;
  `_withPersistedAutoScroll` around height updates; explicit anchoring helpers
  (`getAnchoredScrollTop`, `preserveScrollPosition(target)`) for user-toggled expand/collapse;
  "bottom padding" reservation so collapses don't jump.
- **Progressive rendering**: it deliberately renders the streaming response at an estimated
  words/sec rate (`getProgressiveRenderRate`, clamped, uses `impliedWordLoadRate`), switching to a
  fast drain when complete. Smooths bursty token arrival.
- Lots of special-case code (re-entrant measurement deferral `deferReMeasure`, image-load
  re-measure) — evidence that dynamic-height virtualization in DOM is fiddly.

### Warp
- Blocks = one Alacritty-derived grid per command (https://www.warp.dev/blog/how-warp-works, 2021).
  Custom Rust UI framework with Nathan Sobo (pre-GPUI lineage), Metal; SumTree in editor.
  Nothing more specific published on block virtualization that I found.

### Textual (Python TUI)
- Streaming markdown post (Will McGugan, 2025-07-24): https://willmcgugan.github.io/streaming-markdown/
  Each block (paragraph, fence, table) is a widget; on append only the **last top-level block can
  change**; stores the line where the last block starts and reparses only from there (<1ms
  regardless of doc size); updates last widget in place if type unchanged, replaces it if the type
  changed (paragraph → table when a full row arrives); **buffers tokens** between producer and widget
  so display lags by ms, not a backlog. Shipped as `Markdown.append` in Textual 4.0 ("The Streaming
  Release"; PR https://github.com/Textualize/textual/pull/5950). Parser: markdown-it-py (non-streaming).
- `Log`/`RichLog` use the Line API (render only visible lines) **[memory]**; Markdown widget itself is
  not virtualized (all block widgets mounted) **[memory]**.

### Xi-editor
- Front-end line cache protocol (https://xi-editor.io/docs/frontend-protocol.html): `update` with
  ops `copy/skip/invalidate/ins/update` over an old-lines array; `measure_width` request — **core
  asks the frontend for string widths** to do line breaking.
- Retrospective (Raph Levien, 2020): https://raphlinus.github.io/xi/2020/06/27/xi-retrospective.html
  "I now firmly believe that the process separation between front-end and core was not a good idea";
  scrolling over a cache "took months"; word-wrap vs edit races and tearing; async complicated
  everything; the rope was the clear success. Directly relevant to "Rust core + C renderer" if the
  boundary is *async*; less so if it is an in-process synchronous C ABI.

### Masonry / Xilem `VirtualScroll`
- xilem repo `masonry/src/widgets/virtual_scroll.rs`; Xilem 0.4.0 (2025-10-29), active.
- Dense `usize` ids; anchor index + offset; emits `VirtualScrollAction::Fetch{old_active, target}`
  and the app/driver must add/remove children. Extent bookkeeping delegated to
  **`understory_virtual_list`** 0.1.2 (forest-rs/understory, 2026-10-01): `ExtentModel` trait with
  `FixedExtentModel`, `PrefixSumExtentModel` (lazy prefix sums), `SparsePrefixSumExtentModel`
  (fixed placeholder extent for unmaterialized items), plus `TailAnchoredExtentModel` for chat/log
  stick-to-end. Recommended loop: estimate → measure → `set_extent` → query via
  `index_at_offset_for_len`.
- Self-declared "minimum viable": no focus handling across virtualization, no scrollbar yet, a11y for
  virtualized lists "not yet designed", transforms ignored.

### Floem (Lapce)
- `virtual_stack` / `virtual_list` views (item size fixed or per-item fn **[memory for API detail]**);
  text via parley (Cargo.toml). Lapce editor uses its own line-cache-ish layout.

### Flutter slivers **[mostly memory]**
- `RenderSliverList` lazily lays out children around the scroll offset; max scroll extent is
  **extrapolated from the average extent of laid-out children** (`estimateMaxScrollExtent` on the
  child manager); when children above the viewport turn out a different size, the sliver returns
  `SliverGeometry.scrollOffsetCorrection` (verified: "If this is non-zero after performLayout returns,
  the scroll offset will be adjusted by the parent" and layout reruns). Chat UIs use `reverse: true`
  (offset 0 = bottom) so appending at the "start" never shifts; `CustomScrollView(center:)` anchors a
  sliver for bidirectional growth.

### Android RecyclerView **[memory]**
- `LinearLayoutManager` picks an `AnchorInfo` (adapter position + coordinate) each layout and fills
  both directions from it; `stackFromEnd`/`reverseLayout` for chat; saved state = anchor position +
  offset; stable ids + `DiffUtil` so inserts above don't move the anchor. The "anchor = (index,
  offset)" model is the same as GPUI's `ListOffset`.

### Web: CSS scroll anchoring + content-visibility
- CSS Scroll Anchoring L1 (Editor's Draft 2026-09-30): https://drafts.csswg.org/css-scroll-anchoring/
  pick a deep visible anchor node; on layout change measure its block-start before/after and adjust
  scroll by the delta; suppressed when offset is 0 (top) or when positioning props between anchor
  and scroller change; `overflow-anchor: none` opt-out. Says nothing about being pinned at the
  bottom — stick-to-bottom must be done by the app (or `flex-direction: column-reverse` trick).
- `content-visibility: auto` + `contain-intrinsic-size: auto <len>`: skip rendering offscreen
  subtrees; placeholder size until first render, then the **remembered last rendered size** is used
  while skipped. https://developer.mozilla.org/en-US/docs/Web/CSS/contain-intrinsic-size
  This is exactly "estimate until measured, then cache measured height", built into the browser.

### Synthesis — common answers
- Height of unmeasured items: (a) 0 (GPUI default — bad scrollbar), (b) a uniform/default estimate
  (VS Code `defaultElementHeight`, understory sparse placeholder, CSS intrinsic size), (c)
  extrapolated average (Flutter), (d) remembered last measured (CSS `auto`, VS Code
  `currentRenderedHeight`, GPUI hint kept across `remeasure`). Cheap content-derived estimates
  (lines × line height from byte length / width) are not used by any of these generically but are
  easy for text.
- Anchoring: everyone ends up at **(item index, offset within item)**; on remeasure adjust by delta
  above the anchor. GPUI distinguishes proportional vs absolute offset.
- Stick-to-bottom: explicit follow state, broken by user upward scroll, re-armed on reaching bottom
  (GPUI `FollowMode::Tail`, VS Code `scrollLock`, understory `TailAnchoredExtentModel`); Flutter/
  Android sidestep via reversed layout.
- Last-item incremental re-layout: everyone remeasures the whole last item (GPUI
  `remeasure_items(ix..ix+1)`); none re-lays out only the last paragraph of the last item — that is
  done at the markdown level (Textual's block widgets; Streamdown memoized blocks).
- Don't confirm height while async content pending (GPUI readiness predicate; VS Code re-measure
  on image load).

---

## 3. Markdown parsing for streaming

| Parser | Lang / licence / version | Model | Source positions | Incremental |
|---|---|---|---|---|
| pulldown-cmark | Rust, MIT, v0.13.6 (2026-10-30) | pull event iterator, no AST | yes (`into_offset_iter`) | no |
| comrak | Rust, BSD-2 (COPYING), v0.59.0 (2026-10-26) | arena AST (cmark-gfm port) | yes (sourcepos) | no |
| markdown-rs | Rust, MIT, 1.0.0 (2025-04-23; no pushes since) | mdast AST, MDX | yes | no |
| md4c | C, MIT, tag v0.6.0, active 2026-10 | SAX callbacks (enter/leave block/span, text) | **no** (issue #91 "Provide source information" still open) | no |
| tree-sitter-markdown | C (generated), MIT, v0.5.4 (2026-10-24) | separate block + inline grammars, CST | yes | **yes** (tree-sitter edits) — but not CommonMark-exact |
| markdown-it-py | Python (Textual/rich) | token stream | line maps | no |
| marked | JS (Streamdown) | lexer tokens with `raw` | raw text | no |

Streaming strategies seen:
1. **Full reparse per chunk off-thread, coalesced** — Zed (pulldown-cmark). Simple; O(n²) per message.
2. **Stable-prefix / last-block reparse** — Textual: remember start line of the last top-level
   block, reparse from there; earlier blocks frozen. Must handle last-block type changes (para →
   table, setext heading, lazy continuation) and constructs that reach backward (link reference
   definitions, footnotes, loose/tight list determination).
3. **Block split + memoize** — Streamdown (vercel/streamdown, streamdown@2.7.1 2026-10-19):
   `marked.lexer(md).map(t => t.raw)` → each block memoized by content, only changed (trailing)
   blocks re-render; whole doc as one block when footnotes present (HF chat-ui fork of
   parse-blocks.ts). Docs: https://streamdown.ai/docs/memoization
4. **Repair the unterminated tail before parsing** — `remend` (packages/remend): close unmatched
   `**`/`*`/`_`/`***`/`` ` ``/`~~`/`$$`, give incomplete links a placeholder href
   (`streamdown:incomplete-link`), drop incomplete images, escape `20~25`; inline `$` opt-in;
   priority-ordered handlers; conservative (only when unambiguous). Runs on raw string pre-parse.
5. **Newline-gated commit** — OpenAI Codex CLI TUI (codex-rs/tui/src/markdown_stream.rs):
   `MarkdownStreamCollector` buffers deltas and only commits up to the last `\n`; incomplete line
   is held back so text doesn't change meaning; final flush appends a newline. Combined with
   writing committed lines into terminal scrollback.
6. **Throttle/buffer** — Textual buffers tokens while an update is running; VS Code paces at an
   estimated words/sec.

---

## 4. Terminal-side rich text
- **rich** (Python, MIT, v15.0.0 2026-04): `rich.markdown` via markdown-it-py; tables → rich `Table`
  (box drawing, wraps cells); code via Pygments; links via OSC 8 hyperlinks; images not rendered
  (alt/link) **[memory for image detail]**.
- **Textual** (v8.2.8): block-per-widget Markdown, streaming append (above); tables are widgets
  (scroll horizontally).
- **glamour** (charmbracelet, Go, MIT, v2.0.1 2026-06): goldmark → ANSI string with stylesheets
  (JSON themes), word-wrap at width; tables via own renderer; images shown as link text **[memory]**.
  `glow` is the CLI on top. Output is a static string — no hit testing, no virtualization.
- **tui-markdown** (joshka, Apache-2.0, "experimental Proof of Concept"): pulldown-cmark →
  `ratatui::text::Text`, syntect highlighting. Text only; tables/images limited.
- **ratatui-image** (MIT, active 2026-10): probes terminal for Kitty / Sixel / iTerm2 protocols,
  falls back to half-blocks; queries cell pixel size to map image → cells. Works in immediate-mode
  ratatui but images are out-of-band state (must track placement/ids, clear on scroll).
- **Codex CLI**: renders committed markdown lines into native terminal scrollback (no in-app
  virtualization for history; the terminal is the virtualizer).
- Degradation pattern everywhere: proportional fonts → monospace cells; inline images → block-level
  placeholders or protocol images at cell granularity; tables → box-drawn with wrapping or
  horizontal scroll; selection → terminal-native (can't select across app-drawn widgets); width
  measurement = `unicode-width`/wcwidth (East Asian Wide, emoji ambiguity), not shaping.

---

## 5. "One core, many frontends" for rich text

- **Zed GPUI web (`crates/gpui_web`, first commit 2026-08-26, active)**: wgpu renderer +
  `WebTextSystem { native: CosmicTextSystem, canvas fallback }` — i.e. ships a **wasm shaper
  (cosmic-text/HarfRust) + bundled fonts**, and falls back to **Canvas2D `OffscreenCanvas`**
  measuring/rasterizing per glyph for things its fonts don't cover (separate font-id namespace,
  4096-entry measurement cache). Plus `ime_mirror.rs`: a hidden `<textarea>` mirroring a window of
  document text so browser IMEs/autocorrect work, writes coalesced once per event-loop turn. This is
  the concrete cost of "same layout on web": fonts + shaper in wasm, a fallback path, an IME shim,
  and (not seen) a11y via DOM is absent.
- **Flutter web**: CanvasKit/Skwasm only per current docs (https://docs.flutter.dev/platform-integration/web/renderers);
  the HTML/DOM renderer was deprecated and dropped **[memory: ~2024–25]**. Ships Skia wasm (multi-MB)
  and downloads fonts (Roboto/Noto fallback) **[memory]**. Accessibility via a parallel semantics DOM.
- **Compose Multiplatform web** **[memory]**: Skia via skiko wasm to canvas; same trade-offs.
- **Slint**: one Rust core, multiple renderers (femtovg/GL, Skia, software, Qt); text now via
  shared parley for most renderers; `accessibility-text` feature wires parley → AccessKit.
- **Xi**: core/frontend over JSON; frontend measured widths (`measure_width`) → round-trips and
  races; author regrets the split (above).
- **C-ABI layout cores with frontend measurement** **[memory]**: Yoga (`YGMeasureFunc`), Clay
  (`Clay_SetMeasureTextFunction`), Taffy (measure closure) — the core never shapes; it calls back for
  "how big is this text at width w". Clean for boxes, but per-line hit testing/selection then also
  lives in the frontend.
- **AccessKit C bindings** (https://github.com/AccessKit/accesskit-c): C API implemented in Rust,
  header by cbindgen — a precedent for a Rust core exposed to C hosts.

Lessons for text specifically:
1. Whoever shapes owns: line breaks, heights, hit testing, selection rects, caret movement,
   a11y character positions. Splitting "logical document in core" from "shaping in frontend"
   forces a measurement callback per paragraph at each width (Xi, Yoga/Clay) — fine if synchronous
   and in-process, painful if async/IPC.
2. Web cannot use native font stacks; either ship fonts + wasm shaper (Zed/Flutter) and lose
   native IME/a11y/find-in-page, or let the DOM do text and only share the *document model*
   (blocks, spans, embeds, virtualization indices) — measurement then comes from the DOM
   (ResizeObserver, like VS Code).
3. Terminal measures in cells; layout there is a different algorithm (wcwidth wrap), not a
   different backend of the same shaper.
So the shareable part across native/web/TUI is the **document model + markdown → block/span IR +
streaming/stable-block logic + virtualization bookkeeping (extent model, anchor, follow-tail)**;
the layout/shaping engine is per-frontend.

---

## 6. Accessibility text model (AccessKit 0.25.2)
Source: https://github.com/AccessKit/accesskit/blob/main/accesskit/src/lib.rs
- Text is exposed as `Role::TextRun` nodes (children of a text/paragraph node), each with `value`
  (UTF-8 text of the run; last run of a hard-broken line includes the `\n`).
- Per run the layout engine must supply:
  - `character_lengths: [u8]` — UTF-8 length of each *selectable character* (grapheme cluster; AK
    explicitly can't compute this itself); sum = `value.len()`.
  - `character_positions`, `character_widths: [f32]` — per-character offset/advance along
    `text_direction` in node coordinates (optional, but needed for magnifiers / caret tracking).
  - `word_starts: [u8]` — indices into character_lengths; AK won't compute words (must match the
    app's word motion).
  - `text_direction`, bounds/transform of the run, `next_on_line`/`previous_on_line` linking runs on
    the same visual line (bidirectional pointers).
- Selection: `TextSelection { anchor, focus }` of `TextPosition { node: run id, character_index }`
  set on the containing node.
- Implication: the layout engine must expose **visual lines → runs → grapheme clusters with
  positions**, a stable run id scheme, and mapping between document offsets and (run, char index).
  For a virtualized transcript, offscreen items still need some a11y representation (Masonry
  explicitly hasn't designed this; Flutter/Android expose only realized children plus scroll
  semantics **[memory]**).
- parley 0.12 removed its built-in AccessKit glue (now in the `editor` example); Slint has
  `accessibility-text` = parley + accesskit.

---

## Uncertainties / not verified
- Flutter, RecyclerView, Compose, rich/glamour image handling, Textual Log internals, Floem API
  details: from memory.
- SkParagraph placeholder alignment enum list from memory; Paragraph.h methods verified.
- Did not read Zed's full markdown renderer (363KB) — inline image/embedded element handling inside
  a single `MarkdownElement` not checked.
- Streamdown's current upstream `parse-blocks` source not read directly (seen via Lobe UI /
  HF chat-ui copies).
