# Web views: content that Wheelhouse launches a program to show

Status: design, agreed 2026-10-16. The first slices below are not built yet.

Today a View can show a Jackstay stream only if something else already
started it: the Jackstay view connects to an endpoint someone typed in. A View
has no *opening recipe* that lets Wheelhouse start a process, learn the
endpoint it publishes, and treat the result as the View for that content.

The canonical case is a web page. A URL, or a default start page, should open
as a View that Wheelhouse backs with a Luchs process, and that reports itself
as a web page: its title and URL, with navigation.

## The shape

```text
content  →  Handler  →  runtime instance  →  View (pane-level)
```

- **Content** is a View Spec's: its kind (URL, file, command, facet) plus the
  URL scheme or file extension, with `presentation` as a hint. `"web"` picks
  the web Handler. MIME types and intents can come later without changing the
  View Spec.
- **A Handler** (CONTEXT.md) is the frontend's rule for showing that content:
  which Renderer shows it and, for content another program produces, how to
  launch that program and connect to what it publishes. Each frontend has its
  own Handlers, as it has its own Renderers, so a TUI can show the same URL as
  text or as a link. The View Spec stays logical: `{url, presentation: web}`.
- **The runtime instance** is the launched process, its endpoint, and the
  state its affordances report.
- **The View** is pane-level, of the Handler's Renderer kind.

## Decisions

| | Decision |
|---|---|
| What is handled | View Spec content: kind plus scheme or extension, `presentation` as tie-breaker |
| Who decides | Each frontend, from its own Handler table |
| Live state | The spec says where a View opens; a portable `web-location` resolution `{url}`, kept with the Workspace like a terminal's Cleat session, says where it got to. It is written when navigation settles, without history. The title is a live tab label, device-local and not saved |
| Process ownership | The View owns it, like a terminal's runtime: it starts when the View first builds, keeps running while the workspace is open (shown or not), and stops when the View closes or the workspace is kept. It is supervised like the subscription connector: process group, lifeline, restart with backoff |
| Endpoint | Wheelhouse chooses the name (`wh-<view>`) and passes it in; readiness comes from the producer's stdout line, then its `window.ready` affordance |
| View kind | A `web` Renderer: the page's title on the tab, an address bar with back, forward and reload, and a Jackstay client inside. The `jackstay` view stays for raw endpoints |
| Handler table | Compiled in, beside the Renderer registry. Each entry has `matches` (kind, schemes, presentation), an argv template and a readiness rule; Luchs is the only entry. Its fields are shaped like a future manifest |
| Opening | **Web** in the "+" menu opens a bundled start page; the address bar navigates in place; an `open_url` command prompts for a URL. Terminal links and Andamento's `OPEN_URL` still go to the system browser |
| Size, scale, focus | The pane's pixel size goes out as `presentation.preferred_size`, `scale` from the display, and focus as `presentation.focused`. The producer's `window.requested_size` is ignored: the pane decides |
| Default page | A start page bundled with Wheelhouse |
| Failure | A launch or first load that fails shows the placeholder with the error, Retry and Open in browser |
| Jackstay protocol | The v3 track handshake (Jackstay #119, ABI 0.17), which replaced bootstraps v1 and v2: the source offers tracks (video, input, affordances, playback), the consumer selects them. Wheelhouse and Luchs both move to it. Katzensteg already has (its #174); Porthole has not, so its sources need v3 before Wheelhouse can show them again |

## What exists, and what each side lacks

- **Andamento** already models it: `ANDAMENTO_SLOT_URL` content with an open
  `presentation` vocabulary, and its Suggested Layout docs name "a Luchs page".
- **Wheelhouse:**
  - No Renderer takes URL content. A URL that isn't a `wheelhouse:` page
    becomes a placeholder with Open in browser. A Renderer is the first whose
    content mask matches, and `presentation` is ignored.
  - The Jackstay view uses Jackstay's v1 bootstrap, which Luchs refuses; it
    reads no affordances, and it connects only when someone presses Connect.
  - In the View Spec, a Jackstay "launcher" is the setting that names the
    endpoint (`source_endpoint`, …), not something launched.
  - The subscription connector's supervisor (`src/ingress/connector.rs`) has
    argv, env, a log, restart with backoff, a process group and a lifeline,
    but it is owned by a subscription and logs stdout.
- **Luchs** (`luchs --endpoint NAME --size WxH <url|file>`) prints its socket
  path on stdout, latches `window.ready` after the first load, reports
  `navigation {url, title, can_go_back, …}`, and takes `navigation.load`,
  `back`, `forward` and `reload`, and `presentation` resizes. It renders only on
  macOS. It has no lifeline (the supervisor's process group covers that), needs
  a page argument, and exits if the first load fails.
- **Jackstay** has the affordance vocabulary and nothing for launching: no
  endpoint handoff convention, no readiness signal to a parent, no lifeline.
  Its publication-registry note (latent sources, a "dormant-source catalog with
  per-entry activation policy") is discussion only, and leaves process
  ownership open.

## First slices

1. **The Jackstay view speaks the v3 track handshake, with affordances.** It
   shows the producer's window title and navigation URL, sends size, scale and
   focus, offers back, forward and reload when the producer advertises them,
   and connects by itself when its View asks. This bumps `JACKSTAY_REV` to
   Jackstay `main` (ABI 0.17), and checks that **Open Jackstay Source** still
   works. It is tested against Jackstay's own v3 example producers.
2. **The `web` View.** The Handler table with its Luchs entry, a View-owned
   supervisor generalised from the connector, launch and readiness, the
   `web-location` resolution, the bundled start page, and the opening commands.
   Tests on Linux use Luchs's `--helper` test renderer; a real page needs macOS.
3. **Luchs on v3**, then any other Luchs changes as they turn up, in Luchs.

## Later

- **A View that knows which plugin it uses.** `web` is specific at first. It
  may become a general plugin View, whose Handler names the producer.
- **Handler discovery:** manifests or a catalog of launchable producers, and how
  they are trusted. This is where Jackstay's latent sources and activation
  policy would meet Wheelhouse, and where #93 (Jackstay source discovery that
  keeps explicit endpoints) belongs.
- **Other back ends behind a Handler:** remote Jackstay sources, and wasm.
- **Delegation without navigation.** The `pending` file view, which already
  sniffs a file's type, could hand a file Luchs can render to it, read-only and
  without an address bar, so it looks like any other opened file. Transforms
  belong here too: Markdown or Mermaid rendered to a page. Specific Renderers for
  some of these may still come later.
- **Links:** where terminal and Andamento links open (Wheelhouse or the system
  browser) as a setting, and setting `BROWSER` in launched environments so
  programs open pages in Wheelhouse.
- **Hidden web Views** could be suspended if keeping their processes running
  costs too much.
- **A Linux renderer for Luchs** is Luchs work, outside this design.
