# Scrollbar follow-up delta port (#117)

Baseline: current main, including precise scrolling PR #127 (`3842785`).
RAD reference frontier: `bfbefb3cc9f458b9accd46d00e80daaf96572628`.

| RAD source | Disposition |
| --- | --- |
| `0a5f9ffb` shared region/styled bar, thumb size and fractional target geometry | Already present in Wheelhouse. PR #127 additionally preserves fractional drag results through Wheelhouse's precise-scroll accumulator; retain that rather than RAD's integer conversion. Extend geometry and drag origin to include the displayed animation displacement. |
| `eff47af5` list begin/end region integration, virtual rows after thumb changes | Already present. Add a positioned, nested list scenario that switches styles in one UI state and consumes precise input exactly once. Adapt the table consumer's width measurement to the selected region viewport. |
| `eff47af5` RAD setting plumbing | Irrelevant; retain Wheelhouse's settings and scope. |
| `fc5e03cb` positioned list hover via persistent content rectangle | Adapted. Unkeyed parents have no retained screen rectangle; use the keyed content box's laid-out rectangle. Preserve the `d643a7d` inert-preview ancestor guard. |
| `fc5e03cb` user/window setting scope move | Intentionally omitted; the issue explicitly preserves Wheelhouse's existing scopes. |
| `14c7b93a` smooth, weak classic arrow styling | Adapted on both arrows and axes; assert theme tags, raster flags, background color, and border/background draw flags. |
| `14c7b93a` debugger memory row/header translation | Irrelevant; no matching Wheelhouse consumer. No debugger-specific views imported from `dd989b62` or `77d731e0`. |

Classic track clicks now move one visible extent (a page), while arrow controls
move one index. Overlay tracks remain non-clickable. Geometry reserves separate
two-axis corner footprints, and empty classic furniture remains disabled.

The existing native-build workflow runs the extended shared-state diagnostics on
Linux/Xvfb, macOS, and Windows, plus AppKit scroll event tests on macOS. Tests use
real UI state, layout, hit testing, signals, and event consumption; no mock of the
scrollbar algorithm is introduced. Linux builds use the workflow's pinned Cleat,
Andamento, and Jackstay revisions with `WHEELHOUSE_CLEAT_FEATURES=none`.

Native acceptance remains pending: inspect classic/overlay appearance and perform
physical drag/page/arrow interaction in positioned lists and sidebar sections,
switch styles while scrolled, and verify inert overview tiles cannot reveal or
operate live scrollbars. Xvfb synthetic input does not verify physical trackpad,
menu, or title-bar behavior. The daily driver is not replaced or restarted.

## Linux evidence

- `bash build.sh wheelhouse`: passed with pinned providers and Cleat features `none`.
- `python3 tools/check-generated.py`: committed generated sources match.
- `--scroll_region_diagnostics`: zero failures, including positioned nested lists,
  style switching, fractional animated controls, corner hits, shrink and empty state.
- `--sidebar_diagnostics`: passed, including precise scrolling and overflow selection.
- `--preview_diagnostics`: zero failures; overlapping inert previews remain hidden.
- Each GUI run used software GL, private Xvfb and fresh user/project settings.
- Before fix: 28 control failures and two positioned-hover failures.
- Mutation: old hover calculation caused two positioned-hover failures; one-index
  page clicks caused eight control failures. Both mutants were reverted.
