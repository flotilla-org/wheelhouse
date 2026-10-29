//! The sidebar's change stream and intents (docs/design/change-stream.md).
//! A host subscribes, then polls when wheelhouse_changed_since says to: the
//! first poll's delta holds everything, each later one what changed since the
//! one before. Apply each delta whole to the host's own model of sections,
//! nodes and child lists. Logical state changes by intent. A subscription
//! the sidebar doesn't know (never given, or unsubscribed) is an error from
//! both calls; a host that gets one follows again from a new subscription.
use super::*;
use wheelhouse_core::{
    presentation::{Content, Presented},
    sidebar::{Intent, IntentReceipt},
    stream::{ListOp, NodeContent, Parent, SectionContent, SidebarDelta, SidebarOp, SubscriptionId},
};

/// A section's content, new or changed (`node` is its content).
pub const WHEELHOUSE_OP_SECTION: u32 = 0;
/// A section and its child list go; its nodes go by their own REMOVE ops.
pub const WHEELHOUSE_OP_REMOVE_SECTION: u32 = 1;
/// The order of the sections changes by the list ops (keys are section names).
pub const WHEELHOUSE_OP_SECTIONS: u32 = 2;
/// A node's content, new or changed (`node` is its content).
pub const WHEELHOUSE_OP_UPSERT: u32 = 3;
/// A node and its child list go; its descendants go by their own REMOVE ops.
pub const WHEELHOUSE_OP_REMOVE: u32 = 4;
/// A child list changes by the list ops: a node's (`key`), or a section's
/// when `key` is empty. Its keys are placement keys in `section`.
pub const WHEELHOUSE_OP_CHILDREN: u32 = 5;
/// The display variables, all of them (the op's items index them).
pub const WHEELHOUSE_OP_DISPLAY_VARIABLES: u32 = 6;
/// The diagnostics, all of them.
pub const WHEELHOUSE_OP_DIAGNOSTICS: u32 = 7;
/// The activation errors, all of them.
pub const WHEELHOUSE_OP_ERRORS: u32 = 8;

/// Insert `key` at `at`.
pub const WHEELHOUSE_LIST_INSERT: u32 = 0;
/// Remove the item at `at`.
pub const WHEELHOUSE_LIST_REMOVE: u32 = 1;
/// Take the item at `from` out, and put it in at `to` in the list without it.
pub const WHEELHOUSE_LIST_MOVE: u32 = 2;

/// An intent's kind; WheelhouseIntent says which of its fields each reads.
pub const WHEELHOUSE_INTENT_COPY_SUBJECT_URL: u32 = 0;
pub const WHEELHOUSE_INTENT_ACTIVATE_PLACEMENT: u32 = 1;
pub const WHEELHOUSE_INTENT_ACTIVATE: u32 = 2;
pub const WHEELHOUSE_INTENT_TOGGLE_PLACEMENT: u32 = 3;
pub const WHEELHOUSE_INTENT_SET_VARIABLE: u32 = 4;
pub const WHEELHOUSE_INTENT_TOGGLE_DISPLAY_VARIABLE: u32 = 5;

/// wheelhouse_apply_intent's result: the intent was malformed, or the
/// sidebar unusable (error_out says why), and nothing changed.
pub const WHEELHOUSE_INTENT_INVALID: u32 = 0;
/// Applied: its request is written, its effects are queued for
/// wheelhouse_effects_take, and its changes arrive in deltas.
pub const WHEELHOUSE_INTENT_ACCEPTED: u32 = 1;
/// Not applied, and nothing changed; error_out says why.
pub const WHEELHOUSE_INTENT_REFUSED: u32 = 2;

/// Follow the sidebar's changes. Write the subscription's ID to `out`.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_subscribe(h: *mut WheelhouseCore, out: *mut u64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| {
        let out = out.as_mut().ok_or("null output")?;
        *out = h.sidebar.subscribe().0;
        Ok(())
    })
    .is_some() as u32
}

/// Stop following; 0 (no error) for a subscription that doesn't exist.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_unsubscribe(h: *mut WheelhouseCore, subscription: u64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| Ok(h.sidebar.unsubscribe(SubscriptionId(subscription)))).unwrap_or(false) as u32
}

/// 1 if a poll may have something to deliver. It builds nothing: check after
/// draining a batch, and poll only when it is 1. A change that turns out to
/// present nothing new makes it 1 once. 0 with no error when there is nothing
/// to deliver; 0 with an error for an unknown subscription (never given, or
/// unsubscribed: subscribe again) or on failure.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_changed_since(h: *mut WheelhouseCore, subscription: u64, error_out: *mut *mut c_char) -> u32 {
    run(h, error_out, |h| h.sidebar.changed_since(SubscriptionId(subscription)).map_err(|e| e.to_string())).unwrap_or(false) as u32
}

/// What changed since this subscription's last delta (everything, for its
/// first), with the intents accepted since. NULL with no error when there is
/// nothing to deliver; NULL with an error for an unknown subscription (never
/// given, or unsubscribed: subscribe again) or on failure. The delta owns its
/// text and survives the sidebar; release it with wheelhouse_delta_release.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_poll(h: *mut WheelhouseCore, subscription: u64, error_out: *mut *mut c_char) -> *mut WheelhouseDelta {
    run(h, error_out, |h| {
        let _phase = profile::span("abi-poll");
        let delta = h.sidebar.poll(SubscriptionId(subscription)).map_err(|e| e.to_string())?;
        Ok(delta.map(|delta| Box::into_raw(Box::new(WheelhouseDelta::of(delta)))))
    })
    .flatten()
    .unwrap_or(ptr::null_mut())
}

struct Op {
    kind: u32,
    section: String,
    key: String,
    first: usize,
    count: usize,
}

struct StreamNode {
    section: bool,
    key: String,
    entity: EntityRef,
    stale: bool,
    label: String,
    layout: String,
    form: String,
    loop_key: String,
    state: u32,
    workspace: Option<WorkspaceId>,
    selected: bool,
    openable: bool,
    has_url: bool,
    collapsed: bool,
    pinned: bool,
    default_host: String,
    order: Option<i64>,
    fields: (usize, usize),
    details: (usize, usize),
    controls: (usize, usize),
    presented: Presented,
}

struct List {
    kind: u32,
    at: usize,
    from: usize,
    to: usize,
    key: String,
}

struct Variable {
    name: String,
    label: String,
    icon: String,
    persist: bool,
    value: Option<DisplayVariableValue>,
}

/// One poll's changes. Text it returns is borrowed from it until release.
pub struct WheelhouseDelta {
    generation: u64,
    intents: Vec<u64>,
    ops: Vec<Op>,
    nodes: Vec<StreamNode>,
    fields: Vec<Field>,
    controls: Vec<Control>,
    lists: Vec<List>,
    variables: Vec<Variable>,
    texts: Vec<String>,
    errors: Vec<(EntityRef, String)>,
}

impl WheelhouseDelta {
    fn of(delta: SidebarDelta) -> Self {
        let mut out = Self {
            generation: delta.generation,
            intents: delta.intents,
            ops: vec![],
            nodes: vec![],
            fields: vec![],
            controls: vec![],
            lists: vec![],
            variables: vec![],
            texts: vec![],
            errors: vec![],
        };
        for op in delta.ops {
            let op = match op {
                SidebarOp::Section(section) => {
                    let first = out.section(&section);
                    Op { kind: WHEELHOUSE_OP_SECTION, section: section.name.clone(), key: String::new(), first, count: 1 }
                }
                SidebarOp::RemoveSection { name } => {
                    Op { kind: WHEELHOUSE_OP_REMOVE_SECTION, section: name, key: String::new(), first: NONE, count: 0 }
                }
                SidebarOp::Sections { ops } => {
                    let (first, count) = out.lists(ops, |name| name);
                    Op { kind: WHEELHOUSE_OP_SECTIONS, section: String::new(), key: String::new(), first, count }
                }
                SidebarOp::Upsert { section, node } => {
                    let first = out.node(&node);
                    Op { kind: WHEELHOUSE_OP_UPSERT, section, key: encode_key(&node.key), first, count: 1 }
                }
                SidebarOp::Remove(id) => {
                    Op { kind: WHEELHOUSE_OP_REMOVE, section: id.section, key: encode_key(&id.key), first: NONE, count: 0 }
                }
                SidebarOp::Children { parent, ops } => {
                    let (section, key) = match parent {
                        Parent::Section { name } => (name, String::new()),
                        Parent::Node(id) => (id.section, encode_key(&id.key)),
                    };
                    let (first, count) = out.lists(ops, |key| encode_key(&key));
                    Op { kind: WHEELHOUSE_OP_CHILDREN, section, key, first, count }
                }
                SidebarOp::DisplayVariables { variables, values } => {
                    let first = out.variables.len();
                    out.variables.extend(variables.into_iter().map(|variable| Variable {
                        value: values.get(&variable.name).cloned(),
                        name: variable.name,
                        label: variable.label,
                        icon: variable.icon,
                        persist: variable.persist,
                    }));
                    Op {
                        kind: WHEELHOUSE_OP_DISPLAY_VARIABLES,
                        section: String::new(),
                        key: String::new(),
                        first,
                        count: out.variables.len() - first,
                    }
                }
                SidebarOp::Diagnostics { diagnostics } => {
                    let first = out.texts.len();
                    out.texts.extend(diagnostics);
                    Op {
                        kind: WHEELHOUSE_OP_DIAGNOSTICS,
                        section: String::new(),
                        key: String::new(),
                        first,
                        count: out.texts.len() - first,
                    }
                }
                SidebarOp::Errors { errors } => {
                    let first = out.errors.len();
                    out.errors.extend(errors.into_iter().map(|error| (error.entity, error.message)));
                    Op { kind: WHEELHOUSE_OP_ERRORS, section: String::new(), key: String::new(), first, count: out.errors.len() - first }
                }
            };
            out.ops.push(op);
        }
        out
    }

    fn content(&mut self, content: &Content) -> ((usize, usize), (usize, usize)) {
        let fields = self.fields.len();
        self.fields.extend(content.fields.iter().map(field_of));
        let controls = self.controls.len();
        // A control names what it does: a display-variable control's intent
        // toggles its variable.
        self.controls.extend(content.controls.iter().map(|c| Control {
            kind: control_kind(c.kind),
            label: c.variable.clone().unwrap_or_default(),
            glyph: c.glyph.clone().unwrap_or_default(),
            action: NONE,
            value: String::new(),
            value_kind: WHEELHOUSE_VALUE_NONE,
            checked: false,
            variable: c.variable.clone().unwrap_or_default(),
            persist: false,
        }));
        ((fields, content.fields.len()), (controls, content.controls.len()))
    }

    fn section(&mut self, section: &SectionContent) -> usize {
        let (fields, controls) = self.content(&section.content);
        self.nodes.push(StreamNode {
            section: true,
            key: String::new(),
            entity: EntityRef::new("", "", ""),
            stale: false,
            label: section.name.clone(),
            layout: String::new(),
            form: String::new(),
            loop_key: String::new(),
            state: WHEELHOUSE_CATALOG,
            workspace: None,
            selected: false,
            openable: false,
            has_url: false,
            collapsed: false,
            pinned: section.pinned,
            default_host: section.default_host.clone().unwrap_or_default(),
            order: section.order,
            fields,
            details: (self.fields.len(), 0),
            controls,
            presented: section.content.presented.clone(),
        });
        self.nodes.len() - 1
    }

    fn node(&mut self, node: &NodeContent) -> usize {
        let (fields, controls) = self.content(&node.content);
        let (details, _) = self.content(&node.detail);
        let (state, workspace, selected, openable) = state_view(&node.state);
        self.nodes.push(StreamNode {
            section: false,
            key: encode_key(&node.key),
            entity: node.entity.clone(),
            stale: node.stale,
            label: node.label.clone(),
            layout: node.layout.clone().unwrap_or_default(),
            form: node.form.clone(),
            loop_key: node.loop_key.encode(),
            state,
            workspace,
            selected,
            openable: openable || node.has_url,
            has_url: node.has_url,
            collapsed: node.collapsed,
            pinned: false,
            default_host: String::new(),
            order: None,
            fields,
            details,
            controls,
            presented: node.content.presented.clone(),
        });
        self.nodes.len() - 1
    }

    fn lists<K>(&mut self, ops: Vec<ListOp<K>>, key: impl Fn(K) -> String) -> (usize, usize) {
        let first = self.lists.len();
        for op in ops {
            self.lists.push(match op {
                ListOp::Insert { at, key: k } => List { kind: WHEELHOUSE_LIST_INSERT, at, from: NONE, to: NONE, key: key(k) },
                ListOp::Remove { at } => List { kind: WHEELHOUSE_LIST_REMOVE, at, from: NONE, to: NONE, key: String::new() },
                ListOp::Move { from, to } => List { kind: WHEELHOUSE_LIST_MOVE, at: NONE, from, to, key: String::new() },
            });
        }
        (first, self.lists.len() - first)
    }
}

/// The generation the host's model is at once the delta is applied.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_generation(d: *const WheelhouseDelta) -> u64 {
    d.as_ref().map_or(0, |d| d.generation)
}

/// Intents accepted since the last delta, in order: all they changed is in
/// this delta or an earlier one.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_intent_count(d: *const WheelhouseDelta) -> usize {
    d.as_ref().map_or(0, |d| d.intents.len())
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_intent(d: *const WheelhouseDelta, index: usize, out: *mut u64) -> u32 {
    let (Some(request), Some(out)) = (d.as_ref().and_then(|d| d.intents.get(index)), out.as_mut()) else {
        return 0;
    };
    *out = *request;
    1
}

/// An operation (WHEELHOUSE_OP_*), in the order to apply them.
#[repr(C)]
pub struct DeltaOpView {
    pub kind: u32,
    /// The section the op is about (empty for SECTIONS, DISPLAY_VARIABLES,
    /// DIAGNOSTICS and ERRORS).
    pub section: Text,
    /// UPSERT's and REMOVE's node, or CHILDREN's parent node (empty for a
    /// section's list): an opaque placement key, unique within its section.
    pub key: Text,
    /// SECTION's and UPSERT's content: an index for wheelhouse_delta_node.
    /// SECTIONS' and CHILDREN's first list op, DISPLAY_VARIABLES' first
    /// variable, DIAGNOSTICS' first diagnostic, ERRORS' first error; with
    /// `count` of them.
    pub first: usize,
    pub count: usize,
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_op_count(d: *const WheelhouseDelta) -> usize {
    d.as_ref().map_or(0, |d| d.ops.len())
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_op(d: *const WheelhouseDelta, index: usize, out: *mut DeltaOpView) -> u32 {
    let (Some(op), Some(out)) = (d.as_ref().and_then(|d| d.ops.get(index)), out.as_mut()) else {
        return 0;
    };
    *out =
        DeltaOpView { kind: op.kind, section: Text::borrowed(&op.section), key: Text::borrowed(&op.key), first: op.first, count: op.count };
    1
}

/// A section's or node's own content, without its children. Fields and
/// controls are read with wheelhouse_delta_field and _control. Its typed
/// presented values are what its template's `presents` fields gave; absent
/// ones are empty. A node's facts and variables stay in the core.
#[repr(C)]
pub struct StreamNodeView {
    pub is_section: u32,
    pub key: Text,
    pub entity: EntityView,
    pub stale: u32,
    /// A section's name, or a node's label.
    pub label: Text,
    pub layout: Text,
    pub form: Text,
    /// Opaque loop invocation key: compare for equality, don't parse.
    pub loop_key: Text,
    /// WHEELHOUSE_CATALOG, _LATENT, _OPENING or _LIVE.
    pub state: u32,
    pub workspace_id: WorkspaceIdView,
    pub selected: u32,
    /// Activating it opens something: a latent node that can open, or one
    /// whose subject has a URL.
    pub openable: u32,
    /// Its subject has a URL: COPY_SUBJECT_URL of its entity copies it.
    pub has_url: u32,
    pub collapsed: u32,
    pub pinned: u32,
    pub default_host: Text,
    pub has_order: u32,
    pub order: i64,
    pub first_field: usize,
    pub field_count: usize,
    pub first_detail: usize,
    pub detail_count: usize,
    pub first_control: usize,
    pub control_count: usize,
    pub status: Text,
    pub attention: u32,
    /// A symbolic icon name or a glyph.
    pub icon: Text,
    pub short_label: Text,
    pub medium_label: Text,
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_node(d: *const WheelhouseDelta, index: usize, out: *mut StreamNodeView) -> u32 {
    let (Some(n), Some(out)) = (d.as_ref().and_then(|d| d.nodes.get(index)), out.as_mut()) else {
        return 0;
    };
    let text = |value: &Option<String>| Text::borrowed(value.as_deref().unwrap_or(""));
    *out = StreamNodeView {
        is_section: n.section as u32,
        key: Text::borrowed(&n.key),
        entity: EntityView::of(&n.entity),
        stale: n.stale as u32,
        label: Text::borrowed(&n.label),
        layout: Text::borrowed(&n.layout),
        form: Text::borrowed(&n.form),
        loop_key: Text::borrowed(&n.loop_key),
        state: n.state,
        workspace_id: workspace_view(n.workspace),
        selected: n.selected as u32,
        openable: n.openable as u32,
        has_url: n.has_url as u32,
        collapsed: n.collapsed as u32,
        pinned: n.pinned as u32,
        default_host: Text::borrowed(&n.default_host),
        has_order: n.order.is_some() as u32,
        order: n.order.unwrap_or_default(),
        first_field: n.fields.0,
        field_count: n.fields.1,
        first_detail: n.details.0,
        detail_count: n.details.1,
        first_control: n.controls.0,
        control_count: n.controls.1,
        status: text(&n.presented.status),
        attention: n.presented.attention as u32,
        icon: text(&n.presented.icon),
        short_label: text(&n.presented.short_label),
        medium_label: text(&n.presented.medium_label),
    };
    1
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_field(d: *const WheelhouseDelta, index: usize, out: *mut FieldView) -> u32 {
    let (Some(f), Some(out)) = (d.as_ref().and_then(|d| d.fields.get(index)), out.as_mut()) else {
        return 0;
    };
    *out = f.view();
    1
}
/// A control. Its action is WHEELHOUSE_NONE: a display-variable control's
/// intent is TOGGLE_DISPLAY_VARIABLE of its variable, and its value is the
/// variable's in DISPLAY_VARIABLES.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_control(d: *const WheelhouseDelta, index: usize, out: *mut ControlView) -> u32 {
    let (Some(c), Some(out)) = (d.as_ref().and_then(|d| d.controls.get(index)), out.as_mut()) else {
        return 0;
    };
    *out = c.view();
    1
}

/// A list op (WHEELHOUSE_LIST_*): INSERT reads `at` and `key`, REMOVE `at`,
/// MOVE `from` and `to`; the others are WHEELHOUSE_NONE and empty.
#[repr(C)]
pub struct ListOpView {
    pub kind: u32,
    pub at: usize,
    pub from: usize,
    pub to: usize,
    pub key: Text,
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_list_op(d: *const WheelhouseDelta, index: usize, out: *mut ListOpView) -> u32 {
    let (Some(l), Some(out)) = (d.as_ref().and_then(|d| d.lists.get(index)), out.as_mut()) else {
        return 0;
    };
    *out = ListOpView { kind: l.kind, at: l.at, from: l.from, to: l.to, key: Text::borrowed(&l.key) };
    1
}

/// A display variable, with its value (WHEELHOUSE_VALUE_*).
#[repr(C)]
pub struct DisplayVariableView {
    pub name: Text,
    /// Its label, or its name when it has none.
    pub label: Text,
    pub icon: Text,
    pub persist: u32,
    pub value_kind: u32,
    pub checked: u32,
    pub value: Text,
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_display_variable(d: *const WheelhouseDelta, index: usize, out: *mut DisplayVariableView) -> u32 {
    let (Some(v), Some(out)) = (d.as_ref().and_then(|d| d.variables.get(index)), out.as_mut()) else {
        return 0;
    };
    let (value_kind, checked, value) = match &v.value {
        Some(DisplayVariableValue::Bool(checked)) => (WHEELHOUSE_VALUE_BOOL, *checked, ""),
        Some(DisplayVariableValue::Enum(value)) => (WHEELHOUSE_VALUE_ENUM, false, value.as_str()),
        None => (WHEELHOUSE_VALUE_NONE, false, ""),
    };
    *out = DisplayVariableView {
        name: Text::borrowed(&v.name),
        label: Text::borrowed(if v.label.is_empty() { &v.name } else { &v.label }),
        icon: Text::borrowed(&v.icon),
        persist: v.persist as u32,
        value_kind,
        checked: checked as u32,
        value: Text::borrowed(value),
    };
    1
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_diagnostic(d: *const WheelhouseDelta, index: usize, out: *mut Text) -> u32 {
    let (Some(text), Some(out)) = (d.as_ref().and_then(|d| d.texts.get(index)), out.as_mut()) else {
        return 0;
    };
    *out = Text::borrowed(text);
    1
}

/// An entity whose activation failed, and why.
#[repr(C)]
pub struct ActivationErrorView {
    pub entity: EntityView,
    pub message: Text,
}
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_error(d: *const WheelhouseDelta, index: usize, out: *mut ActivationErrorView) -> u32 {
    let (Some((entity, message)), Some(out)) = (d.as_ref().and_then(|d| d.errors.get(index)), out.as_mut()) else {
        return 0;
    };
    *out = ActivationErrorView { entity: EntityView::of(entity), message: Text::borrowed(message) };
    1
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_delta_release(d: *mut WheelhouseDelta) {
    if !d.is_null() {
        drop(Box::from_raw(d));
    }
}

/// An intent (WHEELHOUSE_INTENT_*). Each kind reads some fields:
/// ACTIVATE_PLACEMENT and TOGGLE_PLACEMENT a node's `key`; COPY_SUBJECT_URL
/// and ACTIVATE an `entity`; SET_VARIABLE `key`, the variable's `name`, and
/// `value` when `has_value` (none resets it); TOGGLE_DISPLAY_VARIABLE `name`.
/// A key is one a delta gave.
#[repr(C)]
pub struct IntentView {
    pub kind: u32,
    pub key: Text,
    pub entity: EntityView,
    pub name: Text,
    pub has_value: u32,
    pub value: Text,
}
impl IntentView {
    unsafe fn read(&self) -> Result<Intent, String> {
        Ok(match self.kind {
            WHEELHOUSE_INTENT_COPY_SUBJECT_URL => Intent::CopySubjectUrl { entity: self.entity.read()? },
            WHEELHOUSE_INTENT_ACTIVATE_PLACEMENT => Intent::ActivatePlacement { key: decode_key(&self.key.read()?)? },
            WHEELHOUSE_INTENT_ACTIVATE => Intent::Activate { entity: self.entity.read()? },
            WHEELHOUSE_INTENT_TOGGLE_PLACEMENT => Intent::TogglePlacement { key: decode_key(&self.key.read()?)? },
            WHEELHOUSE_INTENT_SET_VARIABLE => Intent::SetVariable {
                key: decode_key(&self.key.read()?)?,
                name: self.name.read()?,
                value: if self.has_value != 0 { Some(self.value.read()?) } else { None },
            },
            WHEELHOUSE_INTENT_TOGGLE_DISPLAY_VARIABLE => Intent::ToggleDisplayVariable { name: self.name.read()? },
            other => return Err(format!("unknown intent kind {other}")),
        })
    }
}

/// Apply an intent: WHEELHOUSE_INTENT_ACCEPTED, _REFUSED or _INVALID. An
/// accepted intent's request ID is written to `request_out` (when not NULL),
/// and a later delta lists it.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_apply_intent(
    h: *mut WheelhouseCore,
    intent: *const IntentView,
    request_out: *mut u64,
    error_out: *mut *mut c_char,
) -> u32 {
    let result = run(h, error_out, |h| {
        let intent = intent.as_ref().ok_or("null intent")?.read()?;
        Ok(match h.sidebar.apply_intent(intent) {
            IntentReceipt::Accepted { request, effects } => {
                h.effects.extend(effects);
                if let Some(out) = request_out.as_mut() {
                    *out = request;
                }
                Ok(())
            }
            IntentReceipt::Refused { reason } => Err(reason),
        })
    });
    match result {
        Some(Ok(())) => WHEELHOUSE_INTENT_ACCEPTED,
        Some(Err(reason)) => {
            if !error_out.is_null() {
                *error_out = string(reason);
            }
            WHEELHOUSE_INTENT_REFUSED
        }
        None => WHEELHOUSE_INTENT_INVALID,
    }
}
