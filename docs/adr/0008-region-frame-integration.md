# Region Frames Own Selection Integration

**Status:** proposed.

Panel tabs, workspace inventory entries, and future control-surface selectors all express the same visual relationship: a shell chrome selection handle may read as part of the frame-bearing region selected by its binding. The current panel-only frame segment pass was useful as a checkpoint, but it exposes the wrong long-term ownership boundary: exact frame integration wants a post-layout region/frame resolver, not per-widget borders or tab-specific notch math.

The proposed direction is that frame-bearing regions and selection handles feed one resolver. Normal split boundaries, controlled split boundaries, workspace edges, panel bodies, tab strips, and control-region/workspace-region selection bindings should all describe region adjacency and frame-integration intent; transient drag/drop hit regions remain separate. The immediate renderer may still emit filled edge rects and rounded rects, while richer output such as merged SDF regions, interior curved corners, and continuous material textures remains a future backend direction.

This keeps the current branch's lesson without rushing the renderer: frame integration is semantic first, and rendering primitives should be chosen after the region relationships are explicit.
