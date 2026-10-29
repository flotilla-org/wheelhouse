# Structured details

This is the core's contract for Wheelhouse #177, part of ABI 4 ([The host
interface](sidebar-design/host-interface.md)). The authoritative C layouts and
pointer lifetimes are in `crates/wheelhouse-ffi/include/wheelhouse.h`. Every
identity here is an `WheelhouseEntity`, `{provider, kind, id}`; kind and ID
alone name nothing.

## Template declarations

Detail templates remain `<kind>/detail`, slot `detail`. Fields accept optional
`section`, `role`, `label`, and `structured-only` properties in both KDL and JSON:

```kdl
template "issue/detail" slot="detail" node-kind="entity" {
  field "title" section="header" role="title" key="display.label"
  field "state" section="header" role="state" key="status.state"
  field "ci" section="facts" role="fact" label="CI" key="ci.state"
  field "related" structured-only=true section="related" role="relation" key="flotilla.subject.works_on"
}
```

| Role | C tag | Meaning |
| --- | --- | --- |
| `identity` | 0 | Header identity |
| `title` | 1 | Header title |
| `state` | 2 | State badge |
| `fact` | 3 | Labeled fact |
| `relation` | 4 | Typed related entities |

Sections are frontend-neutral strings; bundled declarations use `header`, `facts`
and `related`. An absent section or label is an empty string. Fields without a
role remain available to flat clients and are omitted from structured cards.
`structured-only=true` adds a declaration without adding a flat field or reserving
its columns. Existing `prefix` and `suffix` affect flat output only. Structured
text resolves directly from the source, including configured transformations,
without recovering labels or identities from rendered text. Inheritance,
fragments, overrides and effective KDL dumps preserve these properties.

Bundled detail templates cover `change_request`, `issue`, `convoy`, `role`,
`project` and `worktree`; other kinds use the generic detail fallback. Bundled
relation fields consume `related.entities`, `flotilla.subject.produces`,
`flotilla.subject.works_on`, `flotilla.subject_of`, and
`flotilla.role.current_attempt`. Producers supply `MetadataValue::EntityRefs`;
text and string lists do not implicitly become relationships. Other typed
relationship keys can be declared by configuration. Existing legacy text keys,
such as `flotilla.project`, are not inferred as links.

## Cards by entity

A host asks for an entity's card by entity (docs/design/change-stream.md,
PR 5.4); a snapshot carries no cards:

| Export | Result |
| --- | --- |
| `wheelhouse_card_acquire` | One exact entity's card as the core has it now; NULL, without an error, when the catalog has no such entity, and NULL with an error for unreadable text |
| `wheelhouse_card_is_current` | Whether the card is still the core's answer: the snapshot's currency (revision and client) |
| `wheelhouse_card` | Entity, label, field count, `has_url`, clock, error, optional Workspace ID, `opening`, and the entity's chip status, attention and icon |
| `wheelhouse_card_field` | A `WheelhouseDetailField`: name, section, role, label, unadorned text, presence, source and observation metadata, `stale`, relation count |
| `wheelhouse_card_relation` | Exact target identity and its ID as fallback text, filtered by navigation path; ask for the target's own card for its label |
| `wheelhouse_card_action` | Semantic intent, label, target identity and the `WHEELHOUSE_INTENT_*` kind to apply: 0 is the primary action (`ACTIVATE`), 1 copies the URL (`COPY_SUBJECT_URL`, only with `has_url`) |
| `wheelhouse_card_release` | Release it; its text is valid until then |

A card owns all the text it returns, which stays valid after later changes
and the core's destruction, until release. It is the core's answer at one
revision: its `now_ms` is the core's host-monotonic clock when it was
acquired, and it stays current through heartbeats and ticks that change
nothing. Ask again when it is no longer current; the old card stays readable.
A card's actions are intents on its entity, applied with
`wheelhouse_apply_intent`, so there is no stale action index: a host that
queues an action keeps its entity and intent name, and matches them against a
current card when it acts. Native keeps the cards it asked for until the first,
the oldest, is no longer current. The core's revision cache retains at most 64
cards or misses.

### Card contents

Lookup is over the complete resolved catalog, including entities absent from all
placements or hidden by presentation filters, hidden relation targets and
retained ended subjects. A configuration resolution failure is reported in the
card's `error`; no field contents are guessed.

`has_value=0` denotes a missing fact; `has_value=1` with zero-length text denotes
known empty. Zero-length output text has a non-null pointer. Empty literals and
empty typed lists are also known values. Missing fields remain declared so
consumers can choose their presentation. `source_key` identifies the winning
metadata source. When no fallback source resolves, it is empty; no declared key
is guessed as the cause of a missing fact. Literal/synthetic values also have no
source key.

`has_observation` distinguishes an observation from a synthetic value.
`observed_at_ms` is the winning fact's controller receipt/refresh time, in the
same **host monotonic millisecond domain** as the card's `now_ms`, not a Unix
or producer event timestamp. `source_id` identifies that winning producer.
An entity's fact is its last writer's, so `source_id` is the source that last
wrote it, and it is kept with retained observations. An identical re-send
from the same source keeps the observation time, so age denotes the last
change of value or writer. A fact has no lease: `stale` is set while the
provider that wrote it is stale (its connection dropped), and the fact goes
only when its source unsets it or its provider is retracted. Frontends own
relative-age formatting and stale colors.

Relations come directly from typed entity references. Self references are omitted; duplicate references retain only their first occurrence.
Iterate `relation_count`, passing the exact entity navigation path to each
relation lookup; return 0 means skip that index. Path filtering does not renumber
indices. A relation gives the target's identity and its ID as `display_text`;
ask for the target's card for its label. A target with no placement still has a
card; a target absent from the catalog has none (`wheelhouse_card_acquire`
returns NULL without an error). Never parse relation field text for targets.

Action index 0 is the primary action, kind `WHEELHOUSE_INTENT_ACTIVATE`; index 1
is copy URL, kind `WHEELHOUSE_INTENT_COPY_SUBJECT_URL`, when the card has
`has_url`. Primary intent is `open-url`, `focus-workspace`,
`materialize-workspace`, or `inspect`; the copy intent is `copy-url`. The primary
label uses the same `action.primary.label` fact as workspace controls, with a
semantic default. Apply an action with `wheelhouse_apply_intent`, as an intent
of its kind on its entity; it targets the stable entity and produces the
existing effects/completions. Primary activation retains the existing
precedence: a subject URL opens in the browser before workspace focus.

Preview identity is separate from all field roles: `has_workspace` and
`workspace_id` (16 bytes) identify a live workspace even for a card without a placement.
For a card anchored to a specific visible alias, the existing node workspace
identity remains available. Both peek and engaged cards may draw the same live
preview; #89 owns attachment policy, #166 owns detaching, and Wheelhouse owns
layout, colors, gestures and the visibility of actions in engaged cards. The
Zellij reserved hover panel is unchanged.

## Validation

`fixtures/typed-detail.jsonl` supplies all six kinds, known-empty summary facts,
missing kind-specific facts, and typed self/hidden/unavailable relations.
Core tests enumerate every role across missing and known-empty values and check
KDL/JSON agreement, effective KDL round trips, separate labels/text, empty
literals, invalid roles and preservation of flat output. Native Rust tests check
that every catalog entity's card answers what the core's catalog card says and
that its actions do what the core's actions do, and cover path omission, exact
targets, observation provenance, provider staleness, action intents, card
currency and owned card lifetimes, and the rejection of unreadable text. The
independent C fixture links the real library and checks the actual card
layouts, a card's live preview identity and its action applied as an intent,
along with the other ABI scenarios.
