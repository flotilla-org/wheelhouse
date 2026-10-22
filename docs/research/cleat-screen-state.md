# Research: what Cleat exposes about screen state today

Status: research notes, 22 October 2026. Nothing here is decided.

Question: [#429](https://github.com/flotilla-org/wheelhouse/issues/429), for the
map [#421](https://github.com/flotilla-org/wheelhouse/issues/421). What do local
Cleat daemon sessions send as screen state, what does native Wheelhouse read,
and what is missing for (a) a browser behind a relay and (b) one Cleat client
per session shared by several Views
([#417](https://github.com/flotilla-org/wheelhouse/issues/417))?

**Sources.** Wheelhouse is cited at `origin/main` 99f1355. Cleat is cited at
00c072b, the revision Wheelhouse currently builds against (the
`wheelhouse-osc52-169` worktree), and written `cleat:` below with paths relative
to the Cleat repo root. Upstream Cleat `main` is 25 commits ahead (89cf40c);
where it changes the answer, that is called out as **upstream**. Nothing was
run; this is a reading of the code and Cleat's own docs.

## Short answer

- Daemon sessions send **row-level screen state, not VT bytes**: a full
  visible-grid replace on attach, then whole-row replacements for rows that
  changed. Each cell carries graphemes, resolved RGB plus the original
  palette/RGB colour, flags, width, underline, and its OSC 8 URI. Cursor,
  modes, scrollbar and the full list of Kitty image placements ride on every
  update.
- The encoding is **postcard** (Rust serde, varint, not self-describing)
  inside a 9-byte multiplexed frame, over an HTTP `Upgrade: cleat-packet/1` on
  the daemon's Unix socket (named pipe on Windows). One connection per daemon,
  one channel per attachment.
- Flow control is **one unacknowledged render per channel**. The daemon keeps
  a per-session row cache and catches each channel up from the rows that
  changed since what that channel last saw, so a slow reader gets coalesced
  state.
- **Kitty graphics** are the only images: placements (including virtual ones)
  plus image bytes sent once per channel, either as 64 KiB chunks or as a
  local file handoff. **Sixel and iTerm2 (OSC 1337) images are not decoded**;
  nothing about them reaches clients.
- Native Wheelhouse opens **one channel per Terminal View** on a shared
  per-daemon connection and keeps its own cell and image cache per View.
- For a browser, the hard gaps are the encoding (postcard tied to Rust struct
  layout), local-only image file offers, and no transport beyond the local
  socket. For #417, the daemon already fans out well, but Wheelhouse would
  need to share one channel and one cache between Views; the per-channel
  scrollback viewport and the size vote do not fit "several Views, one
  client" as they stand.

## Transport and attach protocol

- **Connection.** A client connects to the daemon socket and sends
  `POST /connect` with `Connection: Upgrade`, `Upgrade: cleat-packet/1`, an
  `x-cleat-output-context` header and a JSON body of directory selectors. The
  daemon answers `101 Switching Protocols`
  (`cleat:crates/cleat/src/provider_daemon.rs:846-889`,
  `cleat:crates/cleat/src/http_uds.rs:698`, `:768-776`).
- **Framing.** Each packet is `channel: u32 LE`, `msg_type: u8`, `len: u32 LE`,
  then a postcard payload of at most 4 MiB
  (`cleat:crates/cleat/src/packet.rs:71-72`, `:336-361`). Postcard is the serde
  binary format from `cleat:crates/cleat/Cargo.toml:24`. Field order, enum
  variant indices and varints come straight from the Rust structs; there is no
  schema file.
- **Handshake.** The daemon sends `ControlHello { version, min_supported_version }`
  and then a `DirectorySnapshot` on channel 0. The client rejects a range that
  does not contain its version (`cleat:packet.rs:75-91`,
  `cleat:provider_daemon.rs:865-888`). Pinned Cleat speaks protocol 12 only
  (`cleat:packet.rs:11-12`). **Upstream** speaks 15 and still accepts 12.
- **Channel 0** carries directory snapshot and deltas, activity snapshot and
  events, open/close channel, errors and transfer redirects
  (`cleat:packet.rs:15-25`, `:93-162`).
- **Opening a session.** The client sends `OpenChannel { channel, session_id,
  role, take, identity }` (`cleat:packet.rs:172-183`). The daemon replies with
  `RoleState` (granted role, controller, participants, exclusivity, fixed size,
  whether clipboard writes are supported) and then a full render
  (`cleat:crates/cleat/src/session.rs:4826-4945`).
- **Per-session messages** (`cleat:packet.rs:27-39`):
  - down: `RENDER` (16), `ROLE` state (21), `VIEW_STATE` (22), `IMAGE` chunk
    (24), `IMAGE_FILE` offer (25), `CLIPBOARD` (27), `CLIPBOARD_LOSS` (28);
  - up: `ACK` (17), `INPUT` (18, structured key/text/mouse/focus/paste/resize
    events), `RESIZE` (19), `VIEWPORT` (20), `ROLE` request (21),
    `SIZE_POLICY` (23), `IMAGE_FILE_RESULT` (26).
- **Reconnect.** The C provider's reader thread reconnects with backoff
  (100 ms to 2 s), reopens every channel with its last role and size, and the
  daemon answers each with a full render. A transferred session sends a
  redirect and the provider follows it to the new daemon
  (`cleat:provider_daemon.rs:1-18`, `:52-54`).
- Other daemon endpoints (`GET /sessions/{id}/snapshot` returns a full grid as
  JSON, `/screen` returns text) are one-shot reads for diagnostics and tools,
  not a live feed (`cleat:http_uds.rs:691-730`, `cleat:session.rs:3909-3940`).

## The render packet

`RenderPacket { update: TerminalRenderUpdate, links, view }`
(`cleat:packet.rs:291-302`). The update (`cleat:crates/cleat/src/provider.rs:140-154`)
holds:

- `cols`, `rows`, `geometry` (cell and content pixel sizes);
- `viewport_kind` (live normal, live alternate, normal scrollback),
  `scrollback_offset_rows` and `scrollbar` (`total_rows`, `viewport_rows`,
  `viewport_top_row`, `at_bottom`);
- `terminal_modes`: alternate screen, application cursor keys, alternate
  scroll, mouse tracking mode and report format
  (`cleat:crates/cleat/src/vt/mod.rs:235-250`);
- `render_generation`, `cursor` (col, row, visible, style, blink, wide tail),
  `dirty` (clean, partial, full);
- `ops`: a list of `FullVisibleReplace`, `RowReplace` or `ScrollCopy`
  (`cleat:provider.rs:61-78`);
- `image_resources` and `image_placements` (below).

Each row (`cleat:provider.rs:80-93`) has its cells plus `wrap`,
`wrap_continuation`, `semantic_prompt` (OSC 133), `has_kitty_virtual_placeholder`
and summary flags. Each cell (`cleat:provider.rs:95-122`) has its graphemes as
`u32` code points and a style: flags, width (narrow, wide, spacer head or tail),
`resolved_fg`/`resolved_bg`, the original `fg_color`/`bg_color` (none, palette
index or RGB), underline style and colour, protected, semantic content tag,
`hyperlink_id` and the **owned OSC 8 URI bytes**.

`links` and `view` matter for scrollback: live packets leave `links` empty and
`view.status = Live`; history frames carry `History`, `Stale` or `Unavailable`
and a notice (`cleat:provider.rs:176-196`).

### Snapshots and deltas

- **What the VT produces.** Ghostty's render state yields exact row damage.
  `render_update` emits one `FullVisibleReplace` for full dirt and one
  `RowReplace` per dirty row otherwise (`cleat:crates/cleat/src/vt/ghostty.rs:710-795`).
  It never emits `ScrollCopy`; the C header says scrolling falls back to full
  replacement until Ghostty exposes scroll damage
  (`cleat:crates/cleat/include/cleat_provider.h:664-671`). Scrolled output
  therefore costs every row.
- **Synchronized output (mode 2026)** is honoured at publication: while a
  batch is open, consumers keep the last finished frame, with a 1 s deadline
  (`cleat:crates/cleat/src/host/presentation.rs:1-26`).
- **Daemon cache.** Each hosted session keeps a `PacketRenderCache`: the
  latest frame-wide state, every row, and the generation at which each row
  last changed. A resize or screen switch resets it
  (`cleat:session.rs:4157-4258`).
- **Per-channel catch-up.** For each channel, `since(last_source_generation)`
  returns `RowReplace` ops for just the rows changed since that channel's last
  delivery, or a `FullVisibleReplace` if it is behind a reset
  (`cleat:session.rs:4217-4252`). The daemon renumbers `render_generation` per
  channel (`last_sent + 1`), so generations are channel-local
  (`cleat:session.rs:5059-5076`).
- **Backpressure.** A channel is due only when it has no render in flight; the
  client's `ACK` of that generation releases it
  (`cleat:session.rs:4596-4605`, `:5009-5023`). The C provider acks when the
  caller consumes the update (`cleat:crates/cleat/src/provider_ffi.rs:2150-2175`).
  A client whose queued output exceeds 4 MiB is marked dead and dropped
  (`cleat:session.rs:48`, `:4316-4326`).
- **Client side.** The C provider keeps only the one pending packet per
  channel and the last frame-wide state; it holds **no grid**. Daemon sessions
  return false from `cleat_session_snapshot`; the caller must keep its own grid
  from render updates (`cleat:provider_daemon.rs:129-174`,
  `cleat:provider_ffi.rs:2113-2115`, `cleat:cleat_provider.h:652-663`).

## Image placements

- **Kitty graphics only.** Ghostty's Kitty image store is enabled with a
  320 MB limit and file, temp-file and shared-memory media
  (`cleat:vt/ghostty.rs:41`, `:77-80`). Every render update carries the
  **complete current list** of image resources and placements, not a delta
  (`cleat:vt/ghostty.rs:797-839`).
- **Resource**: `image_id`, `generation`, pixel size, `format` (RGB, RGBA, PNG,
  gray, gray-alpha), `compression` (none or zlib), `data_len`
  (`cleat:provider.rs:156-165`, `cleat:cleat_provider.h:132-138`).
- **Placement**: image id and generation, placement id, z, viewport col/row
  (may be negative when partly scrolled off), grid cols/rows, pixel size,
  source rect, pixel offsets, and a `VIRTUAL` flag for Unicode-placeholder
  placements (`cleat:provider.rs:205-226`). Placeholder cells (U+10EEEE) also
  stay in the grid, and rows flag `has_kitty_virtual_placeholder`.
- **Bytes.** The actor captures each referenced generation when it renders
  (`cleat:crates/cleat/src/host/actor.rs:1510-1538`,
  `cleat:crates/cleat/src/image_delivery.rs:37-86`). Per channel, the daemon
  sends only generations that channel does not already hold, all before the
  render that references them. Each image is first offered as a local file
  path (`IMAGE_FILE`); if the client declines, it is sent as ordered 64 KiB
  `IMAGE` chunks. A view's images are capped at 320 MiB
  (`cleat:image_delivery.rs:17-18`, `:88-176`, `:178-240`).
- **Sixel and iTerm2.** Not decoded. The only sixel code is a Windows ConPTY
  test that checks the DCS reaches the engine
  (`cleat:crates/cleat/src/session_runtime.rs:1176-1306`); Ghostty has no
  sixel support and nothing in Cleat handles OSC 1337. Such images are
  dropped. Cleat's README lists sixel only as something the outer host
  terminal may answer queries for (`cleat:README.md:206`).
- **Upstream** adds the original Kitty virtual-placement declarations
  (`cleat_virtual_placement`, protocol 13, `MSG_SESSION_RENDER_VIRTUAL`) as a
  complete set per render, opt-in, so a client can re-emit them
  (upstream commit ee9058e, `packet.rs` and `cleat_provider.h`).

## Scrollback, selection, cursor, hyperlinks, clipboard, status

- **Scrollback is per attachment, server-side.** A `VIEWPORT` command, or a
  wheel event that the app is not tracking, moves that channel's own history
  anchor. The daemon then sends that channel captured history frames instead
  of live ones, at most one per 34 ms and two per servicing pass. Typing
  returns that channel to live. Browsing does not move other attachments'
  views (`cleat:session.rs:4633-4660`, `:4737-4762`, `:5024-5080`;
  `cleat:docs/multiplayer-attachments.md:89-112`). Scrollback rows are never
  streamed; a client sees only its current viewport.
- **Selection** is client-local; Cleat sends nothing for it
  (`cleat:docs/multiplayer-attachments.md:24-28`).
- **Cursor**: one application cursor in every update; history views hide it.
- **Hyperlinks**: each cell carries its URI bytes (capped at 1 MiB per render,
  `cleat:vt/ghostty.rs:28`, `:731-735`); history frames also list links.
- **OSC 52**: write-only clipboard events go to controller channels only,
  with an epoch and sequence and a separate drop counter; reads are not
  supported (`cleat:session.rs:4957-4980`,
  `cleat:crates/cleat/src/clipboard.rs:11-21`, `cleat:cleat_provider.h:713-731`).
- **OSC 7501 program status**: not in pinned Cleat. **Upstream** stores it
  per session and adds opt-in `MSG_SESSION_PROGRAM_STATUS` (30) and activity
  events (protocol 14; commits 6efad06, ad194dd).
- **Roles and size**: drivers vote, and the PTY takes the minimum columns and
  rows of all drivers; watchers do not vote
  (`cleat:crates/cleat/src/attachment_control.rs:106-110`). The earliest driver
  supplies the cell pixel size.

## What native Wheelhouse reads

- **Backend choice per View.** A Terminal View uses the daemon backend when it
  has a `session` parameter or `daemon:1`; otherwise it runs in-process
  (`src/uishell/uishell_views.c:4144-4164`).
- **One connection per daemon, one channel per View.** Providers are cached
  by daemon name (`uishell_views.c:3711-3749`). Each View then calls
  `cleat_session_attach` or `cleat_session_create`, which opens its own
  channel, with no role set (so it requests control)
  (`uishell_views.c:4181-4210`). Each View keeps its own `cell_cache` and
  `image_cache` (`uishell_views.c:49`, `:103-104`).
- **Per frame**: `cleat_session_poll`; if dirty (and a background preview is
  admitted), `cleat_session_render_update`, apply to the cell cache, apply to
  the image cache, `cleat_session_mark_observed`, release
  (`uishell_views.c:4509-4531`). Consuming is what acks, so a View that skips
  a frame makes the daemon coalesce for it.
- **Cell cache** applies all three op kinds, copying URIs and wrap facts
  (`src/uishell/uishell_terminal_glyph.c:5444-5574`). Selection is checked
  against old and new cells and cleared if they differ
  (`uishell_views.c:182-234`).
- **Image cache** looks up each new `(image_id, generation)` with
  `cleat_session_with_image_resource_data`, decodes RGB, RGBA or PNG (gray
  deferred, compressed rejected), uploads a texture, and copies all placements
  each update. It does not copy the placement `flags`; placeholder cells are
  simply not drawn as glyphs (`uishell_terminal_glyph.c:64-74`, `:5624-5686`,
  `:5700-5852`). Unplaced images are evicted LRU past a quota, since Cleat
  exposes only placement-referenced resources.
- **Input and scrolling** go up as structured events and viewport commands
  (`uishell_views.c:251-276`); size and geometry via `cleat_session_resize` and
  `cleat_session_update_geometry` (`uishell_views.c:4236`, `:4360`).
- **Remote sessions** do not use this path. A vessel terminal runs a
  `flotilla attach --host … <session>` command inside its own Cleat session
  (`docs/design/subject-workspaces.md:66-70`). That attach renders packets back
  into VT bytes for its terminal (`cleat:session.rs:349-360`), which the local
  Ghostty parses again. Remote screen state reaches Wheelhouse as bytes.

## Gaps for a browser behind a relay

1. **Encoding.** Postcard has no schema; a browser decoder must mirror the Rust
   struct layouts and enum indices exactly, and would break on any field
   change. Either the relay re-encodes into a documented format, or Cleat
   publishes a schema (or the core's wasm build decodes it).
2. **Size of a frame.** Cells repeat full style records (two colour forms,
   resolved RGB, URI bytes) with no style table. Scrolled output sends every
   row, since `ScrollCopy` is never emitted. Fine on a local socket; worth
   measuring over a network.
3. **Images.** The file offer hands over a local path; a relay must decline it
   (the simple Rust client already does, `cleat:packet.rs:465-487`) and take
   chunks. The browser must decode PNG and raw RGB(A), honour zlib, and cope
   with the 320 MiB per-view budget. Placement lists are full replacements
   each frame, which is easy to mirror. Virtual placements need the placeholder
   cells to be drawn as image slices; Wheelhouse does not do this from the
   `flags` today.
4. **Transport.** The protocol is reachable only on the local socket after an
   HTTP upgrade; there is no TCP, TLS or authentication of its own. The relay
   has to terminate the browser connection and hold the Cleat connection.
5. **Remote sessions.** Today remote state comes back as VT bytes through
   `flotilla attach`. A browser drawing screen state needs a packet stream
   from the remote daemon (#104).
6. **Version.** Pinned Cleat speaks exactly protocol 12; upstream accepts
   12 to 15. A relay should negotiate rather than assume.
7. **No scrollback stream.** History is one server-side viewport per channel,
   rate-limited to 34 ms per capture. A browser that wants smooth local
   scrolling would need history rows sent, not only rendered viewports.

## Gaps for #417 (one client, several Views)

- **The daemon side already fans out**: one cache per session, independent
  per-channel catch-up and credit. Several channels from one Wheelhouse work
  today; #417 is about not doing that.
- **Wheelhouse side.** Each View owns a `cleat_session`, a cell cache and an
  image cache, and consuming an update both acks and moves on, so two Views
  cannot share one channel without a shared owner that consumes once and lets
  both Views draw from one cache. The C ABI allows one live render update per
  session (`cleat_provider.h:652-655`), which fits a single owner.
- **Sizing.** With one channel per View, both are drivers and the PTY takes
  their minimum size (`attachment_control.rs:106-110`), not "the View last
  interacted with". With one shared channel, Wheelhouse sends one size, which
  matches #417's rule, and letterboxes the other View locally.
- **Cell pixel size** comes from one driver; Views with different fonts would
  need image placements scaled locally.
- **Scrollback.** The history viewport belongs to the channel, so one shared
  channel means one scroll position for every View of the session. Independent
  scrolling per View would need either a channel per scrolling View (a watcher
  opened only while browsing) or history rows sent to the client.
- **Selection** is already client-local and per View, so it is unaffected.
- **Clipboard events** are queued per channel and acquired once
  (`cleat_provider.h:713-717`); with one channel, Wheelhouse must route them
  once, not per View. `src/uishell/uishell_terminal_clipboard.c` already
  registers clipboard handling per session handle.
