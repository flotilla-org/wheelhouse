# Andamento sidebar prototype

Status: interactive design experiment, September 2026. Changes are not yet a
settled UI contract. Run `./scripts/run-daily-driver.sh` from this checkout.

The question is whether a compact tree with independently scrolling sections
can become the primary workspace selector. Evaluate it with the live catalog,
including long labels, repeated vessel names, and more rows than fit on screen.

## Decisions

The tree is intended to subsume the original workspace selector. Before removing
that selector, it must cover local workspace creation, closing, and workspaces
that remain open after their catalog entry disappears.

Tree placement does not dictate terminal ownership. A governor shown at a
project root need not move into that project's workspace. Later view options
can change grouping without rearranging running terminals.

Activation resolves through a binding. Today it opens or focuses a workspace;
later it can reveal a particular view within one. Several entity entries can
resolve to the same workspace. Keep provider identities separate from labels
and from their placement in the tree.

A project overview initially runs its published `flotilla view` recipe. Vessel
workspaces run their attach recipes. Neither requires a separate workspace tab
bar. Panel tabs continue to select views within a workspace.

## Experiment

Use one Andamento model and snapshot for all sections. Each section owns its
height, collapse state and scroll position. Headers stay outside the scrollable
body. Projects receives the remaining height; secondary sections start with
bounded bodies and can be resized. Empty sections without controls are hidden.

Use one line per normal entry. Disclosure changes expansion; the name opens or
focuses its workspace. Show open/selected state separately from producer status.
Keep full labels and descriptive state available in tooltips. Attention needs
project context because its entries are detached from the tree hierarchy.

The experiment keeps section sizing in memory. It does not introduce a public
layout configuration, separate dockable views, or automatic governor placement.

## Review in the running app

- Can Projects and Attention scroll without moving each other or their headers?
- Does resizing or collapsing one section leave the other readable?
- Can we distinguish several vessels named `work` and discover project overview
  activation without permanent Open/Focus buttons?
- Does opening from either placement focus the same workspace?
- Do live updates leave the viewport stable enough to read?

Record the result after trying the UI. The next composition experiment, if
needed, is one project workspace containing its overview and governor. Decide
its focus behaviour from that example before generalising templates or toggles.

## First implementation check

The native build and existing host diagnostics pass. In an isolated daily-driver
window with real Flotilla metadata, wheel input moved Attention while Projects
and both headers stayed put. Dragging the separator enlarged Attention and made
Projects overflow; wheel input then moved Projects independently. Opening the
Flotilla project launched its scoped TUI and marked the project entry selected.

The layout still needs user evaluation. Glyph choices, section height defaults,
and how much context to show in Attention are provisional. Scroll offsets are
pixel positions, so inserting rows above the viewport can still shift the item
being read. Stable item anchoring and revealing keyboard-focused offscreen rows
need a separate check before treating this as the finished selector.

## Collapse review

The first text-font disclosure was too narrow: at 11 px, the closed `>` measured
10.5 px with only 10 px available after padding. It disappeared through text
truncation while its hit target remained. Use the shell's centred icon-font
expander, with no text padding or truncation, for both section and tree controls.
A native geometry regression covers this size. Terminal-type and selection
indicators must not use arrow shapes.

Section counts now count direct entries, independent of descendant expansion.
Previously Projects counted visible rows, making collapse appear to remove
projects from the catalog. Section collapse remains local viewport state; nested
placement collapse belongs to Andamento. A typed-ABI regression checks that
nested collapse survives producer updates, host observations and parent reopening.

The automated launcher environment used during the experiment contained
`NO_COLOR=1`. The embedded Flotilla TUI inherited it despite having correct TERM
and COLORTERM values. Relaunching the experiment without that automation setting
is the appropriate fix; do not globally override an intentional user NO_COLOR
preference in the application or daily-driver script.

Verification after the fix: the previously failing 11 px disclosure geometry
check passes. Collapsing the Andamento project in the running native window now
leaves a visible right-facing expander and keeps Projects at seven. Activating
that collapsed project's name focuses its TUI without changing tree expansion.
The relaunched TUI has colour headings, selection backgrounds and symbol glyphs;
its process has TERM=xterm-256color, COLORTERM=truecolor, and no NO_COLOR setting.

## Selection, tooltips and title-bar placement

Current-workspace placements use the selected-tab theme on the whole row and a
checkmark. All visible aliases are marked, including a convoy and vessel that
resolve to the same workspace. A dot still denotes an open workspace; disclosure
arrows are only for expansion. The ABI test checks selection on both vessel
placements and its convoy alias.

Rows with rich tooltips opt out of automatic truncated-string hover. Their rich
tooltip already contains the complete label, so rendering both produced overlap.

Title-bar tabs remain independent of menu style. The prototype's fresh profile
had that setting off. Enabling `tabs_in_title_bar` restores the raised tab strips;
the sidebar mode buttons stay in their own control region. Both native-menu and
compact-menu arrangements were checked in native windows.


## Template controls restored

The Projects section now declares the existing Issues display variable. Native
section controls occupy a shared compact row at the bottom of the sidebar,
outside the section viewports. Diagnostic messages sit above that row.
A selected background and border indicate the enabled value from the snapshot;
clicks dispatch the core action. Filtering applies to both Projects and Attention.
An isolated live-catalog window verified the click removed issue rows and changed
Attention from 11 to 6 entries. Empty Sessions is hidden. The five ABI tests,
native build and host diagnostics pass. The user's existing window was left open;
the test window was closed after verification.

See [the core reconciliation](andamento-native-reconciliation.md) for the inspected
abbreviation and inline-layout PRs and the native requirements they need to cover.

## Merged core adoption

The dependency checkout and CI pin now use Andamento's merged PR 91 revision.
Native rows consume the resolved label field, while tooltips retain full labels.
Two additional ABI regressions check abbreviation with preserved field slots and
loop identity across sibling and Attention placements. All seven native ABI tests,
all six ingress tests (including the native executable), the native build and
workspace host diagnostics pass. Live visual verification of abbreviation remains
to be done; the shipped template continues to use full labels.
