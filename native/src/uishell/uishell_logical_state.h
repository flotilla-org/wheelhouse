// The logical state as deterministic text: what a restart, another frontend
// or another device must reproduce (ADR 0012), then a separate Presentation
// section. It is the seam the state-model migration checks behaviour through:
// each step reimplements it against new storage and keeps its output, so it
// names things by meaning and never prints config node IDs, GUIDs, pointers
// or timestamps, or anything else that depends on config node shape.
#ifndef UISHELL_LOGICAL_STATE_H
#define UISHELL_LOGICAL_STATE_H

internal String8 uishell_logical_state_text(Arena *arena);

#endif
