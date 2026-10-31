# Research: grafting foreign accessibility subtrees under a View

Status: research notes, 31 October 2026. Nothing here is decided. The decisions
are being made on the map "Wheelhouse's UI is machine-readable and
locale-ready" (#343); this answers its research ticket #556.

A **Foreign Subtree** is an Accessibility Tree subtree that something other
than the toolkit's own boxes supplies, grafted under a View's node, whose
source answers actions on its nodes (`CONTEXT.md:362-364` on branch
`docs/adr-0014-intents`). The four sources are Jackstay plugin views, Luchs web
views, porthole-exported windows and apps, and Cleat's region recognisers.

Sources were read at these revisions: AccessKit `main` f3026222 (releases of
25 October 2026), WebKit `main` daa70f50, CEF `master`, Jackstay
`origin/main` ce9355d, Luchs 1448d76, porthole e7c64ce, Cleat 32c255e, and
Wheelhouse `origin/main` 1f52720. Line numbers refer to those revisions.

## Summary

- **AccessKit can graft today, in-process.** Multiple-tree support shipped in
  `accesskit` 0.23.0 (15 January 2026). A graft node names a subtree by
  `TreeId`; each subtree has its own `NodeId` space; action requests carry
  `target_tree`. The application still submits every subtree's updates to its
  own adapter itself; AccessKit has no cross-process transport, and its AT-SPI
  adapter can neither embed an out-of-process platform tree nor be embedded.
- **So every foreign source must reach Wheelhouse as data** (an AccessKit-shaped
  tree update) and Wheelhouse relays it into its adapter as a subtree. Platform
  embedding (AT-SPI sockets, macOS remote tokens) exists in WebKit but is not
  something AccessKit can host.
- **Jackstay affordances can't carry a tree as they stand.** They are bounded
  JSON snapshots with a closed set of domains, ADR 0002 rules out a generic
  widget tree, and nothing about accessibility or ARIA exists in Jackstay yet.
  An app-kind track could carry one without changing the affordances contract.
- **WebKit hands the embedder no tree as data.** On Linux the page is an AT-SPI
  subtree in the web process that the UI process embeds through a socket; on
  macOS it is grafted through private AppKit remote-element SPI. CEF's
  windowless mode, by contrast, does deliver the tree as data.
- **porthole reads a little AX today and exports none of it.** It walks AX for
  a content rect on macOS and has no tree export on any platform.
- **Cleat's screen tree is a tree with roles and cell bounds**, served over
  CLI and HTTP, but its node IDs last only one response, so it needs stable
  identities before it maps onto accessibility nodes.

## a) AccessKit: composing trees

### What exists

- **Releases.** `accesskit` 0.25.2, `accesskit_unix` 0.24.1,
  `accesskit_macos` 0.27.2, `accesskit_windows` 0.35.2 and `accesskit_consumer`
  0.39.2 were all released on 25 October 2026 (GitHub releases of
  AccessKit/accesskit). `accesskit-c` is at 0.23.1 (26 September 2026). The
  earlier research cited 0.25.1 (`docs/research/automation-accessibility-i18n.md`).
- **Multiple trees.** "Multiple tree support (#655)" is a breaking change in
  0.23.0, 2026-01-15 (`accesskit/CHANGELOG.md`; PR
  [AccessKit#655](https://github.com/AccessKit/accesskit/pull/655), merged
  2026-01-10). The PR says it "adds support for nested trees to the schema and
  all existing adapters", and that the design aims to let "completely
  independant providers … expose their subtree(s) without a central
  registrar".
- **Node ID namespaces.** "Each tree (root or subtree) has its own independent
  ID space … the combination of `NodeId` and `TreeId` uniquely identifies a
  node" (`accesskit/src/lib.rs:705-715`). `TreeId` wraps a UUID; `TreeId::ROOT`
  is the nil UUID, and subtrees should use a random v4 UUID
  (`lib.rs:737-750`). `NodeId` stays a `u64`.
- **Graft nodes.** A node with `tree_id` set "acts as a graft point, and its
  child is the root of the specified subtree. A graft node must be created
  before its subtree is pushed. Removing a graft node or clearing this property
  removes its subtree, unless a new graft node is provided in the same update"
  (`lib.rs:2542-2549`). Graft nodes cannot have ordinary children (PR #655;
  enforced in `accesskit_consumer/src/tree.rs:203-217`).
- **Tree updates per tree.** `TreeUpdate` gained `tree_id`. The first update
  for a subtree must include `tree` data, and the graft must already exist
  (`lib.rs:3241-3250`). `focus` in a subtree update says which subtree node has
  focus when the graft node is focused (`lib.rs:3252-3261`). Effective focus is
  found by following grafts down from the root (`tree.rs:98-100`).
- **Geometry composes across the graft.** The subtree root's parent is the
  graft node (`tree.rs:471-473`), and `transform()` multiplies up the parent
  chain (`accesskit_consumer/src/node.rs:297-307`). A graft node's transform
  can therefore map a source's own coordinates (a plugin's canvas, a page's
  viewport) into the window.
- **Action routing.** `ActionRequest` has `target_tree` and `target_node`
  (`lib.rs:3332-3337`). The adapter calls the application's single
  `ActionHandler::do_action` (`lib.rs:3366-3377`); the application reads
  `target_tree` and forwards the request to whoever owns that subtree.
- **Serialisable.** The `serde` and `schemars` features cover `TreeUpdate`,
  `Node` and `ActionRequest` (`accesskit/Cargo.toml` `[features]`;
  `#[cfg_attr(feature = "serde", …)]` on each type), and 0.25.1 added
  `tree_id` to the JSON schema
  ([AccessKit#811](https://github.com/AccessKit/accesskit/issues/811)). A
  foreign source can therefore send AccessKit-shaped updates as data, encoded
  as CBOR through serde.
- **Identifiers for tests.** `author_id` exists "for automated testing
  purposes" and must be unique among siblings; `html_id` (0.25.0) takes
  precedence where both map to the same platform property
  (`lib.rs:2131-2144`). Either could carry an **Automation ID**.
- **Malformed updates panic.** The consumer panics on a subtree pushed without
  a graft, or updated before it exists (`tree.rs:134-145`), and on a second
  graft for the same subtree (`tree.rs:188-198`). Updates relayed from another
  process must be validated before they reach the adapter.

### What is missing or only discussed

- **No cross-process transport.** Subtrees are pushed by the application that
  owns the adapter. AccessKit's push model copies Chromium's renderer-to-browser
  IPC, "except that both components run in the same process and communicate
  through normal function calls rather than IPC" (`accesskit/README.md:29`),
  so a relay between processes is the application's job. The planned architecture text says subtrees let "different
  actors … produce accessibility trees without coordinating … and let the main
  application submit them to the adapters"
  ([AccessKit#750](https://github.com/AccessKit/accesskit/issues/750), open,
  documentation only).
- **No platform embedding.** An AccessKit maintainer, 25 August 2026: "The
  AccessKit AT-SPI adapter currently doesn't support neither embedding
  out-of-process trees nor being embedded"
  ([AccessKit#778](https://github.com/AccessKit/accesskit/issues/778#issuecomment-5415040228)).
  The same issue (open) asks for the AT-SPI `embeds` relation on grafts, which
  Orca uses to find a browser's active document. No code has landed for it.
- **No cross-tree relations.** PR #655: "it is not possible … to indicate that
  a node is labelled by another node that is in a different tree".
- **Roadmap.** There is no roadmap item for out-of-process subtrees; the open
  items are #750 (documenting subtrees) and #778 (`embeds`).

## b) Jackstay: the v3 handshake's affordances

### What exists

- **Affordances are a core track kind** (`affordances`, direction `both`) in
  the v3 track handshake. When admitted, the track gets its own socketpair
  (`jackstay/docs/design/affordances.md:11-22`;
  `docs/design/track-handshake.md:29-33`). A host offers at most one
  (`track-handshake.md:62`).
- **The content is a closed set of domains:** media, navigation, cursor,
  scroll, window and presentation (`crates/jackstay/src/affordances.rs:97-104`),
  each a full snapshot plus a few verbs with capability flags
  (`docs/adr/0002-affordances-channel.md:8-14, 38-43`). Unknown domains are
  ignored, and vendor domains use an `x-` prefix (`affordances.rs:695-703`).
- **The encoding is still JSON.** Each message is `{"version":1,"domain",
  "domain_version":1,"kind":"snapshot"|"verb",…}` (`affordances.rs:163-164,
  569`) in length-prefixed JSON framing (`src/framing.rs:1, 8-9`). One message
  is at most 128 KiB, and the queue at most 512 KiB (`affordances.rs:391`).
  Snapshots are whole, with no deltas (ADR 0002 line 40).
- **A generic tree is ruled out.** ADR 0002 rejects "Generic widgets: transfers
  layout and toolkit semantics, growing a remote UI protocol" and extends only
  "by domain, versioned enum values and vendor prefixes" (lines 32-34). The
  design defers menus to a "dbusmenu-shaped" second slice and says "no generic
  widget tree is introduced here" (`affordances.md:57-59`).
- **No accessibility or ARIA.** Neither Jackstay's code nor its docs mention
  ARIA. A `TrackType::AccessibilityEvents` variant exists in
  `crates/jackstay/src/model.rs:95-102`, but nothing uses it except `Video`
  (`model.rs:81-85`). The "ARIA-flavoured direction" exists only in the
  resolution of Wheelhouse #92.
- **Wheelhouse's side.** The Jackstay view polls the host handle, publishes
  presentation and sends verbs (`native/src/jackstay/wheelhouse_jackstay.c:523,
  553, 557`), and shows window and navigation state (`docs/design/jackstay-view.md:101-115`).

### Planned, not built

- **CBOR.** Jackstay #104 (closed 13 October 2026) decided that the whole setup
  plane, affordances included, moves to CBOR and "replaces … affordances JSON
  v1"; flotilla ADR 0063 names it the first adopter
  (`flotilla/docs/adr/0063-cbor-is-the-stack-binary-format.md:55`). The code is
  still JSON (above). Under ADR 0063, a protocol that sets no limit gets 1 MiB
  per message, nesting depth 64 and 65,536 elements per array or map (line 41).
- **Rings.** Jackstay #103 (open) would move input and affordances onto shared
  memory rings; "How affordances' variable-size JSON messages fit in a ring" is
  an open point.
- **App-defined tracks.** Kinds are open: an app kind is a reverse-domain name
  with an opaque descriptor (`track-handshake.md:48-50`). Admitting one "only
  records the agreement: Jackstay opens no channel for it, so its transport is
  the host's and consumer's own" (lines 62-65). Jackstay #105 (open) would make
  app kinds byte queues with the same transport as core tracks.

### Could affordances carry a tree update and action requests?

- **As ADR 0002 stands, no.** A tree is the generic widget protocol it rejects.
  A whole-tree snapshot would also hit the 128 KiB message cap for a large
  page, and the channel has no deltas.
- **An `x-` vendor domain could carry one** without a Jackstay change, since
  unknown domains are skipped. It would still be JSON, capped, and against the
  ADR's spirit.
- **An app-kind track**, such as `org.flotilla.accessibility`, fits the
  handshake as written. Its transport would be Wheelhouse's and the
  producer's own until #105 lands. Its messages could be AccessKit
  `TreeUpdate`s (subtree-local IDs, incremental) and `ActionRequest`s, as
  CBOR under ADR 0063.
- **For plugin views that run the Wheelhouse toolkit**, the producer side
  would come for free: the toolkit's own annotation walk (#92) builds the
  subtree in the plugin process. Plugin views themselves are only planned;
  "nothing in any of these repos covers plugins"
  (`docs/research/plugin-views-and-build.md:35`).

## c) WebKit and Luchs: the page's tree

### Luchs today

- **macOS only, WKWebView in a helper.** The renderer is a `WKWebView` helper
  process; "Linux has no real renderer yet" (`luchs/README.md:3-6, 48-50`).
  The page publishes window, navigation, cursor and scroll affordances, and
  nothing accessible (`README.md:17-20`).
- **The helper's window is on screen but transparent** (`alphaValue = 0`, level
  `.screenSaver`), because WebKit throttles hidden pages
  (`native/macos/LuchsWebviewCapture.swift:467-484`). Its page is probably
  visible to assistive tools as an invisible window of another app. This was
  not checked.
- **CEF is an optional spike on `main`**, macOS arm64, behind a Cargo feature
  (`native/cef/README.md:1-15`). It renders windowless
  (`native/cef/engine.mm:461-466`). Luchs has no accessibility code in either
  renderer.

### WebKit on Linux (WebKitGTK and WPE)

- **The page's tree lives in the web process, on the AT-SPI bus.** WebKit
  implements AT-SPI directly (`Source/WebCore/accessibility/atspi/`). The root
  registers the `Accessible`, `Socket` and `Component` interfaces
  (`AccessibilityRootAtspi.cpp:154-164`), and its `Embedded` method records the
  parent's bus name and path (`AccessibilityRootAtspi.cpp:135-142, 179-184`).
- **The UI process embeds it by plug ID.** `WebPageProxy::bindAccessibilityTree`
  hands the plug ID to `wpe_view_accessible_bind` on the WPE Platform API, or
  to `atk_socket_embed` with ATK (`Source/WebKit/UIProcess/wpe/WebPageProxyWPE.cpp:78-99`).
- **WPE's embedder hook is a socket, not data.** `WPEViewAccessible` has one
  method, `bind(accessible, plug_id)` (`WPEPlatform/wpe/WPEViewAccessible.h:41-50`);
  `wpe_view_get_accessible` returns it (`WPEView.h:78, 173`). The ATK
  implementation is an `AtkSocket` that calls `atk_socket_embed`
  (`wpe/atk/WPEViewAccessibleAtk.cpp:40, 140-143`).
- **So on Linux an embedder gets a socket to graft, not a tree.** Reading the
  tree as data means being an AT-SPI client of the web process over D-Bus.
  Actions would go to the same objects (AT-SPI `Action` and related
  interfaces, which WebKit implements: `AccessibilityObjectActionAtspi.cpp`).
- **Release.** The newest WPE tag is `wpewebkit-2.54.2` (WebKit tags).

### WebKit on macOS

- **No public API.** `WKWebView.h` has no accessibility tree API.
- **Grafting is private SPI.** WebKit grafts the web process's tree through
  AppKit's `NSAccessibilityRemoteUIElement`: `remoteTokenForLocalUIElement:`,
  `registerRemoteUIProcessIdentifier:` and `initWithRemoteToken:`
  (`Source/WebCore/PAL/pal/spi/cocoa/NSAccessibilitySPI.h:41-51`).
  `WKWebViewPrivate.h:692-698` exposes `_remoteAccessibilityChildToken` and
  related calls.
- **In Luchs's shape**, the helper owns the WKWebView, so a graft into
  Wheelhouse's window would need that private SPI across a second process
  boundary. Alternatively the helper reads its own view's tree through public
  NSAccessibility (or `AXUIElement` against its own pid) and serialises it.

### CEF (the Luchs branch)

- **Windowless mode delivers the tree as data.** For windowless browsers,
  accessibility runs in "TreeOnly mode … the full tree is computed, and events
  are passed to CefAccessibiltyHandler, but platform accessibility objects are
  not created. The client may implement platform accessibility objects using
  CefAccessibiltyHandler callbacks" (`include/cef_browser.h:983-988`). It is
  turned on with `SetAccessibilityState(STATE_ENABLED)` (`cef_browser.h:965-991`).
- **Callbacks:** `OnAccessibilityTreeChange(CefValue)` and
  `OnAccessibilityLocationChange(CefValue)`
  (`include/cef_accessibility_handler.h`), from
  `CefRenderHandler::GetAccessibilityHandler` (`cef_render_handler.h:61-66`).
- **No action call.** CEF has no call that performs an accessibility action on
  a node. Actions would go through input, or through DevTools
  (`ExecuteDevToolsMethod`, `cef_browser.h:649`).
- **The fit.** AccessKit's schema "is based largely on Chromium's
  cross-platform accessibility abstraction" (`accesskit/README.md:15`), so
  translating CEF's tree into AccessKit nodes should be mostly mechanical.
  This was not prototyped.

## d) porthole: an exported window's native tree

### What exists

- **macOS reads AX for geometry only.** The adapter binds
  `AXUIElementCreateApplication`, `AXUIElementCopyAttributeValue` and
  `AXUIElementPerformAction` (`crates/porthole-adapter-macos/src/ax.rs:23-25`).
  It uses them for placement, focus and close, and for the **Content rect**:
  `AXContents`, else the largest `AXChildren` child
  (`src/content_rect.rs:42-51`; `CONTEXT.md:188-199`).
- **Windows and Linux read nothing.** The Windows content rect is
  `unsupported("UIAutomation content rect")`
  (`crates/porthole-adapter-windows/src/native.rs:750`). Linux's AT-SPI source
  is only named as the future source (`CONTEXT.md:197-199`).
- **Exports carry media and optional input, nothing else.** An export request
  has execution, chroma, bitrate and `input` (`crates/porthole-protocol/src/publications.rs:57-67`);
  a capture session's Jackstay offer is one video track, with "no input or
  affordances track" (`crates/portholed/src/capture_registry.rs:1221-1223`).
- **The wire vocabulary is already platform-neutral.** porthole ADR 0002 names
  "AT-SPI on Linux, UIAutomation on Windows, ARIA in webview shims" as future
  sources of the same `role` field (`docs/adr/0002-wire-vocabulary-platform-neutral.md:5-10`).
- **No issue tracks exporting a tree** (porthole issue search for
  "accessibility" and "tree").

### What it could provide

- **On the same machine**, the platform APIs read any app's tree:
  `AXUIElement` with the Accessibility permission (which porthole already
  holds), AT-SPI as a bus client, and UIA as a client.
- **For a remote consumer** (the bridge crosses hosts), the tree must travel as
  data: porthole walks it, serialises it with platform-neutral roles, and maps
  incoming actions to `AXUIElementPerformAction`, AT-SPI `DoAction` or UIA
  patterns. None of this exists. Bounds would need the same window-to-image
  mapping the video uses.
- **Identity is the hard part.** AX elements have no stable IDs, so porthole
  would have to mint `NodeId`s and keep them across walks. This is an
  inference from the AX API's shape, not something porthole has tried.

## e) Cleat: recognisers and program status

### What exists

- **The screen tree** (`7b1fe40`, Cleat #316) is a pure snapshot analysis:
  `screen > region… > row slice > span`, with cursor overlays
  (`crates/terminal-screen/README.md:24-39, 52-58`). Regions come from graph
  segmentation with roles `frame`, `enclosure`, `zone`, `group`, `block` and
  `gap`, and carry provenance and confidence (`README.md:26-30, 41-47`).
- **It is served.** `cleat inspect ID --screen` and
  `GET /sessions/{id}/screen?tree=1` return the same JSON schema; nodes carry
  `id`, `parent`, `children`, `element`, `roles`, `attributes`, `bounds`,
  `text` and `confidence` (`docs/screen-queries.md:3-24, 43-49`).
- **IDs last one response.** "IDs are local to this response, not persistent
  across generations" (`screen-queries.md:13-15`); the crate has "no …
  identity across frames" (`README.md:3-4`).
- **Bounds are cells**, zero-based and half-open (`screen-queries.md:19-20`).
- **Recognisers** (ADR 0008, built in Cleat #349) are TOML rules over the tree
  that place role nodes such as `composer`, `status` and `dialog`, with
  `value`, `state` and `kind` (`docs/adr/0008-recognizers.md:5, 9`;
  `docs/recognizers.md:113-118`). A Rust `Recognizer` trait covers the rest
  (`crates/cleat/src/recognizer.rs:313-345`). Selection is explicit per
  session (ADR 0008 rule 3). Roles stream through
  `SessionService::connect_recognizer`, or `/connect` with
  `{"recognizer_roles":true}` (protocol 15) (`recognizers.md:153-157`).
- **OSC 7501 program status** is kept by the session as one record per id,
  with state (`idle`, `working`, `done`, `blocked`, `error`) and source
  (`osc7501`, `title` or `recognizer`) (`docs/adr/0007-program-status-through-attach.md:10`).
  The text is untrusted and must be stripped of invisible formatting wherever
  it leaves the terminal (ADR 0007 consequences, line 21).
- **Guarded actions exist.** `cleat send --if SELECTOR` refuses when the
  selector doesn't match (ADR 0008 rule 6), for example
  `composer[value='']`.
- **Wheelhouse uses none of it today.** There are no references to the screen
  tree, recognisers or program status under `crates/` or `native/`.

### Does it map onto accessibility nodes?

- **Roles map onto AccessKit roles.** `Terminal`, `Document`, `Region`,
  `Group`, `Status`, `Dialog`, `AlertDialog`, `TextInput` and
  `MultilineTextInput` all exist (`accesskit/src/lib.rs:62-270`). The likely
  shape: a `composer` becomes a text input whose value is the draft, a
  `dialog` becomes a dialog, `status` becomes a status node with the OSC 7501
  state, and the regions become groups with their text.
- **Bounds convert.** Cells times the terminal's cell metrics, which
  Wheelhouse knows, give rects relative to the terminal box. A graft node's
  transform can carry that mapping.
- **Identity doesn't.** AccessKit needs stable `NodeId`s for focus and
  incremental updates, but graph-region IDs change every generation. Recogniser
  roles are the stable candidates: one node per role name is a key that lasts.
  Regions would need matching across frames, which Cleat calls temporal
  production and hasn't built (`README.md:30`).
- **Actions.** Focus and activate can go to the terminal. Set-value on a
  composer would become a guarded `cleat send --if`. Any other action has no
  equivalent.
- **The opt-in rule applies.** Terminal screen text is exposed only when the
  setting is on, which #92 also turns on when a screen reader is detected.
  Recogniser roles and program status carry screen text too.

## Implications

These are options, not decisions.

1. **One graft mechanism in the toolkit.**
   - Every foreign source arrives as AccessKit `TreeUpdate`s with its own
     `TreeId`. The toolkit crate (`wheelhouse-accessibility`, #92) owns grafts
     by Automation ID, validates updates before the adapter sees them, and
     routes `ActionRequest`s by `target_tree` back to the source.
   - The alternative is a Wheelhouse node schema translated into AccessKit
     nodes. That adds a layer, but keeps AccessKit's types out of every
     producer.
2. **The transport for Jackstay sources.**
   - **(a)** An app-kind track (`org.flotilla.accessibility`) carrying CBOR
     `TreeUpdate`s and `ActionRequest`s. This needs no ADR change, but needs
     its own channel until Jackstay #105 lands.
   - **(b)** A new affordances domain. This means revisiting Jackstay ADR 0002,
     raising the 128 KiB cap and adding deltas.
   - **(c)** A core `accessibility` track kind in Jackstay, perhaps reviving
     `TrackType::AccessibilityEvents`.
   - Whichever is chosen, Luchs, a toolkit plugin and porthole would all carry
     the same messages.
3. **Web views, per renderer.**
   - **CEF windowless** gives the tree as data, so Luchs translates it.
   - **WKWebView** gives data only if the helper walks its own view through
     public NSAccessibility; the alternative is private remote-token SPI.
   - **Linux WebKit**, when Luchs grows a renderer, gives a socket. Luchs would
     read the web process as an AT-SPI client and send data, because
     AccessKit's adapter can't embed sockets.
   - Or accept the helper window's own exposure as an interim, if it turns out
     to work at all (unchecked).
4. **porthole** would need new work in each adapter: walk, mint IDs, map roles
   to the neutral vocabulary, execute actions. It could ship the tree as an
   export track beside video. Cross-host is the norm, so data is the only
   option.
5. **Cleat.** Two shapes:
   - **Roles first:** a subtree of the recogniser's role nodes and program
     status only, with stable keys. This is cheap and fits AccessKit now.
   - **The full region tree:** this needs cross-frame identity in Cleat first
     (temporal production), or a Wheelhouse-side matcher.
   - Either way, the source is Cleat's library or HTTP stream, not Jackstay,
     and it sits behind the terminal-text setting.
6. **Upstream asks worth considering:**
   - out-of-process subtree helpers in AccessKit (none are planned);
   - the AT-SPI `embeds` relation on grafts (AccessKit#778), so Orca finds a
     web view's document;
   - a Jackstay decision on where accessibility travels.
7. **Automation IDs.** A graft node gets the View's Automation ID. Foreign
   nodes could carry source-scoped IDs in `author_id`
   (`<view-id>/<source-key>`). Whether the Placement Tree shows foreign nodes
   at all is open; the #347 schema has no foreign kinds.
