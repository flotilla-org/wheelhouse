# Andamento Owns Logical State; Wheelhouse Renders From Its Snapshot

**Status:** accepted, not yet implemented. Decided on
[Map: a target state model for Wheelhouse](https://github.com/flotilla-org/wheelhouse/issues/290),
in [Draw the line between logical state and Presentation State](https://github.com/flotilla-org/wheelhouse/issues/294),
[Decide how and where state is saved](https://github.com/flotilla-org/wheelhouse/issues/296) and
[Decide the target shape of RAD's config tree after logical state moves](https://github.com/flotilla-org/wheelhouse/issues/299).
It revises the 2026-09-07 ruling on
[Map #17](https://github.com/flotilla-org/wheelhouse/issues/17) that "state
belongs to each presentation client": that now applies to Presentation State only.

## Context

Everything Wheelhouse remembers lives in RAD's config tree, saved to a user
file. Andamento, which drives the sidebar, saves nothing; Wheelhouse replays
sidebar state into one Andamento core per window on every launch. Config node
IDs are never saved, so each workspace ends up with three identities, runtime
state hangs off IDs that change on restart, and the user's edits to a
workspace overwrite the provider's suggested layout in place. The TUI and web
frontends built on Andamento cannot share any of it.

## Decision

State is **logical** if another frontend or device would need it to show the
same Dashboard or workspace meaningfully. Andamento owns all logical state:

- the **Dashboard**: provider subscriptions, sections, groups, pins, display
  toggles, order, row collapse and the sidebar's dock arrangement;
- each **Workspace**: identity, subject, label, mood, retained/ended status,
  its **Slots** with their **View Specs**, and its panel tree including tab
  order, selected tabs and relative split weights;
- portable **Target Resolutions**.

Wheelhouse renders from a typed read model built from Andamento's snapshot
and sends it edit operations. It never writes the logical tree. Andamento
exports and imports **named records** (`dashboard`, `workspace/<id>`) as
versioned KDL; the host writes them to a Dashboard directory when their
generation changes. Andamento itself never touches the filesystem.

RAD's config tree keeps only **Presentation State**: absolute geometry, the
visible workspace and focus per window, section collapse, fonts, keybindings
and chrome, keyed by `workspace/view` and panel addresses and stored in a
device-local area. Machine-local Target Resolutions live there too.

## Consequences

- One Andamento core per process, with per-window view handles.
- Workspace IDs are UUIDv7s created by Andamento on materialize; entities are
  `{provider, kind, id}` with the provider taken from the Dashboard's
  subscription.
- Views take a typed `WH_ViewContext` from one registry of Renderers, and
  their state lives as long as their Slot. The evaluator leaves views and
  settings.
- Wheelhouse and Andamento change together more often; their ABI is versioned.
- Switching to Dashboard directories is a fresh start; existing user files are
  not imported.
- Sharing a Dashboard later means moving one directory.
