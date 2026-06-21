# Workspace Theme Presets Resolve at Surface Boundaries

**Status:** proposed.

The first per-workspace theme slice uses a preset name stored on the workspace config, not a full override bundle. A workspace may carry `theme: "Preset Name"`; if absent, it falls back through the default workspace/window theme and then the user/default theme. This is intentionally narrow: a preset field is enough to prove the scoping and rendering seams, while still allowing later extension to named theme objects, override bundles, KDL theme files, metrics, assets, and post-effects.

Workspace content renders under that workspace's resolved theme. The visible workspace uses its own theme; sidebar previews and overview tiles use the same rendered workspace texture, so they naturally show the previewed workspace's theme without a separate preview-specific theme path. The selected control-surface handle also uses the selected workspace's theme so it can read as part of the selected workspace region.

Outer shell chrome remains under the default workspace/default theme for this slice: the control-surface background, title bar, and other non-workspace host chrome should not accidentally vary while iterating over workspace previews. Tabs in the title bar are a deferred boundary case. Longer term, tab strip pieces can render in their workspace theme, and the region/frame resolver can decide when themed regions visually merge or require separating borders.

The initial implementation should therefore add workspace preset storage and a theme-resolution helper, then push/pop the resolved theme around each workspace surface render and the selected workspace handle. Theme identity is part of cached rendering state: theme color lookup caches must include the active theme, and cached workspace surfaces must include the resolved workspace theme in their content version so a theme-only change cannot preserve stale pixels. Editing can arrive through a later workspace settings affordance from the control surface.
