# Rich text: what Copy puts on the clipboard

Status: research notes, 31 October 2026. Nothing here is decided. The decisions taken from it are recorded on [the Rich Text map](https://github.com/flotilla-org/wheelhouse/issues/537). Companion to
[rich-text-prior-art.md](rich-text-prior-art.md) and
[rich-text-current-state.md](rich-text-current-state.md).

Question: when a reader copies a selection of rendered rich text or markdown,
what do comparable apps write to the clipboard, and what pastes well elsewhere?
The answer feeds the Copy design for the read-only Rich Text view.

Facts were read from primary sources on 31 October 2026. Source code is cited
as `path` at a commit. Closed-source apps (Obsidian, ChatGPT, Claude.ai,
JetBrains, Notion, Slack, Linear, Warp) are cited from their docs, changelogs
or issue trackers. Claims marked **[unverified]** come from memory or from
user reports I could not check against code or the clipboard.

## Sources

- Zed `zed-industries/zed` at `ba63a022c9` (2026-10-31): `crates/markdown/src/markdown.rs`,
  `crates/markdown/src/selection.rs`, `crates/agent_ui/src/conversation_view/thread_view.rs`,
  `crates/markdown_preview/src/markdown_preview_view.rs`,
  `crates/gpui_linux/src/linux/wayland/clipboard.rs`, `crates/gpui_linux/src/linux/x11/clipboard.rs`,
  `crates/gpui/src/platform.rs`, `assets/keymaps/default-linux.json`.
  Selection-to-markdown landed in #60657 (commit `8f907c5ee1`, 2026-07-09).
- VS Code `microsoft/vscode` at `0861b77b3f`: `src/vs/workbench/contrib/chat/browser/actions/chatCopyActions.ts`,
  `.../chat/browser/widget/chatClipboard.ts`, `.../chat/browser/widget/chatListWidget.ts`,
  `.../chat/browser/actions/chatCodeblockActions.ts`,
  `src/vs/workbench/contrib/notebook/browser/viewModel/cellOutputTextHelper.ts`.
  Hidden and collapsed content left out of copies in commits `6f5da90286`, `246f47b0d3`
  and `63d8806516` (all 2026-10-29).
- Obsidian 1.12.4 desktop changelog, 2026-02-27:
  https://obsidian.md/changelog/2026-02-27-desktop-v1.12.4/ ; forum threads
  https://forum.obsidian.md/t/ctrl-c-cmd-c-in-reading-mode-should-copy-the-rendered-version-html/106346 and
  https://forum.obsidian.md/t/copy-and-paste-rendered-version-no-markdown-from-the-editor/74252?page=2
- WHATWG HTML, `innerText` rendered text collection steps:
  https://html.spec.whatwg.org/multipage/dom.html#rendered-text-collection-steps
- W3C Clipboard API and events, Editor's Draft 24 June 2026: https://w3c.github.io/clipboard-apis/
- GitHub `github/paste-markdown` (`src/paste-markdown-text.ts`, `src/paste-markdown-html.ts`, README),
  `github/quote-selection` README.
- LibreOffice `LibreOffice/core` at `a36c4c5293`: `vcl/qt5/QtTransferable.cxx`,
  `sot/source/base/exchange.cxx`; commit `a3eb702383` "Add markdown under paste special for
  backends requiring qt" (2026-03-23).
- Issue trackers: anthropics/claude-code #54670 (open, 2026-04-29) and #63363;
  Cursor forum https://forum.cursor.com/t/copy-raw-markdown-from-assistant-responses/150557 ;
  OpenAI community https://community.openai.com/t/bad-chatgpt-s-markdown-in-copy-paste-working-solution/1405669
- Vendor docs: Linear https://linear.app/docs/editor and https://linear.app/docs/exporting-data ;
  Slack https://slack.com/help/articles/360039953113 ; Notion https://www.notion.com/help/import-data-into-notion ;
  Warp https://docs.warp.dev/terminal/blocks/block-actions ; iTerm2
  https://iterm2.com/documentation-menu-items.html ; JetBrains https://jetbrains.com/guide/ai/tips/use-code-from-chat

## Summary

- **Plain Copy writes rendered text, not markdown,** in every native renderer read: Zed, VS
  Code chat, the browser default, and Obsidian's reading view on mobile. Zed writes only
  `text/plain`. Browser-based views (VS Code chat and markdown preview, ChatGPT, Claude.ai)
  add a `text/html` fragment.
- **Copy as Markdown is a separate command where it exists, and Zed slices the raw source.**
  It does not generate markdown from the tree. It snaps the selection out of inline delimiters
  and rebalances them so the result is always valid markdown.
- **Message copy buttons copy markdown** as `text/plain` in Zed, VS Code and Claude.ai. Users
  of tools that lack one keep asking for it (Claude Code #54670, Cursor forum). Reasoning
  ("thoughts") is left out.
- **Code block buttons copy the bare code** with no fences, everywhere.
- **Nobody offers `text/markdown` on the clipboard.** Web apps cannot write it natively.
  GitHub uses a private `text/x-gfm` for GitHub-to-GitHub paste. LibreOffice *pretends*
  plain text is markdown to offer "paste special as Markdown". Markdown travels as `text/plain`.
- **HTML hurts when it carries theme styling.** Obsidian's reading-view HTML pasted badly into
  Gmail, Outlook and Docs. VS Code rewrites links that only resolve locally before they reach
  the clipboard.

---

## 1. Obsidian (closed source; docs and forum)

- **Live Preview / Source mode** is a CodeMirror editor, so Copy gives the markdown source as
  `text/plain`. Since **1.12.4 (2026-02-27)**: "Copying text from the editor now includes HTML
  formatting, making it easier to paste into apps like Google Docs" (changelog). The editor now
  writes **markdown as `text/plain` plus rendered `text/html`**. The forum thread announcing it
  says "regular copy will also place an HTML version on the clipboard" (user WhiteNoise,
  2026-02-18).
- **Reading view** is rendered DOM, so the browser default applies (`text/html` plus
  `text/plain`). Desktop users report that the HTML carries theme font, colour and background,
  and that lists and headings break in Gmail, Outlook web and Google Docs (bwat47, 2025-06-18).
  Android reportedly copies plain text only. One report says "manually selecting text in
  Reading mode would include the MD marks" (2025-11-18). **[unverified; contradicts the
  others, may be version-specific]** One user says Ctrl-C with no selection copies the whole
  source document (v1.10).
- **Partial selection inside formatting:** no primary source. In the editor you get whatever
  source characters you selected, so you can end up with unbalanced `**`. **[unverified]**
- **Plugins fill the gaps:** *Copy Document as HTML* (with a "fragment only" option that drops
  theme styling), *Copy as HTML*, *Enhanced Copy* (keeps markdown when copying from reading
  view; regenerates HTML through the Obsidian API), *Copy Markdown Source* (source of the
  selection from either view), *Copy as source* (rendered HTML, prettified). That so many
  exist shows that no single default pleases everyone. A forum reply proposes a preference
  (HTML, plain, Markdown, RTF) rather than one behaviour.

## 2. Zed (source read)

All three surfaces (markdown preview, agent panel, hover popovers) share the `markdown` crate.

- **Copy (`markdown::Copy`, ctrl-c / ctrl-insert)**: `Markdown::copy` writes
  `RenderedText::text_for_range(selection)`. This is the **rendered text** of the laid-out
  lines, as a single `ClipboardItem::new_string` (`crates/markdown/src/markdown.rs` ~L1161).
- **Copy as Markdown (`markdown::CopyAsMarkdown`)**: context-menu entry in the markdown
  preview and the agent thread. It calls
  `selection::rebalanced_markdown_for_selection(source, events, root_block_starts, selection)`
  (`crates/markdown/src/selection.rs` L104). The steps:
  1. collect inline spans (emphasis, strong, strikethrough, sup/sub, links, code spans) from
     the parser events of the root blocks at each end of the selection;
  2. `snap_out_of_delimiters`, so a selection never starts or ends inside `**` or `` ` ``;
  3. if the selection lies wholly inside code spans, return the raw source slice;
  4. otherwise `rebalance_delimiters`: re-add the opening or closing delimiters of spans the
     selection cuts.

  So it is a **slice of the raw source**, repaired, and never regenerated from the tree. PR
  #60657 calls it "Improve selection copying to always yield valid markdown".
- **PRIMARY**: on mouse-up after a drag, Linux and FreeBSD builds call
  `cx.write_to_primary(...)` with the rendered text (`markdown.rs` ~L2544).
- **Code block copy button**: `CopyButton` writes `code` (the block's content, no fences)
  (`markdown.rs` ~L3650). Mermaid blocks copy their source (`mermaid.rs` L941).
- **Agent panel** (`thread_view.rs`):
  - "Copy This Agent Response" (icon button and context menu, ~L7980 and ~L8810) uses
    `get_agent_message_content`. It joins every `AssistantMessageChunk::Message` from the user
    turn as `block.to_markdown(cx)` with `\n\n`, and **skips `Thought` chunks**. Tool calls are
    not included (only `AssistantMessage` entries are visited).
  - User submissions have a "Copy" button that joins the text parts.
  - "Open Thread as Markdown" opens the whole thread in an editor buffer instead of copying.
- **Formats**: `ClipboardEntry` is `String | Image | ExternalPaths` (`gpui/src/platform.rs`
  L3143). There is no HTML entry. On Wayland, Zed offers text as
  `text/plain;charset=utf-8`, `UTF8_STRING` and `text/plain` only
  (`gpui_linux/.../wayland/clipboard.rs` L16-18). On X11 `text/html` appears only as a
  commented-out atom (`x11/clipboard.rs` L80). **Zed never offers HTML.** Pasting a Zed
  selection into Google Docs gives unformatted text, and the same selection pasted into GitHub
  gives no markdown unless you used Copy as Markdown.

## 3. VS Code (source read)

- **Chat panel selection copy** (`chatListWidget.ts` `handleCopy`, ~L804-886): normally the
  **browser default** (`text/plain` plus `text/html`). VS Code steps in only when the selection
  touches `<a>` or `<img>`. Then it clones the selection *with its ancestors*, so a partly
  selected heading or list item keeps its shape (`cloneVisibleSelection`). It drops hidden
  elements, collapsed sections and toolbars. It rewrites non-portable links (workspace paths)
  to `<code>label</code>` (`sanitizeChatClipboardFragment`), then
  `setData(text/plain, selection.toString())` and `setData(text/html, …)`. Three commits on
  2026-10-29 tightened "don't copy hidden or collapsed UI". This is a live pain point.
- **"Copy" on a message** (`workbench.action.chat.copyItem`, `chatCopyActions.ts` L295-344):
  with a native selection inside the widget it writes `selection.toString()`. Otherwise it
  writes `toPortableMarkdown(stringifyItem(item))`, i.e. **the response markdown as
  `text/plain`**, with links and images that only resolve locally reduced to their label in
  inline code. Also "Copy All" (whole session as markdown), "Copy Final Response", and "Copy
  Math Source" (LaTeX of a KaTeX node).
- **Code block Copy** writes `context.code` (no fences) (`chatCodeblockActions.ts` L173).
- **Notebook "Copy Cell Output"** writes the text of the first text-like output MIME. It
  strips ANSI SGR only for `application/vnd.code.notebook.error`. Stream output is copied as-is
  (`cellOutputTextHelper.ts` L84-125).
- **Markdown preview** is a webview, so it uses the browser default. Code-block copy buttons in
  the preview are recent: #322274 opened 2026-06-21, follow-up bugs in August.
  **[unverified: whether merged in stable]**
- **Claude Code's VS Code extension**: selecting and copying yields plain text and loses the
  formatting. #54670 (open) and #63363 (closed as duplicate) ask for a message copy button that
  writes "the raw Markdown source … matching claude.ai's behavior".

## 4. JetBrains

- **Markdown preview** runs in JCEF (Chromium), so selection copy is the browser default.
  **[unverified]**
- **AI Assistant chat**: code snippets have "Copy to Clipboard" next to Insert at Caret and
  Create File from Snippet (JetBrains guide). The docs don't describe a whole-response copy or
  its format. **[unverified]**

## 5. LLM chat UIs (closed source)

- **Claude.ai / Claude Desktop** (Electron, same web UI): the message copy button writes
  markdown. A user in claude-code #63363 relies on this ("raw Markdown source … matching
  claude.ai's behavior"). Selection copy is the browser default (HTML plus plain). Code block
  button copies bare code. **[unverified beyond that issue text: MIME types not inspected]**
- **ChatGPT**: the copy button is widely reported to paste *formatted* into Word and Docs
  (headings, lists, bold), which implies it writes `text/html`. A UI update on 2026-10-08
  broke bold and italic in that output and replaced external links with "google favicon"
  links (OpenAI community, 2026-10-12). Users fell back to scripts that write markdown with
  `navigator.clipboard.writeText`. **[unverified: exact MIME types]**
- **Cursor**: copying a response "captures the rendered output, not the raw markdown source".
  Users want a Copy-as-Markdown button like the code-block one (forum, 2026-02-01, no staff
  reply).

## 6. Browsers (baseline)

- **Copy of a selection** writes `text/html` (the serialized selected fragment, with enough
  ancestor context to stay valid, plus inline styles in Chromium **[unverified detail]**) and
  `text/plain`.
- **`text/plain` follows the innerText rules** (WHATWG "rendered text collection steps"; the
  spec notes they generalise to ranges and the Selection stringifier):
  - TAB between `table-cell` boxes in a row, LF between `table-row`s, so **tables become TSV**;
  - `<p>` gives 2 required line breaks, other block-level boxes give 1, `<br>` gives LF;
  - `visibility` not visible or not rendered gives nothing, so collapsed `display:none` content
    is not copied. VS Code had to work around content hidden by opacity or visibility while
    still laid out;
  - no rule emits list markers. Chromium omits bullets in plain text. Firefox's plain-text
    serializer may add them. **[unverified]**
- **Web apps cannot put `text/markdown` on the OS clipboard.** The Clipboard API's mandatory
  types are `text/plain`, `text/html` and `image/png`. Other types must be "web custom
  formats" (`"web "` prefix, stored under a UA-private mapping). `DataTransfer.setData` with an
  arbitrary type during `copy` is limited "as a security precaution". Chromium keeps such
  types in its own private pickle. **[unverified for Chromium internals]** So ChatGPT,
  Claude.ai and GitHub can only offer markdown as `text/plain`.
- **Pain points**: theme styling leaking into the HTML (Obsidian), UI chrome in the selection
  (VS Code toolbars, hidden summaries), links that only resolve locally (VS Code), and partly
  selected structures losing their shape.

## 7. Paste targets: Notion, Slack, Linear, GitHub

- **GitHub** comment boxes (`@github/paste-markdown`):
  - HTML tables and spreadsheet cells paste as markdown tables;
  - text with `<a>` links in the HTML becomes markdown links (it walks the HTML and rewrites
    the `text/plain`);
  - URLs pasted over a selection become links;
  - a private **`text/x-gfm`** type, written by GitHub's own copy and quote-reply, is pasted
    verbatim (`paste-markdown-text.ts`).

  Rich formatting (bold, headings) in pasted HTML is otherwise lost. **Markdown in `text/plain`
  is what GitHub wants.**
- **Linear**: "typed or pasted Markdown is converted into rich text automatically". It has
  explicit "Copy as Markdown" (Cmd/Ctrl-K, Cmd-Opt-C) for issues and documents (Linear docs).
- **Slack**: the default WYSIWYG composer takes formatted (HTML) paste. With "Format messages
  with markup" on, pasted formatted text arrives as plain text, and Slack's syntax (mrkdwn) is
  not CommonMark (Slack help).
- **Notion**: help pages cover markdown shortcuts while typing and markdown file import. In
  practice, pasting markdown `text/plain` makes blocks, and HTML paste is also accepted.
  **[unverified: clipboard behaviour not documented]**

## 8. Terminals

- **Warp**: block context menu offers copy command, copy output, or both; AI blocks offer
  prompt, output, both, or the entire conversation. Formats are not documented.
- **iTerm2**: Copy (plain); Option turns it into "Copy With Styles" (fonts and colours, i.e.
  rich text); "Copy with Control Sequences" re-synthesises SGR so the text pastes back into a
  terminal looking the same. Shell integration has "Select Output of Last Command", then Copy.

## 9. Linux clipboard specifics

- **Multiple targets, lazily served.** A Wayland `wl_data_source` advertises a list of MIME
  types. The source writes data to an fd only when a client asks for a type. X11 selections
  work the same way (target conversion on request). So offering an extra format costs nothing
  until someone pastes it, and expensive formats (HTML, markdown generation) can be built on
  demand. The catch: content is gone when the owner exits unless a clipboard manager took a
  copy.
- **PRIMARY** (`wp_primary_selection_v1`, X11 PRIMARY): set on selection, pasted with the
  middle button. Zed writes rendered plain text to it on mouse-up. Terminals and most toolkits
  serve only text there.
- **Text target names**: Zed offers `text/plain;charset=utf-8`, `UTF8_STRING` and
  `text/plain`, and accepts the first two. Offer all three for compatibility with X11-bridged
  (XWayland) apps.
- **`text/markdown` exists (RFC 7763), but on Linux no app *offers* it on the clipboard as far
  as I found.** LibreOffice's Qt backend registers `SotClipboardFormatId::MARKDOWN` =
  `text/markdown` (`sot/source/base/exchange.cxx` L200). But `QtTransferable` *synthesises* a
  `text/markdown` flavour whenever `text/plain` is present, and feeds it the plain text
  (`vcl/qt5/QtTransferable.cxx` ~L120-155, commit `a3eb702383`), so "Paste Special → Markdown"
  works with nothing upstream offering it. LibreOffice therefore *accepts* `text/markdown` (on
  the Qt VCL backend). `text/x-markdown` turned up only in syntax-mode tables, never as a
  clipboard type. **[unverified: GTK apps, KDE apps, Typora]**

---

## Comparison

| App / surface | Plain Copy of a selection | MIME types written | Copy as Markdown | Message copy button | Code block button |
|---|---|---|---|---|---|
| Zed markdown preview / agent panel | rendered text | `text/plain` (+ PRIMARY on Linux) | yes, context menu: **raw source slice, delimiters rebalanced** | markdown of assistant chunks, thoughts and tools skipped | bare code |
| VS Code chat | rendered text; HTML fragment | browser default `text/plain` + `text/html`; sanitized when links or images are touched | no (message Copy is markdown) | markdown (`toPortableMarkdown`), links made portable | bare code |
| VS Code markdown preview | rendered | browser default | no | n/a | bare code (recent) |
| VS Code notebook output | n/a | `text/plain` | n/a | "Copy Cell Output" = raw text | n/a |
| Obsidian Live Preview (1.12+) | markdown source | `text/plain` (md) + `text/html` | n/a (source is md) | n/a | (code block copy button, **[unverified]**) |
| Obsidian Reading view | rendered | browser default (themed HTML) | plugins only | n/a | **[unverified]** |
| Claude.ai / Desktop | rendered | browser default **[unverified]** | no | markdown **[unverified MIME]** | bare code |
| ChatGPT | rendered | browser default **[unverified]** | no | rich text (HTML) + plain **[unverified]** | bare code |
| Cursor / Claude Code VS Code ext. | rendered/plain | plain | no (requested) | missing (requested) | bare code |
| Browser | rendered | `text/html` + `text/plain` (innerText rules, tables to TSV) | n/a | n/a | n/a |
| Linear | rich | **[unverified]** | Cmd-Opt-C per issue/doc | n/a | n/a |
| GitHub (copy from rendered comment) | rendered | `text/html` + `text/plain` + private `text/x-gfm` for own paste **[unverified on copy side]** | quote-reply keeps md | n/a | bare code |
| Warp | terminal text | **[unverified]** | n/a | block: command / output / both | n/a |
| iTerm2 | plain | `text/plain`; Option gives styled (RTF) | n/a | n/a | n/a |

---

## Implications for Wheelhouse

The current proposal, item by item:

1. **Copy writes `text/plain` + `text/html` generated from the tree.** Supported. This is
   the browser baseline, VS Code chat and Obsidian 1.12. It beats Zed, whose plain-only copy
   loses all structure in Docs and Notion. Two conditions from the evidence:
   - Emit **semantic, unstyled HTML** (`h1`, `ul`, `table`, `pre`, `a`), with no theme
     colours, fonts or backgrounds. Obsidian's themed HTML is the most-cited failure.
   - **Copy what is visible, and keep partly selected structure.** Like VS Code, wrap a
     partial list item, heading or table cell in its ancestors in the HTML, and leave out UI
     chrome (buttons, badges, timestamps).

   Build both formats lazily on request (Wayland/X11 serve on demand). Offer
   `text/plain;charset=utf-8`, `UTF8_STRING` and `text/plain`.
2. **Separate "Copy as Markdown", generated from the tree.** Keep the command, and change the
   source of truth where one exists. Zed, the closest precedent and also a native app, **slices
   the raw source and rebalances inline delimiters**. That keeps the author's syntax (reference
   links, `*` vs `_`, setext headings, HTML blocks, footnotes) that a tree round-trip loses.
   Suggested rule:
   - a markdown file, or agent message text (itself model-written markdown): slice the source
     span, then repair the delimiters at the edges, as Zed does;
   - generated Blocks (tool calls and results, logs, embedded tables, transcript scaffolding):
     generate from the tree.

   Mixed selections join the two per Block. This needs pulldown-cmark offset ranges on every
   Block, and the proposal already keeps raw source.
3. **Offer a `text/markdown` target?** The evidence says it would be close to useless. No app
   found offers it. Web apps can't. LibreOffice, the one consumer, treats any `text/plain` as
   markdown. Markdown-aware destinations (GitHub, Linear, Notion, Slack markup) all read
   **markdown from `text/plain`**. Adding `text/markdown` beside `text/plain` in Copy as
   Markdown is harmless and cheap if served lazily. Don't design around it.
4. **Tables become TSV in plain text.** Supported: this is exactly the innerText rule, and it
   pastes into spreadsheets. Keep `<table>` in the HTML so GitHub's paste-markdown turns it
   into a markdown table.
5. **Code without fences in plain text.** Supported for code-block buttons (universal) and for
   selections (browser `<pre>` behaviour). In Copy as Markdown, fences must be kept or re-added
   when the selection spans a whole code block.
6. **Collapsed content not copied.** Supported. Browsers drop hidden content, VS Code spent
   three commits making collapsed sections drop out, and Zed's message copy skips thoughts.
   Use one rule throughout: copy what is visible. A per-Block "copy" on a collapsed tool result
   should still copy that Block's full content, since the button names the Block.
7. **Per-Block copy buttons.** Code: bare code as `text/plain` (universal). Message: **markdown
   as `text/plain`** (Zed, VS Code, Claude.ai; repeatedly requested where missing), with
   reasoning and tool calls left out by default, as in Zed. Rewrite links that only resolve
   locally (file paths, `wheelhouse:` URIs) to their label, as VS Code's `toPortableMarkdown`
   does. Open question: should the message button also offer `text/html`? ChatGPT does,
   reportedly, so Docs gets formatting. GitHub's paste handler then rewrites the HTML links
   into the plain text, which could double-convert text that is already markdown.
   **[unverified risk]** Start with markdown-only, like Zed and VS Code.
8. **Additions the evidence suggests:**
   - **PRIMARY**: write rendered plain text on selection end, as Zed does.
   - **Whole-transcript export**: "Copy All" / "Open Thread as Markdown" (VS Code, Zed, Warp's
     "entire conversation").
   - **Log Blocks**: plain copy strips ANSI. Consider an iTerm2-style "Copy with control
     sequences" later. A tool-call Block should offer separate copy of the command and of the
     output, like Warp's command/output/both.

## Uncertainties

- Closed apps (Obsidian, ChatGPT, Claude.ai, JetBrains, Notion, Linear, Warp): exact MIME
  types were not inspected. A `wl-paste --list-types` check after copying from each would
  settle it.
- Chromium vs Firefox list-marker and inline-style behaviour in copied selections: not read
  from source.
- Did not survey GTK and KDE apps for `text/markdown` acceptance beyond LibreOffice.
