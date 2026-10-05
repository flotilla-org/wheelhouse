# Section placement

Andamento declares region identity and content. `default-host="sidebar"` and
`order=10` supply initial placement hints; Wheelhouse saves the actual
positions in its existing docking trees. A region's `section` setting is its
stable id. The owning Controlled Split stores a `section_positions` inventory
alongside `control_views` and `floating_panels`. An inventory entry with
`closed` explicitly records an intentional close; missing entries represent
newly declared regions.

Reconciliation retains valid saved Views, removes disappeared regions and
their inventory entries, and places new ids at their hints. The shared docking
checker validates restored and new positions. Wheelhouse recognises `sidebar`
and `floating` host hints; absent, unknown or rejected hosts fall back to the
sidebar. Explicit orders sort lower first; an omitted order uses the region's
zero-based declaration index. Ties preserve declaration order. New sections
are inserted before their next hinted neighbour in the same host, keeping the
saved sections' relative order and identities. Existing nested panel trees
remain intact.

A legacy saved host without an inventory is adopted once in place. Its
currently declared missing Views are recorded as intentional closes because
older Wheelhouse used the presence of the saved host to prevent reopening
them. Regions first declared on later updates are new inventory ids and appear
normally. Reset To Default Panel Layout (and the compact/simple variants)
clears section positions and closes at the root Controlled Split, restoring
current hints. Child Workspace layouts keep their own ownership. Reopening
individual closed sections is deferred to #183.

Run `python tools/test-section-placement.py` after building for the headless
native lifecycle scenarios and KDL source-integrity check. These exercise the
real Andamento ABI, shared docking checker and config serializer, including
restart, add/remove, reset, stable/tied orders, unknown hosts, provider loss,
nested saved splits and invalid saved placement. The source integrity runner
loads the shipped KDL before reordering/resetting its regions. An
authoritative empty declaration clears all positions and closed records; a
missing provider snapshot preserves both. Native snapshots are immutable,
complete acquisitions, so absence of section nodes is a declaration update,
not a partial reload. The operator performs the physical drag check after
merge.
