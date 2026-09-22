# Pane Focus & Frame Spacing — Design Notes

Working record (2026-06-16, grilled with Robert). Not implemented yet. Captures the visual + behavioural decisions around **Active Panel** focus indication and panel frame spacing; the hard-to-reverse rendering decision (dock-resolved edge-segment borders) is split out as [ADR 0007](adr/0007-panel-borders-are-dock-resolved-edge-segments.md). Terms per [CONTEXT.md](../CONTEXT.md).

## The problem: "focus" was one blue, cast too wide

The default theme paints a blue (`0x2392eb`) selected-border on **every** Panel's front tab, plus sidebar entry buttons and previews. It reads as jarring because it conflates three orthogonal things, and does so from *two* mechanisms sharing one color:

- **`tab border`** (`= 0x2392eb`) is drawn on the **Selected View**'s tab of *every* Panel (the tab box's own `DrawBorder`, `:4190`) — even inactive panels.
- The global **`focus border`** (also `0x2392eb`) fires on any clickable, focus-active box (`:7837`) — i.e. the **Active Panel**'s subtree.

So "this is the front tab" and "this has keyboard focus" are visually identical, and the net spreads to every panel and to the sidebar/previews. The code already has the right predicates — `tab_is_selected`, `panel_tree.focused == panel`, and their conjunction (`:4061`, `:4209`) — it's the *indicators* that collapse.

### Decomposition (now in CONTEXT.md)
- **Selected View** — the front tab of a Panel. Every Panel has one. Presentation, not focus.
- **Active Panel** — the keyboard-input target (`panel_tree.focused`).
- **Workspace Focus** — the Active Panel's Selected View. The only thing keyboard input reaches.
- **Selected Workspace** (sidebar/preview) — a *fourth*, distinct concept: a selection, not focus. Owned by the andamento/hierarchy redesign.

## Indicators (subtractive)

| Concept | Cue |
|---|---|
| Selected View (all panels) | **Structural only** — the integrated tab (below). No color accent. |
| Active Panel | **Dim the *inactive* panels.** No border, no positive mark. |
| Workspace Focus (intersection) | Emergent: the front view of the one undimmed panel. |
| Selected Workspace | A calm selection accent on the sidebar entry — andamento's to redesign (may reuse the integration trick to connect the Selected Workspace to the workspace area, as the Selected View connects to its Panel). |

The global blue **`focus border` is retired** to opt-in (a `UI_BoxFlag_DisableFocusBorder` already exists); it survives only where a deliberate selection accent belongs (sidebar selected entry), softened. A reserved positive focus accent — just the focused tab handle's top edge (cmux-style) — is held for later **only if** dimming proves too weak in practice.

## Integrated tab (the Selected View cue)

Chrome-style: one continuous frame around the **active tab + panel body**, the active tab's run being the only suppressed border segment (inactive tabs keep the body's top border beside them). This is a partial-edge thing a box outline can't draw — see ADR 0007.
- Selected tab background **= panel body background** (so it reads as part of the body).
- Inactive tabs **recessed** (dimmer bg/text). Today this is inverted: `tab inactive background = 0` (transparent → looks like the panel) while `tab background = 0x333333`.
- The first-tab left inset (`:4182`, a 1px spacer) is **removed**.

## Inactive-pane dimming — get this right (no-accent depends on it)

**Built 2026-06-16 (branch `inactive-panel-dim`).** A **front scrim** over non-active panels (`:3873`; built *before* `panel_box` so, under reverse-order sibling painting, it draws *in front*; non-clickable, so clicks still focus the panel). Driven by a bounded user setting **`inactive_panel_dim`** (range 0–0.8, **default 0.55**), covering the **whole panel** (`panel_rect`: tabs + body + frame), not just the old view-area scrim.

**Flavour — fade toward the backdrop, *not* a black multiply.** A pure-black scrim at alpha `a` is `dst*(1-a)`: a multiplicative darken that *preserves* the fg/bg ratio — so inactive content gets darker but stays just as crisp/legible, which doesn't read as "recede." Blending instead toward the **window `background`** color makes fg and bg *converge* → contrast drops → the panel reads as pushed back. Chosen on sight (0.55 toward-bg beat the black version). Tradeoff: very dark terminal backgrounds *flatten* rather than darken (the target bg `0x1f1f1f` slightly lifts pure black while the bright text falls toward it) — that flattening *is* the recede. The old `inactive background = 0x16` theme value is now bypassed.

**Future — theme amount × global intensity (combine, don't just multiply).** When this migrates to Theme (thread B), a theme wants its *own* dim amount (a delicate light theme dims less than a high-contrast dark one), combined with the user's global preference. Two independent `[0,1]` values **multiplied** collapse to near-zero — wrong (Robert's point). Lean: the **theme declares the amount**, the **global setting is an intensity multiplier defaulting to 1×** (scales a theme's choice up or down), so neither shrinks the other by default. Exact combine to settle in the theme pass.

**Blur / desaturate** ("attention focus mode") remains the later upgrade on the **View Surface** composite path (needs intermediate textures); the scrim is the zero-surface baseline.

## Frame spacing (summary; mechanism in ADR 0007)

- **Border** always present, thickness ≥ 1px (setting).
- **Gap** — `0` → single shared seam; a *reasonable* value → two-border cards. Guided to 0-or-proper, never tiny.
- **Window-edge border** — its own on/off (default off, ghostty-like); small tunable window padding kept.
- **Default = flush** (gap 0, single seam, no window-edge border). The heavier card look (gap + per-panel borders + window-edge border) is fully recoverable via settings — "non-destructive" in the sense that nothing is lost, just re-defaulted.
- Covers **both** the normal split tree and the Controlled Split.

### Known edge case: tab strips meeting at a panel seam (try-and-correct)

`tab_side` is per-Panel (top `Side_Min` / bottom `Side_Max`, flipped at `shell_core.c:~3641`); the tab corner radii already encode the integration direction — rounded on the outer edge, square toward the body (`:4146`). So vertically-adjacent Panels can put a **tab strip on one or both sides of a shared seam** (tab↔tab, tab↔body, body↔body). Today's `0.15em` self-inset separates them, and the flush default **removes** that separation — a case we partly rely on.

- **Invariant to keep:** integration is **intra-panel only** — an active tab opens into *its own* body, never across an inter-panel seam, which stays bordered. This rules out the worst outcome (a tab appearing to flow into a neighbour's content).
- **Residual (cosmetic):** two tab strips meeting flush (a back-to-back double tab row) may read as cramped. Likely fix: retain a **minimum separation at tab-bearing seams** (flush fully collapses only body↔body), or a hair of gap whenever a seam touches a tab strip.
- Per Robert: **try it and correct on sight** — don't over-specify the rule before seeing it.

## Scope & sequencing

All knobs (gap, thickness, inactive-dim strength, window-edge controls) are **user settings now** — code-declared, ranged → sliders, like `overlay_scrollbars`/`animation_speed`. They migrate to **Theme** parameters in the later theme pass ([theme-design-notes](theme-design-notes.md) thread B, "beyond colors → metrics/density"); the setting/theme duality is already the model, so this is not throwaway.

**Deferred (noted, not now):** tab auto-hide / compact tab-strip layouts; attention blur/desaturate (surface-composite); the concave tab-base fillet (effects toolchain); the Selected-Workspace integration + the broader sidebar/preview selection restyle (andamento); migrating these metrics into Theme.

## Native visual follow-up, 2026-09-22

Observed during the project sidebar review; these remain to investigate:

- The shell's outer border appears pinched at the Cocoa window corners. Compare
  our radius and stroke placement with the actual native window mask, including
  different display scales and fullscreen.
- At `tab_gap = 0`, the left edge of a tab header appears almost clipped away.
  Unless chrome pushes the tab along, its left border should align with the body
  it selects. Check the selected-tab draw extent against the tab-strip clip.
- Explore concave inner corners where the selected tab joins its panel, with
  subtler treatment for inactive tabs. ADRs 0007/0008 describe the frame ownership
  and possible rendering work; settle geometry before choosing a new primitive.
- With a nonzero `panel_gap`, title-bar controls need a bottom edge wherever no
  panel abuts them. Resolve this by adjacency, including partially uncovered
  spans. Consider whether the same spacing setting should inset the sidebar.

The first project's top-stroke clipping and missing sidebar top/right edges are
addressed in the native composition branch. The sidebar frame uses the panel
border setting; it does not yet participate in panel-gap/adjacency resolution.
