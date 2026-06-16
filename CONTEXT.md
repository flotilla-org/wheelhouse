# UIShell

UIShell is a native application shell for composing interactive workspaces from reusable controls, tabbed panels, and embedded views.

## Language

**Panel**:
A tabbed container that hosts one or more **Views**.
_Avoid_: Sidebar, workspace, region

**View**:
A renderable content surface hosted by a **Panel** or future layout host.
_Avoid_: Widget, pane, control

**Active Panel**:
The **Panel** that is the current keyboard-input target within a **Workspace** — input reaches its **Selected View**. Exactly one per panel tree. Orthogonal to which **View** any panel shows; "active" is about input, not presentation.
_Avoid_: Focused tab, selected panel, current pane

**Selected View**:
The single **View** a **Panel** currently shows (its front tab). Every **Panel** has exactly one, whether or not it is the **Active Panel**. Being selected is presentation, not focus.
_Avoid_: Focused view, active tab, current tab

**Region**:
A layout host that may contain a **Panel**, a single **View**, a **Control Surface**, or nested layout.
_Avoid_: Panel, tab, content pane

**Frame-Bearing Region**:
A **Region** whose boundary participates in shell chrome frame resolution. Frame-bearing regions can contribute visible borders, shared seams, outer-edge policy, and selection-handle openings, but they do not draw their own borders directly.
_Avoid_: Bordered box, outlined panel, drawable rect

**Terminal View**:
A **View** that renders an interactive terminal session.
_Avoid_: Terminal panel, terminal workspace

**Terminal Provider**:
A **Content Provider** that resolves a terminal target into terminal input, output, and state.
_Avoid_: Terminal view, Ghostty integration

**Cleat Terminal Provider**:
A **Terminal Provider** implemented through cleat APIs or sessions.
_Avoid_: Bespoke PTY backend, direct Ghostty dependency

**Terminal Provider ABI**:
A C-callable interface used by UIShell to create, drive, and render terminal sessions.
_Avoid_: Cleat CLI, daemon protocol

**Terminal Snapshot**:
A coherent pull-based view of terminal cells, cursor state, and damage visible to UIShell.
_Avoid_: Callback event, tty transcript

**Terminal Input Event**:
A structured keyboard, mouse, focus, paste, or resize event sent from UIShell to a **Terminal Provider**.
_Avoid_: Shell command, raw byte stream

**Terminal Render Feed**:
A provider-supplied terminal update stream shaped for graphical rendering rather than only tty byte output.
_Avoid_: TTY stream, terminal process

**Terminal Cell Feed**:
A **Terminal Render Feed** made from grid cells, attributes, cursor state, scrollback, and terminal image state.
_Avoid_: Texture feed, foreign renderer

**Terminal Glyph Rendering**:
The conversion of terminal cells, graphemes, style attributes, and cursor state into drawn glyphs, including fallback fonts, emoji, box drawing, Powerline/private-use symbols, wide cells, grapheme clusters, and terminal-specific sprite-like graphics.
_Avoid_: Font stuff, terminal provider

**Terminal Glyph Renderer**:
The subsystem that performs **Terminal Glyph Rendering**. It may be UIShell-owned, provider-assisted, or backed by a separate Ghostty-derived wrapper.
_Avoid_: Cleat provider, VT engine

**Terminal Sprite**:
A grid-fitted terminal glyph or decoration better rendered procedurally or from an atlas than through ordinary font fallback, such as box drawing, block elements, braille, Powerline separators, rounded corners, diagonals, underlines, and cursor shapes.
_Avoid_: UI icon, image placement, emoji

**Terminal Sprite Atlas**:
A renderer-owned atlas of rasterized **Terminal Sprites** keyed by codepoint, cell metrics, style, and presentation, drawn as textured quads by the **Terminal Glyph Renderer**.
_Avoid_: Kitty image atlas, font fallback cache, Cleat texture

**Shell Chrome**:
The shell-owned visual furniture around view content — title bar, tab strips, panel dividers, status bar, scrollbars. Its visibility, style, and density are configurable properties, not fixed features.
_Avoid_: Theme (the visual parameter set), layout, view content

**Selection Handle**:
A shell chrome element that represents the currently selected content through a selection relationship or binding. It may visually attach to the region that content occupies when the geometry supports it, but it is not a general mechanism for merging arbitrary regions; examples include the selected tab for a **Selected View** and the **Workspace Inventory Entry** for the **Visible Workspace**.
_Avoid_: Notch, selected item, active tab

**Frame Integration**:
A shell chrome effect where a **Selection Handle** and the frame-bearing region selected by its binding share or elide frame edges so they read as one connected shape. Frame integration requires compatible geometry; a binding can exist without it. When themes differ across the binding, the selected handle normally reads as part of the selected region; unselected handles remain theme/preference-defined.
_Avoid_: Notch, merge, border hack

**Control Surface**:
A non-tabbed interactive surface that summarizes, selects, or creates **Workspaces**.
_Avoid_: Sidebar, panel, tab

**Control Region**:
The **Region** in a **Controlled Split** that contains the **Control Surface** owning workspace selection. It may contain additional controls or views, but its selection relationship targets the sibling **Workspace Region**.
_Avoid_: Sidebar, control side, left pane

**Workspace**:
A remembered working area that can reconnect controls, panels, and embedded views to live local or remote resources.
_Avoid_: Tab, project, session

**Runtime Instance**:
A live attachment between a **View** or **Control Surface** and an external or in-process resource.
_Avoid_: Workspace, config, layout

**Target Reference**:
A durable reference that a provider can resolve into content or layout for a **Workspace** or **View**.
_Avoid_: Runtime instance, process handle, provider state

**Instance Namespace**:
The identity scope for resource IDs and provider state owned by one **Runtime Instance**.
_Avoid_: Global namespace, workspace namespace

**Content Provider**:
An external or in-process service that resolves a **Target Reference** into content, layout, or live resources.
_Avoid_: Workspace, runtime instance

**Suggested Layout**:
A provider-supplied baseline layout for a target.
_Avoid_: Workspace, user layout

**Workspace Overlay**:
The user's saved changes to a **Workspace** relative to a **Suggested Layout**.
_Avoid_: Suggested layout, runtime instance

**Latent Workspace**:
A provider-known workspace candidate that has not yet been materialized as a saved **Workspace**.
_Avoid_: Workspace, runtime instance

**Materialized Workspace**:
A **Workspace** with live **Runtime Instances** attached.
_Avoid_: Visible workspace, selected workspace

**Visible Workspace**:
The **Workspace** holding a **Controlled Split**'s **Workspace Mount** and content input focus. Other children may be on screen as **Workspace Previews** without being visible in this sense.
_Avoid_: Materialized workspace, saved workspace, workspace preview

**Workspace Preview**:
A presentation of a **Workspace Inventory** entry other than the **Visible Workspace**: a **Materialized Workspace** shown through a **View Surface** (rebuilt at whatever frequency the budget allows, or retained from its last build), or a **Latent Workspace** shown as **Entry Content**. A preview's interior receives no input; interacting with a preview selects or zooms it.
_Avoid_: Thumbnail, screenshot, separate build mode

**Mounted Workspace**:
A **Workspace** currently attached to a region for UI and rendering.
_Avoid_: Materialized workspace, saved workspace

**Workspace Mount**:
The attachment between a **Workspace** and the **Region** where it builds UI and renders.
_Avoid_: Runtime instance, saved workspace

**Workspace Region**:
The **Region** in a **Controlled Split** where the **Visible Workspace** is mounted. It may be frame-bearing at the split boundary and may contain ordinary workspace layout inside it.
_Avoid_: Main pane, content side, non-control side

**Workspace Selection Binding**:
An association where a **Control Surface** selects which **Workspace** is mounted in a **Workspace Region**.
_Avoid_: Sidebar split, tab selection, layout link

**Workspace Address**:
The path through nested **Controlled Splits** to a **Workspace**.
_Avoid_: Workspace name, global ID

**Workspace ID**:
A stable identifier for a **Workspace** within its owning **Controlled Split**.
_Avoid_: Workspace address, layout position, display name

**Workspace Focus**:
The focused panel, view, or control remembered by a materialized **Workspace**.
_Avoid_: Window focus, global focus

**Command Context**:
The mounted workspace, focused surface, and runtime references used when executing a command.
_Avoid_: Command registry, workspace state

**Input Route**:
The resolved destination for an input event before default focus navigation, view handling, or global command handling consumes it.
_Avoid_: Focus, command binding, raw event

**Input Owner**:
The active view, control, or shell layer that has claimed a class of input for the current interaction.
_Avoid_: Focused widget, selected panel

**Input Ownership**:
The policy that decides which **Input Owner** receives keyboard, text, mouse, scroll, paste, or shortcut input.
_Avoid_: Terminal key hack, focus traversal

**Workspace Extraction**:
An explicit operation that turns part of a **Workspace** into a separate **Workspace**.
_Avoid_: Tab move, panel split

**Workspace Adoption**:
An explicit operation that brings a separate **Workspace** into another **Workspace**.
_Avoid_: Tab move, panel split

**Workspace Template**:
A reusable starting point for creating a **Workspace**.
_Avoid_: Suggested layout, latent workspace

**Workspace Inventory**:
The collection of saved workspaces, latent workspaces, and templates shown by a **Control Surface**.
_Avoid_: Tab list, panel list

**Workspace Inventory Entry**:
An item presented by a **Control Surface** that represents a **Workspace**, **Latent Workspace**, or **Workspace Template**. Activating an entry may make a **Workspace** visible directly, or first turn a latent/template candidate into a **Workspace**.
_Avoid_: Tab, selected item, sidebar item

**Materialized Workspace Inventory**:
The first workspace inventory containing only materialized workspaces and workspace creation.
_Avoid_: Provider inventory, template gallery

**Controlled Split**:
A two-sided layout relationship with a **Control Region**, a **Workspace Region**, and a **Workspace Selection Binding** between them. It is the current concrete arrangement for workspace selection, not the only possible future arrangement.
_Avoid_: Sidebar split, panel split

**Entry Metadata**:
The key/value facts attached to a **Workspace Inventory** entry (e.g. repo, branch, project, agent status), supplied by enrichers and orchestrators, that drive grouping and presentation.
_Avoid_: Config, layout, tags (the theme-matching mechanism)

**Grouping Projection**:
The prioritised rules that project **Entry Metadata** into the hierarchy a **Control Surface** displays. Shared across complementary frontends (text and GUI multiplexers) so they group the same things the same way; the serialization and delivery mechanism are not part of the concept.
_Avoid_: Render template, sidebar config

**Entry Content**:
What a projected inventory node resolves to for display — text, badges, images, previews. Each frontend chooses its own best presentation of the same content.
_Avoid_: Render template (a per-frontend mechanism), theme

**View Surface**:
A texture-backed rendering of a view's content that the shell can place, transform, and post-process — source-agnostic: produced by an in-process render pass or imported from an external process. Views draw direct by default; a surface exists only when an effect, preview, zoom, or external source demands one, and every surface-based presentation degrades back to direct drawing.
_Avoid_: Texture (the raw resource), framebuffer, transport names (e.g. jackstay)

**Theme**:
The declarative visual parameter set the shell applies to what it draws — colors today, extending to metrics, density, assets, and post-effects on **View Surfaces**. Attached per **Workspace** ("mood per workspace") with a user-level default.
_Avoid_: Skin, metaphor view, layout

**Metaphor View**:
A **View** that presents the shared model — sessions, **Entry Metadata**, the **Grouping Projection** — through an alternative representation (map, RTS, dashboard, aquarium). Supplied by a **Content Provider**, typically an external process (e.g. Godot or a small custom engine), and able to embed live **View Surfaces** such as terminals inside itself.
_Avoid_: Theme, skin, easter egg

**Chrome Host**:
A region that carries placeable shell controls rather than workspace content — the title bar today; the **Control Surface** (sidebar) and a status bar later. Distinct from a **Region**, which hosts content (**Panels**, **Views**). A host lays out an ordered sequence of **Niches**.
_Avoid_: Toolbar, bar, region

**Niche**:
A named, anchored slot within a **Chrome Host**. **Platform-reserved** niches (mac window decorations, owner-drawn window controls) are sized and presence-controlled by the window manager; the shell flows around them. **Shell-owned** niches hold **Chrome Elements**. A niche is an anchored flex container, so it can hold fixed-size controls and a growable element side by side.
_Avoid_: Slot, zone, region, toolbar

**Chrome Element**:
A placeable shell control — sidebar-collapse, new-workspace, overview toggle, the main menu, the workspace **Tab Strip**. Each declares a **Placement Chain** and whether it may fully hide. Not bound to one host.
_Avoid_: Button, widget, toolbar item

**Placement Chain**:
A **Chrome Element**'s ordered list of acceptable placements — niches that may span hosts — ending in a terminal option (hidden, or an overflow menu). The first entry is its default. **Placement Resolution** walks the chain when space is tight.
_Avoid_: Anchor, slot list, fallback list (informal)

**Placement Resolution**:
The per-host, measured process that decides where **Chrome Elements** actually land: reserve platform niches, sum the *actual* widths of assigned elements, and while they overflow, move the lowest-priority element to the next link in its **Placement Chain** — replacing hardcoded width breakpoints. A user/config placement override is the element's default placement (declared in code), so resolution is the same default-in-code / override-in-config model as code-declared settings. Physical space wins over a user override: if an element cannot fit even where pinned, resolution still relocates it (rather than clipping). Hosts resolve in a fixed order (title bar, then sidebar), so an element overflowing from one host joins the next host's pool before it resolves — which also guarantees termination.
_Avoid_: Breakpoint, responsive grid, docking (the content-layout system)

**Tab Strip**:
The docking system's row of tabs for a **Panel**. As a **Chrome Element** it can be placed in the title-bar **Chrome Host**: the topmost docking row is then promoted to *be* the chrome row (tab baseline on the title-bar border, browser-style), and the title bar's resolved end-zone widths are handed to the docking layout as **edge insets** on the top-tabbed strips that touch a window edge. The single-panel case is inset on both ends; a split top row insets only the edge-touching strips.
_Avoid_: Title bar tabs (the outcome, not the element), tab bar widget

## Relationships

- A **Panel** hosts one or more **Views**.
- A **Terminal View** is a kind of **View**.
- A **Terminal Provider** supplies the live terminal state used by a **Terminal View**.
- A **Cleat Terminal Provider** is the intended first provider for local **Terminal Views**.
- A **Cleat Terminal Provider** exposes a **Terminal Provider ABI** to UIShell.
- A **Terminal Provider** may expose a **Terminal Render Feed** when graphical terminal state is available.
- A **Terminal Cell Feed** is the initial feed shape for **Terminal Views**.
- A **Terminal View** uses **Terminal Glyph Rendering** when drawing a **Terminal Cell Feed**.
- A **Terminal Render Feed** may eventually reduce or replace parts of UIShell-owned **Terminal Glyph Rendering** when a provider exposes a richer rendering shape.
- A **Terminal Glyph Renderer** is not necessarily part of a **Terminal Provider**.
- A **Terminal Glyph Renderer** may use a **Terminal Sprite Atlas** to draw **Terminal Sprites**.
- A **Terminal Snapshot** is pulled through the **Terminal Provider ABI**.
- UIShell sends **Terminal Input Events** through the **Terminal Provider ABI**.
- A **Workspace** contains one or more **Regions**.
- A **Region** may host a **Panel**, a single **View**, a **Control Surface**, or nested layout.
- A **Panel** body may be a **Frame-Bearing Region**; its **Tab Strip** is **Shell Chrome** that can contribute **Selection Handles** and tab-strip background, but does not itself own the content frame.
- A **Workspace Region** may be frame-bearing at the boundary where it participates in a **Controlled Split**; its internal regions may also be frame-bearing within the workspace.
- A **Controlled Split** contains exactly one **Control Surface** region and exactly one selected **Workspace** region.
- A **Controlled Split** owns the **Workspaces** selected by its **Control Surface**.
- A selected **Selection Handle** may create **Frame Integration** with the region selected by its binding; unselected handles may have their own boundaries or themed previews, but they do not open the selected region's frame.
- A **Workspace ID** identifies a **Workspace** within its owning **Controlled Split**.
- A **Workspace Address** locates a **Workspace** by composing stable IDs through nested **Controlled Splits**.
- A **Control Surface** may select or create many **Workspaces** over time.
- A **Workspace** may have many **Runtime Instances** attached while it is active.
- A **Workspace** may have a **Target Reference** when its layout or content is resolved by a **Content Provider**.
- A **View** may have a **Target Reference** when only that view needs provider-specific content.
- A **Runtime Instance** owns one **Instance Namespace**.
- A **Runtime Instance** may move between **Workspaces** only through an explicit shell operation.
- A **Content Provider** may resolve a **Target Reference** into a **Suggested Layout**, a **View**, or a **Runtime Instance**.
- A **Workspace Overlay** modifies or filters a **Suggested Layout** without replacing the provider's baseline.
- A **Latent Workspace** may appear in a **Control Surface** before it is opened or saved as a **Workspace**.
- A **Materialized Workspace** may exist without being a **Visible Workspace**.
- A **Controlled Split** has exactly one **Visible Workspace** at a time; it may present any number of its other children as **Workspace Previews**.
- A **Controlled Split** may build any **Materialized** child, not only the **Visible Workspace**; a previewed child's content runs normally, differing only in presentation, input routing, and build frequency.
- A **Visible Workspace** is a **Mounted Workspace** in the controlled workspace region.
- The first implementation has one **Workspace Mount** per window.
- Nested **Controlled Splits** may introduce additional **Workspace Mounts** later.
- A **Materialized Workspace** remembers its own **Workspace Focus**.
- **Workspace Focus** is the **Active Panel**'s **Selected View** — the destination of keyboard input. A **Selected View** alone is presentation; only the **Active Panel**'s **Selected View** is focused.
- Commands execute against a **Command Context**.
- An **Input Route** may be influenced by **Workspace Focus**, but it is not the same thing as focus.
- An **Input Owner** may claim specific input classes without owning every event.
- **Input Ownership** resolves before default focus navigation consumes navigational keys.
- **Workspace Extraction** and **Workspace Adoption** are explicit workspace operations, not ordinary tab moves.
- A **Workspace Template** may create a new **Workspace**.
- A **Workspace Inventory** may include **Workspaces**, **Latent Workspaces**, and **Workspace Templates**.
- A **Materialized Workspace Inventory** includes only **Materialized Workspaces** and a way to create a new **Workspace**.
- A **Chrome Host** contains an ordered sequence of **Niches**; a **Region** contains content. They are different layout kinds.
- A **Niche** is platform-reserved (window-manager owned) or shell-owned; only shell-owned niches hold **Chrome Elements**.
- A **Chrome Element** declares a **Placement Chain** spanning niches across hosts; **Placement Resolution** decides its actual niche by measured overflow.
- A **Placement Chain** ends in a terminal option (hidden, or overflow menu); an element that may never fully hide (e.g. the main menu) terminates in a representation, not in hidden.
- A **Chrome Element**'s default placement is declared in code and overridden in config — the same provenance model as code-declared settings.
- The **Tab Strip** is a **Chrome Element**; placing it in the title-bar host promotes the topmost docking row to the chrome row and feeds the title bar's end-zone widths to the docking layout as edge insets.

## Example Dialogue

> **Dev:** "Should the file browser be a **Panel**?"
> **Domain expert:** "No. If it selects which **Workspace** is visible, it is a **Control Surface**. A **Panel** is specifically the tabbed thing that hosts **Views**."

## Flagged Ambiguities

- "Sidebar" describes an initial visual placement, but not the concept. Resolved: use **Control Surface** unless the physical side placement is the point.
- "Layout" was used for both the selected working area and the tree structure inside it. Resolved: use **Workspace** for the selected working area.
- "Workspace" was used for both saved shape and live processes. Resolved: a **Workspace** is the remembered working area; live resource attachments are **Runtime Instances**.
- "Target" may apply at different levels. Resolved: use workspace-level **Target References** for provider-resolved workspaces and view-level **Target References** for self-contained local content.
- "Provider layout" should not imply ownership of the user's arrangement. Resolved: provider output is a **Suggested Layout**; user changes are a **Workspace Overlay**.
- A selector is not a tab strip. Resolved: a **Control Surface** presents a **Workspace Inventory**.
- Workspace existence, materialization, visibility, and mounting are distinct. Resolved: a **Materialized Workspace** has live runtime instances; a **Mounted Workspace** has somewhere to build UI and render.
- The first cut should keep mounting simple without forbidding nesting. Resolved: start with one **Workspace Mount** per window, while allowing nested **Controlled Splits** later.
- Focus should survive workspace switching. Resolved: **Workspace Focus** belongs to each materialized **Workspace**; commands route through the mounted workspace.
- Commands should not be owned by individual workspaces. Resolved: command registration remains global or modular; command execution uses a **Command Context**.
- Input capture should not be terminal-specific. Resolved: use **Input Ownership** so terminal views, text editors, property editors, web views, and shell focus navigation can share one routing policy.
- Tab is both text/editor input and focus traversal depending on context. Resolved: route it through **Input Ownership** before default focus navigation.
- First-cut workspace selection should not depend on providers. Resolved: start with a **Materialized Workspace Inventory**.
- Terminal panes should not become layout primitives. Resolved: use **Terminal View** for terminal content and **Runtime Instance** for live PTY or VT state.
- Ghostty is not a shell-level concept. Resolved: terminal backends sit behind a **Terminal Provider** boundary.
- "Font stuff" mixes several problems. Resolved: use **Terminal Glyph Rendering** for drawing cells as glyphs, and **Terminal Render Feed** for provider-supplied render data.
- Terminal glyph fidelity is not the same problem as terminal session brokering. Resolved: keep **Terminal Glyph Renderer** separate from **Terminal Provider** unless a capability explicitly combines them.
- Terminal glyph fidelity is not ordinary UI text rendering. Resolved: keep the ordinary text rendering API for UI/code text, and use a terminal-shaped **Terminal Glyph Renderer** for **Terminal Cell Feeds**.
- UIShell should not grow a second production PTY backend. Resolved: use a **Cleat Terminal Provider** for local terminal sessions and evolve cleat's API as needed.
- The first UIShell terminal integration should not be shaped by cleat's CLI or daemon protocol. Resolved: define a **Terminal Provider ABI** first; IPC can implement the same model later.
- Cleat is closer to a terminal session broker than a simple terminal client. Resolved: UIShell may consume a **Terminal Render Feed**, not only a tty byte stream.
- The first terminal feed should not require renderer interop. Resolved: start with a **Terminal Cell Feed**, including Kitty-style image state where needed.
- Terminal providers may poll PTYs or transports on background threads. Resolved: UIShell consumes pull-based **Terminal Snapshots** with dirty or damage reporting.
- Terminal input is not keyboard-only. Resolved: send structured **Terminal Input Events** for keyboard, mouse, focus, paste, and resize, with raw byte writes available separately.
- Terminal protocol encoding belongs to the terminal side. Resolved: UIShell sends structured input; the **Terminal Provider** encodes mode-dependent key and mouse protocols.
- Embedded content resource IDs are not shared globally. Resolved: IDs such as terminal image IDs belong to an **Instance Namespace**.
- Moving live content should not be confused with reopening saved layout. Resolved: **Runtime Instances** may move explicitly; **Views** and **Regions** are saved shape.
- Terminal and web content should remain usable as tab children. Resolved: make them **Views**, not a separate pane container category.
- Workspace identity should not depend on layout position. Resolved: use stable **Workspace IDs** and treat **Workspace Addresses** as locators.
- Cross-workspace movement should not be a first-cut tab feature. Resolved: defer arbitrary tab moves between workspaces; preserve room for **Workspace Extraction** and **Workspace Adoption**.
- Sidebar organisation should not depend on an orchestrator being present. Resolved: the **Grouping Projection** evaluates locally over **Entry Metadata**; orchestrators (e.g. flotilla) are enrichers/providers, not a dependency. Whether an orchestrator eventually owns the projection is an open experiment.
- "Text vs GUI" frontends are not different data models. Resolved: complementary frontends share **Entry Metadata** and the **Grouping Projection**; presentation of **Entry Content** is per-frontend.
- "Very customizable look" mixes parameters with representations. Resolved: a **Theme** is declarative shell parameters; radical representations are **Metaphor Views** — content from providers, not theme files.
- Offscreen panel rendering and external content import are not two pipelines. Resolved: both produce **View Surfaces**; compositing, effects, previews, and zoom operate on surfaces without knowing the source.
- "Optional status bar" and similar knobs are not one-off toggles. Resolved: they are **Shell Chrome** properties carried by the existing config system.
- "Visible" and "on screen" diverge once previews exist. Resolved: the **Visible Workspace** is the child holding the **Workspace Mount** and content input focus; other children on screen are **Workspace Previews**.
- A preview is not a degraded build mode. Resolved: a previewed **Materialized Workspace** builds as an ordinary container child of the **Controlled Split** — everything in it runs; only build frequency, render scale, and input routing differ. A workspace carousel or zoom-out is this container presenting many children; showing exactly one is the degenerate case.
- "What goes in the title bar" is not a layout question. Resolved: it is a **Placement Resolution** problem over **Chrome Hosts** (title bar, sidebar, status bar) made of **Niches**; **Chrome Elements** declare a **Placement Chain** and resolve by measured overflow, defaulting in code and overriding in config. No element is hardcoded to the title bar.
- Tabs in the title bar is not a special tab mode. Resolved: it is the **Tab Strip** element placed in the title-bar host; the topmost docking row becomes the chrome row and the title bar's end-zone widths become **edge insets** to the docking layout. Tab overflow within the inset span stays clipped for now (a later overflow dropdown is separable). See ADR 0006.
- The project selector is not assumed useful in uishell. Resolved: a uishell user overwhelmingly has one context bringing all threads together; the project/owner selector is not a fixed title-bar fixture — it is a **Chrome Element** like any other, present only if placed.
