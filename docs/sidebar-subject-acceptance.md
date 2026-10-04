# Sidebar subject chips: acceptance

Convoy subjects are inline chips, in PR/issue numeric order. Workspace actions
share their area and one `+N` menu. Attention retains standalone PR rows, and
Issues, Show finished and Role attempts retain their existing visibility rules.

At width pressure, the name uses the producer's medium and short labels, then
elides to about ten characters. Quiet chips fold from the end of catalog order.
Ready-to-merge and CI-failing subjects, selected/pending workspaces and active
open workspaces remain visible. If those protected chips alone exceed the
available width, their area scrolls horizontally while the status slot stays
fixed. The last chip has two pixels of clearance for its border stroke.

Workspace actions use fixed-width icons. `presentation.icon` wins over a local
template `icon-override` field, then the entity kind supplies the default.
Supported symbolic icons are `terminal`, `overview`, `role`, `threads`, `gear`,
`code`, `review` and `workspace`; other suggestions are literal glyphs. No name
is used to guess an icon. Open workspaces have a stronger fill and left edge,
selected workspaces have the selection fill, and pending actions show an
ellipsis without changing width. Every action retains its full label in the
hover card.

To assign a local role icon, select a template explicitly, for example a
`role/governor` template extending `role/native`, overriding `icon-override`
with `source="literal" value="gear" prefix="chip-icon-override:"`. Match that
role loop on `flotilla.role.name`, rather than changing the renderer. Producer
suggestions still win. The shipped template leaves the override empty.

## Kiwi human review

The operator reviewed the host-direct candidate with disposable profiles at
480-point and 260-point sidebar widths on 2026-10-04. The first review requested
clearance around the final governor chip's right border. After the two-pixel
correction, the operator accepted the presentation: “Looks ok”. The daily
driver was not restarted or replaced.

![Wide: convoy name and every subject/workspace chip](screenshots/convoy-chips/wide.png)

![Narrow: short name, both attention subjects, one shared overflow](screenshots/convoy-chips/narrow.png)

![Overflow contains quiet PR, issue and workspace action](screenshots/convoy-chips/overflow.png)

Reproduce with disposable settings:

```sh
./build/wheelhouse --sidebar_subject_fixture --user:/tmp/chips-user --project:/tmp/chips-project
```

- Hover a subject for its title, readiness, checks, review and observation times.
- Click a subject to open its canonical URL; right-click for Copy URL.
- The no-forge subject offers Copy reference and has no browser action.
- Open Subject workspace from `+N`: its active chip remains on the convoy row.
- Toggle Issues, Show finished and Role attempts to check existing placements.

The fixture forge uses `/review/` and `/ticket/` to expose hard-coded URL shapes.

## Verification

The macOS debug build uses the vessel's Cleat and Andamento checkouts, kiwi's
prepared Ghostty prefix, and `/Users/robert/dev/jackstay` at the workflow-pinned
revision. The 26 native ABI tests include the production width resolver's name
ladder, quiet fold order, protected workspace and extreme-width behavior.
The generated-fixture locale/newline test passes.

Native sidebar diagnostics check laid-out status geometry at 240, 320 and
600 pixels through latent, pending and removed-subject states. Both attention
subjects remain chips, and the existing workspace, selection, reveal, scrolling
and project-motion diagnostics pass. Icon resolution checks cover suggestion
precedence, a local template override and a default for a role with an arbitrary
label. The macOS PR chip Copy URL interaction also passed against the real
clipboard.

The X11 action test now reads actual fixture hit rectangles through
`--sidebar_subject_geometry:<path>` instead of clicking the retired child-row
positions. The optional geometry output is restricted to the embedded subject
fixture. Its browser subprocess records destinations without navigating, and
its clipboard reader checks chip and Attention copies. This X11 test is not
exercised on kiwi; macOS mouse interaction and the real native ABI cover the
local acceptance run. Cross-platform build results are recorded on the PR.
