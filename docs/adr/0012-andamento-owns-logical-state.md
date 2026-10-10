# Andamento Owns Logical State; Arrangements Are Committed as Documents

**Status:** accepted, not yet implemented. Revised 2026-10-10 after an
adversarial review (see "Revision" below). Decided on
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
  its **Slots** with their **View Specs**, and its **arrangement** (panels,
  tabs by slot key, selected tabs, relative split weights);
- portable **Target Resolutions**.

**Arrangements are documents, not operation streams.** A workspace's
arrangement, and the Dashboard's sidebar arrangement, are each one typed
document. Wheelhouse owns the live copy in a typed C module: docking previews,
drop geometry, validity rules and minimum sizes all need pixels and stay
there. When a gesture ends, Wheelhouse commits the whole document with an
expected generation (`set_arrangement(target, doc, expected_gen)`). Andamento
stores it, validates it against the Slot set, and reconciles it: it places
Slots or regions that appeared since, and reports ones that went away. An
arrangement commit does not invalidate the sidebar's evaluation; Andamento
tracks revisions per concern rather than with one counter.

Andamento exports and imports **named records** (`dashboard`,
`workspace/<id>`) as KDL inside a versioned envelope that preserves unknown
nodes; the host writes them to a Dashboard directory when their generation
changes. Andamento itself never touches the filesystem.

RAD's config tree keeps only **Presentation State**: absolute geometry, the
visible workspace and focus per window, section collapse, fonts, keybindings
and chrome, stored in a device-local area. Machine-local Target Resolutions
live there too.

## Consequences

- Workspace IDs are 128-bit (UUIDv7) and supplied by the host when a
  workspace is first saved, so Andamento stays deterministic for replay. A
  Dashboard has at most one workspace per subject at a time.
- An entity is `{provider, kind, id}`, where the provider is the Dashboard's
  **subscription** ID. The identity a provider reports is checked against the
  subscription but is not part of saved keys, so it can change without
  invalidating them.
- These change Andamento's C ABI. ABI 3 adds the new calls beside ABI 2's,
  and Wheelhouse accepts either until it has moved.
- Views take a typed `WH_ViewContext` from one registry of Renderers. Per-view
  state is keyed by Slot and runtime instance, because a Slot that keeps its
  previous instance has two.
- Switching to Dashboard directories is a fresh start; existing user files are
  not imported.
- Sharing a Dashboard later means moving one directory.
- One Andamento core per process is deferred to the window effort; until
  then each window keeps its own core.

## Revision (2026-10-10)

The first version had Wheelhouse send Andamento one edit operation per
docking change and render from Andamento's snapshot. Five adversarial reviews
found that:

- every Andamento mutation invalidates the whole sidebar evaluation (6–67 ms
  per snapshot depending on entity count), and Wheelhouse writes split
  weights on every drag frame;
- snapshot-validated dispatch would discard a drop when a fact arrived
  mid-gesture;
- the docking logic that needs pixels cannot move to Andamento, so
  operation-level ownership left Andamento a shallow module behind a C ABI;
- the planned interim mirror into config nodes would have released live
  terminal sessions.

Committing whole documents at gesture end keeps the decision (Andamento owns
and shares the logical state) and removes all four problems.
