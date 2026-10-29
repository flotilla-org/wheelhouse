#![cfg(feature = "json")]
use std::{mem::MaybeUninit, ptr};
use wheelhouse_core::tab_workspace_id;
use wheelhouse_ffi::*;
fn text(s: &str) -> Text {
    Text { data: s.as_ptr(), len: s.len() }
}
const PROVIDER: &str = "sub-1";
fn entity(kind: Text, id: Text) -> EntityView {
    EntityView { provider: text(PROVIDER), kind, id }
}
unsafe fn read(t: Text) -> String {
    String::from_utf8(std::slice::from_raw_parts(t.data, t.len).to_vec()).unwrap()
}
// A card's field by name.
unsafe fn field(card: *const WheelhouseCard, name: &str) -> DetailFieldView {
    let index = field_index(card, name);
    let mut f = MaybeUninit::uninit();
    assert_eq!(wheelhouse_card_field(card, index, f.as_mut_ptr()), 1);
    f.assume_init()
}
unsafe fn field_index(card: *const WheelhouseCard, name: &str) -> usize {
    let mut c = MaybeUninit::uninit();
    assert_eq!(wheelhouse_card(card, c.as_mut_ptr()), 1);
    for i in 0..c.assume_init().field_count {
        let mut f = MaybeUninit::uninit();
        assert_eq!(wheelhouse_card_field(card, i, f.as_mut_ptr()), 1);
        if read(f.assume_init().name) == name {
            return i;
        }
    }
    panic!("missing field {name}")
}
unsafe fn card_info(card: *const WheelhouseCard) -> CardView {
    let mut c = MaybeUninit::uninit();
    assert_eq!(wheelhouse_card(card, c.as_mut_ptr()), 1);
    c.assume_init()
}
unsafe fn acquire(h: *mut WheelhouseCore, kind: &str, id: &str) -> *mut WheelhouseCard {
    let mut error = ptr::null_mut();
    let card = wheelhouse_card_acquire(h, entity(text(kind), text(id)), &mut error);
    assert!(error.is_null());
    card
}
unsafe fn acquire_worktree(h: *mut WheelhouseCore) -> *mut WheelhouseCard {
    let card = acquire(h, "worktree", "w");
    assert!(!card.is_null());
    card
}
unsafe fn action(card: *const WheelhouseCard, index: usize) -> CardActionView {
    let mut a = MaybeUninit::uninit();
    assert_eq!(wheelhouse_card_action(card, index, a.as_mut_ptr()), 1);
    a.assume_init()
}
// Applies a card's action as the intent it is.
unsafe fn apply(h: *mut WheelhouseCore, a: &CardActionView) -> u32 {
    let intent = IntentView { kind: a.kind, key: text(""), entity: a.entity, name: text(""), has_value: 0, value: text("") };
    wheelhouse_apply_intent(h, &intent, ptr::null_mut(), ptr::null_mut())
}
// Whether the card of worktree `w` says its workspace is opening, and has one.
unsafe fn card_opening(h: *mut WheelhouseCore) -> (u32, u32) {
    let card = acquire_worktree(h);
    let c = card_info(card);
    wheelhouse_card_release(card);
    (c.opening, c.has_workspace)
}
// The kinds of the effects queued since the last take.
unsafe fn effect_kinds(h: *mut WheelhouseCore) -> Vec<u32> {
    let effects = wheelhouse_effects_take(h, ptr::null_mut());
    let kinds = (0..wheelhouse_effects_count(effects))
        .map(|i| {
            let mut e = MaybeUninit::uninit();
            assert_eq!(wheelhouse_effects_get(effects, i, e.as_mut_ptr()), 1);
            e.assume_init().kind
        })
        .collect();
    wheelhouse_effects_release(effects);
    kinds
}
// Acceptance scenario: all six catalog kinds have a card that preserves typed
// roles, known-empty facts, observation provenance and relations even when
// there is no tree node.
#[test]
fn six_kind_native_detail_contract() {
    unsafe {
        let h = wheelhouse_create(ptr::null(), 0, ptr::null_mut());
        assert!(!h.is_null());
        assert_eq!(wheelhouse_abi_version(), 16);
        for line in include_str!("../../../fixtures/typed-detail.jsonl").lines() {
            assert_eq!(wheelhouse_apply_patch_json(h, 42, text(PROVIDER), text(line), ptr::null_mut()), 1);
        }
        for kind in ["change_request", "issue", "convoy", "role", "project", "worktree"] {
            let card = acquire(h, kind, &format!("{kind}:identity / #"));
            assert!(!card.is_null(), "{kind} has a card");
            let f = field(card, "summary");
            assert_eq!(f.role, 3);
            assert_eq!(read(f.section), "facts");
            assert_eq!(read(f.label), "Summary");
            assert_eq!(f.has_value, 1);
            assert_eq!(read(f.text), "");
            assert!(!f.text.data.is_null());
            assert_eq!(f.has_observation, 1);
            assert_eq!(f.observed_at_ms, 42);
            assert_eq!(f.stale, 0);
            assert_eq!(read(f.source_key), "summary.text");
            assert_eq!(read(f.source_id), "fixture-producer");
            assert_eq!(field(card, "identity").role, 0);
            assert_eq!(field(card, "label").role, 1);
            assert_eq!(field(card, "state").role, 2);
            let related = field(card, "related");
            assert_eq!(related.role, 4);
            assert!(related.relation_count >= 1);
            wheelhouse_card_release(card);
        }
        // Missing facts remain declared, without fabricating an empty observation.
        let card = acquire(h, "worktree", "worktree:identity / #");
        let missing = field(card, "branch");
        assert_eq!(missing.has_value, 0);
        assert_eq!(missing.has_observation, 0);
        assert!(!missing.text.data.is_null());
        assert!(!missing.source_id.data.is_null());
        // A relation gives its target's identity and ID; the target's own
        // card has its label.
        let related = field_index(card, "related");
        let mut r = MaybeUninit::uninit();
        assert_eq!(wheelhouse_card_relation(card, related, 0, ptr::null(), 0, r.as_mut_ptr()), 1);
        let r = r.assume_init();
        assert_eq!(read(r.entity.kind), "issue");
        assert_eq!(read(r.entity.id), "issue:identity / #");
        assert_eq!(read(r.display_text), "issue:identity / #");
        let target = wheelhouse_card_acquire(h, r.entity, ptr::null_mut());
        assert_eq!(read(card_info(target).label), "Display issue");
        wheelhouse_card_release(target);
        // A relation already on the navigation path is left out.
        let path = [r.entity];
        assert_eq!(wheelhouse_card_relation(card, related, 0, path.as_ptr(), 1, MaybeUninit::uninit().as_mut_ptr()), 0);
        // A relation to an entity the catalog doesn't have is still given; it has no card.
        let mut unavailable = MaybeUninit::uninit();
        assert_eq!(wheelhouse_card_relation(card, related, 1, ptr::null(), 0, unavailable.as_mut_ptr()), 1);
        let mut error = ptr::null_mut();
        assert!(wheelhouse_card_acquire(h, unavailable.assume_init().entity, &mut error).is_null());
        assert!(error.is_null());
        let primary = action(card, 0);
        assert_eq!(read(primary.intent), "inspect");
        assert_eq!(read(primary.entity.id), "worktree:identity / #");
        assert_eq!(card_info(card).has_workspace, 0);
        // The card's action, applied as its intent, emits Inspect.
        assert_eq!(primary.kind, WHEELHOUSE_INTENT_ACTIVATE);
        assert_eq!(apply(h, &primary), WHEELHOUSE_INTENT_ACCEPTED);
        assert_eq!(effect_kinds(h), [WHEELHOUSE_EFFECT_INSPECT]);
        // A card keeps its facts after mutations: time alone changes
        // nothing, and a new card sees the producer's unset.
        wheelhouse_tick(h, 143, ptr::null_mut());
        let boundary = acquire(h, "worktree", "worktree:identity / #");
        assert_eq!(field(boundary, "summary").has_value, 1);
        let unset = r#"{"target":{"kind":"entity","value":{"kind":"worktree","id":"worktree:identity / #"}},"source_id":"fixture-producer","unset":["summary.text"]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 144, text(PROVIDER), text(unset), ptr::null_mut()), 1);
        let expired = acquire(h, "worktree", "worktree:identity / #");
        assert_eq!(field(expired, "summary").has_value, 0);
        assert_eq!(field(card, "summary").has_value, 1);
        assert_eq!(wheelhouse_card_is_current(h, card, ptr::null_mut()), 0);
        assert!(acquire(h, "unknown", "").is_null());
        assert_eq!(wheelhouse_card_field(card, usize::MAX, MaybeUninit::uninit().as_mut_ptr()), 0);
        for c in [expired, boundary, card] {
            wheelhouse_card_release(c);
        }
        wheelhouse_destroy(h);
    }
}

// Lifecycle scenario: materialization uses the primary control label, its
// card is opening from the request until the workspace is live (as a chip
// draws it), an identical re-send changes nothing, and while its provider is stale a retained
// fact exposes its writer and receipt time, marked stale, through the card.
#[test]
fn native_controls_resends_and_retained_staleness() {
    unsafe {
        let config = r#"
        region "tree" root-template="title" placement="tree"
        template "title" { field "label" literal="Tree"; }
        placement "tree" { for "worktree" kind="worktree" { field "label" key="display.label"; }; }
    "#;
        let h = wheelhouse_create(config.as_ptr(), config.len(), ptr::null_mut());
        assert!(!h.is_null());
        let patch = r#"{"target":{"kind":"entity","value":{"kind":"worktree","id":"w"}},"source_id":"producer","set":{"display.label":{"value":{"type":"text","value":"Worker"}},"summary.text":{"value":{"type":"text","value":"observed"}},"action.primary.target":{"value":{"type":"text","value":"worktree:w"}},"action.primary.recipe":{"value":{"type":"text","value":"exec sh"}},"action.primary.label":{"value":{"type":"text","value":"Attach"}}}}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 100, text(PROVIDER), text(patch), ptr::null_mut()), 1);
        let card = acquire_worktree(h);
        let primary = action(card, 0);
        assert_eq!(read(primary.intent), "materialize-workspace");
        assert_eq!(read(primary.label), "Attach");
        assert_eq!(card_opening(h), (0, 0), "not asked for yet");
        assert_eq!(apply(h, &primary), WHEELHOUSE_INTENT_ACCEPTED);
        assert_eq!(card_opening(h), (1, 0), "asked for, not live");
        wheelhouse_card_release(card);
        let effects = wheelhouse_effects_take(h, ptr::null_mut());
        let mut effect = MaybeUninit::uninit();
        assert_eq!(wheelhouse_effects_get(effects, 0, effect.as_mut_ptr()), 1);
        let effect = effect.assume_init();
        assert_eq!(effect.kind, WHEELHOUSE_EFFECT_MATERIALIZE);
        assert_eq!(
            wheelhouse_complete(
                h,
                effect.request_id,
                WHEELHOUSE_COMPLETE_MATERIALIZE,
                tab_workspace_id(7).into(),
                text(""),
                ptr::null_mut()
            ),
            1
        );
        wheelhouse_effects_release(effects);
        let workspace = WorkspaceInput { id: tab_workspace_id(7).into(), position: 0, name: text("Worker"), selected: 1 };
        assert_eq!(wheelhouse_observe(h, &workspace, 1, ptr::null(), 0, ptr::null_mut()), 1);
        assert_eq!(card_opening(h), (0, 1), "live");
        assert_eq!(wheelhouse_apply_patch_json(h, 105, text(PROVIDER), text(patch), ptr::null_mut()), 1);
        let fresh = acquire_worktree(h);
        assert_eq!(field(fresh, "summary").observed_at_ms, 100, "an identical re-send changes nothing");
        assert_eq!(wheelhouse_tick(h, 10_000, ptr::null_mut()), 1);
        let boundary = acquire_worktree(h);
        assert_eq!(field(boundary, "summary").stale, 0);
        // The producer's connection drops: its facts are kept, and stale.
        assert_eq!(wheelhouse_provider_set_stale(h, text(PROVIDER), 1, ptr::null_mut()), 1);
        let expired = acquire_worktree(h);
        let f = field(expired, "summary");
        assert_eq!(f.has_value, 1);
        assert_eq!(f.stale, 1);
        assert_eq!(f.observed_at_ms, 100);
        assert_eq!(read(f.source_id), "producer");
        let c = card_info(expired);
        assert!(!c.error.data.is_null());
        assert_eq!(c.has_workspace, 1);
        assert_eq!(c.workspace_id.bytes, tab_workspace_id(7).to_bytes());
        for card in [expired, boundary, fresh] {
            wheelhouse_card_release(card);
        }
        wheelhouse_destroy(h);
    }
}

// An entity's fact is its last writer's, whatever another source wrote before,
// and its writer's unset removes it.
#[test]
fn last_writer_provenance() {
    unsafe {
        let h = wheelhouse_create(ptr::null(), 0, ptr::null_mut());
        // Another fact keeps the entity when the summary goes.
        let label = r#"{"target":{"kind":"entity","value":{"kind":"worktree","id":"w"}},"source_id":"z-source","set":{"display.label":{"value":{"type":"text","value":"w"}}}}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(label), ptr::null_mut()), 1);
        for source in ["z-source", "a-source"] {
            let patch = format!(
                r#"{{"target":{{"kind":"entity","value":{{"kind":"worktree","id":"w"}}}},"source_id":"{source}","set":{{"summary.text":{{"value":{{"type":"text","value":"same"}}}}}}}}"#
            );
            assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(&patch), ptr::null_mut()), 1);
        }
        let card = acquire_worktree(h);
        assert_eq!(read(field(card, "summary").source_id), "a-source");
        let unset = r#"{"target":{"kind":"entity","value":{"kind":"worktree","id":"w"}},"source_id":"a-source","unset":["summary.text"]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 1, text(PROVIDER), text(unset), ptr::null_mut()), 1);
        let next = acquire_worktree(h);
        assert_eq!(field(next, "summary").has_value, 0);
        wheelhouse_card_release(next);
        wheelhouse_card_release(card);
        wheelhouse_destroy(h);
    }
}

// A card is the core's answer when it was taken: it reports the core's clock
// then, it is current until the core changes, and it owns its text, which
// stays readable after later patches and the core's destruction.
#[test]
fn a_card_owns_its_answer() {
    unsafe {
        let h = wheelhouse_create(ptr::null(), 0, ptr::null_mut());
        for line in include_str!("../../../fixtures/typed-detail.jsonl").lines() {
            assert_eq!(wheelhouse_apply_patch_json(h, 42, text(PROVIDER), text(line), ptr::null_mut()), 1);
        }
        let held = acquire(h, "worktree", "worktree:identity / #");
        let c = card_info(held);
        assert_eq!(c.now_ms, 42);
        let (borrowed, label) = (c.label, read(c.label));
        // An unexpired tick leaves the card current, at the clock it was taken at.
        assert_eq!(wheelhouse_tick(h, 43, ptr::null_mut()), 1);
        assert_eq!(wheelhouse_card_is_current(h, held, ptr::null_mut()), 1);
        assert_eq!(card_info(held).now_ms, 42);
        let later = acquire(h, "worktree", "worktree:identity / #");
        assert_eq!(card_info(later).now_ms, 43);
        wheelhouse_card_release(later);
        assert!(acquire(h, "issue", "new").is_null());
        let patch = r#"{"target":{"kind":"entity","value":{"kind":"issue","id":"new"}},"source_id":"test","set":{"display.label":{"value":{"type":"text","value":"New"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 43, text(PROVIDER), text(patch), ptr::null_mut()), 1);
        assert_eq!(read(borrowed), label);
        assert_eq!(wheelhouse_card_is_current(h, held, ptr::null_mut()), 0);
        let new = acquire(h, "issue", "new");
        assert!(!new.is_null());
        assert_eq!(read(card_info(new).label), "New");
        let unset = r#"{"target":{"kind":"entity","value":{"kind":"issue","id":"new"}},"source_id":"test","unset":["display.label"]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 44, text(PROVIDER), text(unset), ptr::null_mut()), 1);
        assert!(acquire(h, "issue", "new").is_null());
        assert_eq!(read(card_info(new).label), "New");
        wheelhouse_destroy(h);
        assert_eq!(read(borrowed), label);
        assert_eq!(read(card_info(new).label), "New");
        wheelhouse_card_release(new);
        wheelhouse_card_release(held);
    }
}

// A non-placement region still resolves its template and reports errors
// through the snapshot; the narrow path cannot silently skip it.
#[test]
fn nonplacement_region_reports_template_diagnostic() {
    unsafe {
        let config = "region \"legacy\" root-template=\"missing-template\"";
        let h = wheelhouse_create(config.as_ptr(), config.len(), ptr::null_mut());
        assert!(!h.is_null());
        let snapshot = wheelhouse_snapshot_acquire(h, ptr::null_mut());
        assert!(wheelhouse_snapshot_diagnostic_count(snapshot) > 0);
        wheelhouse_snapshot_release(snapshot);
        wheelhouse_destroy(h);
    }
}

// Unreadable text at the C boundary is an error, with no card. Empty null
// text is valid by the shared Text contract: no such entity, no error.
#[test]
fn a_card_request_rejects_invalid_text() {
    unsafe {
        let h = wheelhouse_create(ptr::null(), 0, ptr::null_mut());
        let invalid_utf8 = [0xff];
        let bad_texts = [Text { data: ptr::null(), len: 1 }, Text { data: invalid_utf8.as_ptr(), len: 1 }];
        for bad in bad_texts {
            for (kind, id) in [(bad, text("id")), (text("kind"), bad)] {
                let mut error = ptr::null_mut();
                assert!(wheelhouse_card_acquire(h, entity(kind, id), &mut error).is_null());
                assert!(!error.is_null());
                wheelhouse_string_free(error);
            }
        }
        let mut error = ptr::null_mut();
        assert!(wheelhouse_card_acquire(h, entity(Text { data: ptr::null(), len: 0 }, text("")), &mut error).is_null());
        assert!(error.is_null());
        wheelhouse_destroy(h);
    }
}
