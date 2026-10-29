//! An entity's catalog card, asked for by entity rather than read from a
//! snapshot (docs/design/change-stream.md, PR 5.4). A card is the core's
//! answer at one revision: check it with wheelhouse_card_is_current and ask
//! again when it isn't. Its actions are intents on its entity, applied with
//! wheelhouse_apply_intent.
use super::*;

pub struct WheelhouseCard {
    client: u64,
    revision: u64,
    now_ms: u64,
    card: DetailCard,
    has_url: bool,
    /// Its entity's provider was stale when the card was taken.
    stale: bool,
}

/// A card's summary. now_ms is the core's clock when the card was taken, in
/// host monotonic milliseconds, as its fields' observations are.
#[repr(C)]
pub struct CardView {
    pub entity: EntityView,
    pub label: Text,
    pub field_count: usize,
    /// Its subject has a URL: action 1 copies it.
    pub has_url: u32,
    pub now_ms: u64,
    pub error: Text,
    /// The workspace live for the entity, if any.
    pub has_workspace: u32,
    pub workspace_id: WorkspaceIdView,
    /// Its workspace is being opened (none is live yet).
    pub opening: u32,
    /// What a chip of the entity presents, as a WheelhouseStripEntry's:
    /// its status, attention and icon; absent ones are empty.
    pub status: Text,
    pub attention: u32,
    pub icon: Text,
}

/// A card's action: an intent (`kind`, a WHEELHOUSE_INTENT_*) on `entity`.
/// `intent` names what it does, for a host to match a queued action
/// against a later card.
#[repr(C)]
pub struct CardActionView {
    pub intent: Text,
    pub label: Text,
    pub kind: u32,
    pub entity: EntityView,
}

/// A relation of a card's field: an entity and the text to show without
/// its own card (its ID).
#[repr(C)]
pub struct CardRelationView {
    pub entity: EntityView,
    pub display_text: Text,
}

/// The card of `entity` as the core has it now: NULL without an error when
/// the catalog has no such entity. Release it with wheelhouse_card_release;
/// its text stays valid until then.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card_acquire(
    h: *mut WheelhouseCore,
    entity: EntityView,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseCard {
    run(h, error_out, |h| {
        let entity = entity.read()?;
        Ok(match h.sidebar.detail_card(&entity) {
            Some(card) => Box::into_raw(Box::new(WheelhouseCard {
                client: h.client,
                revision: h.sidebar.revision(),
                now_ms: h.sidebar.now_ms(),
                has_url: h.sidebar.subject_url(&card.entity).is_some(),
                stale: h.sidebar.provider_is_stale(&card.entity.provider),
                card,
            })),
            None => ptr::null_mut(),
        })
    })
    .unwrap_or(ptr::null_mut())
}

/// 1 if the card is still the core's answer; 0 for a stale, NULL or other
/// core's card (no error), or an unusable core (error). Its currency is the
/// snapshot's: heartbeats and ticks keep it current.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card_is_current(
    h: *mut WheelhouseCore,
    card: *const WheelhouseCard,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| Ok(card.as_ref().is_some_and(|c| c.client == h.client && c.revision == h.sidebar.revision()))).unwrap_or(false)
        as u32
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card(c: *const WheelhouseCard, out: *mut CardView) -> u32 {
    let (Some(c), Some(out)) = (c.as_ref(), out.as_mut()) else {
        return 0;
    };
    *out = CardView {
        entity: EntityView::of(&c.card.entity),
        label: Text::borrowed(&c.card.label),
        field_count: c.card.fields.len(),
        has_url: c.has_url as u32,
        now_ms: c.now_ms,
        error: Text::borrowed(c.card.error.as_deref().unwrap_or("")),
        has_workspace: c.card.workspace_id.is_some() as u32,
        workspace_id: workspace_view(c.card.workspace_id),
        opening: c.card.opening as u32,
        status: Text::borrowed(c.card.presented.status.as_deref().unwrap_or("")),
        attention: c.card.presented.attention as u32,
        icon: Text::borrowed(c.card.presented.icon.as_deref().unwrap_or("")),
    };
    1
}

/// A card's action: index 0 is its primary action (intent open-url,
/// focus-workspace, materialize-workspace or inspect; kind ACTIVATE),
/// index 1 copies its URL (kind COPY_SUBJECT_URL; 0 when it has none).
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card_action(c: *const WheelhouseCard, index: usize, out: *mut CardActionView) -> u32 {
    let (Some(c), Some(out)) = (c.as_ref(), out.as_mut()) else {
        return 0;
    };
    let (intent, label, kind) = match index {
        0 => (c.card.primary_intent.as_str(), c.card.primary_label.as_str(), WHEELHOUSE_INTENT_ACTIVATE),
        1 if c.has_url => ("copy-url", "Copy URL", WHEELHOUSE_INTENT_COPY_SUBJECT_URL),
        _ => return 0,
    };
    *out = CardActionView { intent: Text::borrowed(intent), label: Text::borrowed(label), kind, entity: EntityView::of(&c.card.entity) };
    1
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card_field(c: *const WheelhouseCard, field: usize, out: *mut DetailFieldView) -> u32 {
    let (Some(c), Some(out)) = (c.as_ref(), out.as_mut()) else {
        return 0;
    };
    let Some(f) = c.card.fields.get(field) else {
        return 0;
    };
    *out = detail_field_view(f, c.stale);
    1
}

/// A relation of a card's field. Pass the navigation path of entities: a
/// relation already on it returns 0.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card_relation(
    c: *const WheelhouseCard,
    field: usize,
    relation: usize,
    path: *const EntityView,
    path_count: usize,
    out: *mut CardRelationView,
) -> u32 {
    let (Some(c), Some(out), Ok(path)) = (c.as_ref(), out.as_mut(), slice(path, path_count)) else {
        return 0;
    };
    let Some(entity) = c.card.fields.get(field).and_then(|f| f.relations.get(relation)) else {
        return 0;
    };
    if !on_path(entity, path) {
        *out = CardRelationView { entity: EntityView::of(entity), display_text: Text::borrowed(&entity.id) };
        return 1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_card_release(c: *mut WheelhouseCard) {
    if !c.is_null() {
        drop(Box::from_raw(c));
    }
}
