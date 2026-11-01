//! Marshalling the core can't see: what C reads from snapshots and effects.
use super::*;
use crate::tables::*;
use std::ffi::CStr;
use std::mem::MaybeUninit;
use wheelhouse_core::tab_workspace_id;

const PROVIDER: &str = "sub-1";

fn text(s: &str) -> Text {
    Text::borrowed(s)
}

unsafe fn create(config: &str) -> *mut WheelhouseCore {
    let mut error = ptr::null_mut();
    let h = wheelhouse_create(config.as_ptr(), config.len(), &mut error);
    assert!(!h.is_null(), "{}", CStr::from_ptr(error).to_string_lossy());
    h
}

unsafe fn node(s: *const WheelhouseSnapshot, index: usize) -> NodeView {
    let mut node = MaybeUninit::uninit();
    assert_eq!(wheelhouse_snapshot_node(s, index, node.as_mut_ptr()), 1);
    node.assume_init()
}

// A section node carries its region's hints; entity nodes and the
// synthesised workspace section have none. Orders span both signed
// boundaries (kdl 4 rejects i64::MIN, so the low one is MIN+1).
#[test]
fn section_nodes_carry_their_region_hints() {
    unsafe {
        for order in [None, Some(i64::MIN + 1), Some(0), Some(i64::MAX)] {
            let config = format!(
                "region \"test\" root-template=\"flotilla/region/tree\" default-host=\"sidebar\"{}",
                order.map(|o| format!(" order={o}")).unwrap_or_default()
            );
            let h = create(&config);
            let workspace = WorkspaceInput { id: tab_workspace_id(42).into(), position: 0, name: text("unplaced"), selected: 1 };
            assert_eq!(wheelhouse_observe(h, &workspace, 1, ptr::null(), 0, ptr::null_mut()), 1);
            let s = wheelhouse_snapshot_acquire(h, ptr::null_mut());
            let section = node(s, 0);
            assert_eq!(section.default_host.read().unwrap(), "sidebar");
            assert_eq!((section.has_order, section.order), (order.is_some() as u32, order.unwrap_or_default()));
            for i in 1..wheelhouse_snapshot_node_count(s) {
                let other = node(s, i);
                assert_eq!((other.default_host.len, other.has_order), (0, 0));
                if other.is_section == 0 {
                    assert_eq!(other.workspace_id.bytes, tab_workspace_id(42).to_bytes());
                }
            }
            wheelhouse_snapshot_release(s);
            wheelhouse_destroy(h);
        }
    }
}

#[test]
fn declared_abbreviation_reaches_c_fields_but_keeps_full_details() {
    unsafe {
        let config = include_str!("../../wheelhouse-core/tests/fixtures/abbreviation.kdl");
        let mut error = ptr::null_mut();
        let h = create(config);
        let patch = r#"{"type":"metadata-patch","target":{"kind":"entity","value":{"kind":"vessel","id":"worker"}},"source_id":"test","set":{"flotilla.vessel":{"value":{"type":"text","value":"worker"}},"display.label":{"value":{"type":"text","value":"a very long worker name"}},"display.label.medium":{"value":{"type":"text","value":"worker medium"}},"display.label.short":{"value":{"type":"text","value":"w"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(patch), &mut error), 1);
        for (declaration, expected) in [("", "worker medium"), ("tier=\"short\"", "w"), ("tier=\"full\"", "a very long worker name")] {
            let config = config.replace("kind=\"vessel\"", &format!("kind=\"vessel\" {declaration}"));
            assert_eq!(wheelhouse_configure(h, text(&config), &mut error), 1);
            let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
            let node = node(snapshot, 1);
            assert_eq!(node.label.read().unwrap(), "a very long worker name");
            assert_eq!(node.entity.provider.read().unwrap(), PROVIDER);
            let mut field = MaybeUninit::<FieldView>::uninit();
            assert_eq!(wheelhouse_snapshot_field(snapshot, node.first_field, field.as_mut_ptr()), 1);
            assert_eq!(field.assume_init_ref().text.read().unwrap(), expected);
            assert_eq!(wheelhouse_snapshot_field(snapshot, node.first_detail, field.as_mut_ptr()), 1);
            assert_eq!(field.assume_init_ref().text.read().unwrap(), "a very long worker name");
            wheelhouse_snapshot_release(snapshot);
        }
        wheelhouse_destroy(h);
        assert!(error.is_null());
    }
}

// A workspace subject without an opening recipe is openable, and its
// materialization carries an empty recipe and no managed target.
#[test]
fn a_workspace_subject_materializes_with_an_empty_recipe() {
    unsafe {
        let config = format!("{}\nworkspace kind=\"vessel\"\n", include_str!("../../wheelhouse-core/tests/fixtures/abbreviation.kdl"));
        let mut error = ptr::null_mut();
        let h = create(&config);
        let patch = r#"{"type":"metadata-patch","target":{"kind":"entity","value":{"kind":"vessel","id":"worker"}},"source_id":"test","set":{"flotilla.vessel":{"value":{"type":"text","value":"worker"}},"display.label":{"value":{"type":"text","value":"worker"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(patch), &mut error), 1);
        let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
        let row = node(snapshot, 1);
        assert_eq!((row.state, row.openable), (WHEELHOUSE_LATENT, 1));
        assert_eq!(wheelhouse_dispatch(h, snapshot, row.activate, &mut error), 1);
        let effects = wheelhouse_effects_take(h, &mut error);
        assert_eq!(wheelhouse_effects_count(effects), 1);
        let mut effect = MaybeUninit::<EffectView>::uninit();
        assert_eq!(wheelhouse_effects_get(effects, 0, effect.as_mut_ptr()), 1);
        let effect = effect.assume_init();
        assert_eq!((effect.kind, effect.recipe.read().unwrap().as_str(), effect.has_primary_target), (1, "", 0));
        wheelhouse_effects_release(effects);
        wheelhouse_snapshot_release(snapshot);
        wheelhouse_destroy(h);
        assert!(error.is_null());
    }
}

// wheelhouse#104 through C: a subject with a direct Cleat endpoint
// materializes with an empty recipe and no primary target, and the first
// plan of its primary carries the Cleat session. A host's own Cleat session
// slot round-trips through set and get.
#[test]
fn a_direct_cleat_primary_reaches_c_and_cleat_slots_round_trip() {
    unsafe {
        let config = format!("{}\nworkspace kind=\"vessel\"\n", include_str!("../../wheelhouse-core/tests/fixtures/abbreviation.kdl"));
        let mut error = ptr::null_mut();
        let h = create(&config);
        let fact = |key: &str, value: &str| format!(r#""{key}":{{"value":{{"type":"text","value":"{value}"}}}}"#);
        let set = [
            fact("flotilla.vessel", "worker"),
            fact("display.label", "worker"),
            fact("action.primary.recipe", "flotilla attach worker"),
            fact("action.primary.direct.transport", "ssh"),
            fact("action.primary.direct.host", "robert@feta"),
            fact("action.primary.direct.runtime_root", "/run/cleat"),
            fact("action.primary.direct.daemon", "d1"),
            fact("action.primary.direct.session", "s1"),
        ]
        .join(",");
        let patch = format!(
            r#"{{"type":"metadata-patch","target":{{"kind":"entity","value":{{"kind":"vessel","id":"worker"}}}},"source_id":"test","set":{{{set}}},"unset":[]}}"#
        );
        assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(&patch), &mut error), 1);
        let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
        assert_eq!(wheelhouse_dispatch(h, snapshot, node(snapshot, 1).activate, &mut error), 1);
        wheelhouse_snapshot_release(snapshot);
        let effects = wheelhouse_effects_take(h, &mut error);
        let mut effect = MaybeUninit::<EffectView>::uninit();
        assert_eq!(wheelhouse_effects_get(effects, 0, effect.as_mut_ptr()), 1);
        let effect = effect.assume_init();
        assert_eq!(
            (effect.kind, effect.recipe.read().unwrap().as_str(), effect.has_primary_target),
            (WHEELHOUSE_EFFECT_MATERIALIZE, "", 0)
        );
        let request = effect.request_id;
        wheelhouse_effects_release(effects);
        let id = WorkspaceId::from_bytes([9; 16]);
        assert_eq!(wheelhouse_complete(h, request, WHEELHOUSE_COMPLETE_MATERIALIZE, id.into(), text(""), &mut error), 1);
        let workspace = WorkspaceInput { id: id.into(), position: 0, name: text("worker"), selected: 1 };
        assert_eq!(wheelhouse_observe(h, &workspace, 1, ptr::null(), 0, &mut error), 1);

        let cleat_fields = |spec: &ViewSpecView| {
            [&spec.cleat_transport, &spec.cleat_host, &spec.cleat_runtime_root, &spec.cleat_daemon, &spec.cleat_session]
                .map(|field| field.read().unwrap())
        };
        let plan = wheelhouse_slot_plan(h, id.into(), text("primary"), text(""), &mut error);
        assert_eq!(((*plan).state, (*plan).recipe.content), (WHEELHOUSE_CONTENT_UPDATING, WHEELHOUSE_SLOT_CLEAT_SESSION));
        assert_eq!(cleat_fields(&(*plan).recipe), ["ssh", "robert@feta", "/run/cleat", "d1", "s1"]);
        assert_eq!((*plan).recipe.command.read().unwrap(), "");
        wheelhouse_slot_plan_free(plan);

        let mut spec: ViewSpecView = std::mem::zeroed();
        spec.content = WHEELHOUSE_SLOT_CLEAT_SESSION;
        spec.cleat_transport = text("local");
        spec.cleat_host = text("feta");
        spec.cleat_runtime_root = text("/run/cleat");
        spec.cleat_daemon = text("d2");
        spec.cleat_session = text("s2");
        assert_eq!(wheelhouse_slot_set(h, id.into(), text("u:crew"), &spec, WHEELHOUSE_REBIND_REPLACE, &mut error), 1);
        let slots = wheelhouse_slots_get(h, id.into(), &mut error);
        let crew = slice((*slots).slots, (*slots).slot_count).unwrap().iter().find(|slot| slot.key.read().unwrap() == "u:crew").unwrap();
        assert_eq!(crew.spec.content, WHEELHOUSE_SLOT_CLEAT_SESSION);
        assert_eq!(cleat_fields(&crew.spec), ["local", "feta", "/run/cleat", "d2", "s2"]);
        wheelhouse_slots_free(slots);
        assert!(error.is_null());
        // An unknown transport, an empty field and an option are refused.
        for (transport, session) in [("mosh", "s2"), ("ssh", ""), ("ssh", "-oProxyCommand=x")] {
            spec.cleat_transport = text(transport);
            spec.cleat_session = text(session);
            assert_eq!(wheelhouse_slot_set(h, id.into(), text("u:bad"), &spec, WHEELHOUSE_REBIND_REPLACE, &mut error), 0);
            assert!(!error.is_null());
            wheelhouse_string_free(error);
            error = ptr::null_mut();
        }
        wheelhouse_destroy(h);
    }
}

// A workspace's layouts reach C: their list and glyphs, the one an
// arrangement is of, the slots it leaves unplaced and where showing them puts
// them, choosing another, the slots' roles and the subject's strip.
#[test]
fn workspace_layouts_reach_c() {
    unsafe {
        let template = r#"
workspace kind="vessel" {
  slot "shell" role="shell" kind="command" command="zsh"
  layout "agent" glyph="A" { tabs id="main" { slots role="agent"; }; }
  layout "all" { tabs id="main" { slots rest=true; }; }
  strip { linked { for "issue" kind="issue" { match "flotilla.vessel" of="vessel"; order "display.label"; }; }; }
}
template "issue/compact" slot="compact" node-kind="entity" {
  field "label" key="display.label"
  field "icon" key="presentation.icon" presents="icon"
  field "status" key="status.state" presents="status"
  field "attention" key="status.attention" presents="attention"
}
"#;
        let config = format!("{}\n{template}", include_str!("../../wheelhouse-core/tests/fixtures/abbreviation.kdl"));
        let mut error = ptr::null_mut();
        let h = create(&config);
        let patch = r#"{"type":"metadata-patch","target":{"kind":"entity","value":{"kind":"vessel","id":"worker"}},"source_id":"test","set":{"flotilla.vessel":{"value":{"type":"text","value":"worker"}},"display.label":{"value":{"type":"text","value":"worker"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(patch), &mut error), 1);
        // i1 presents an icon, a status and attention; i2 none of them.
        for (issue, vessel, presented) in [
            ("i2", "worker", ""),
            (
                "i1",
                "worker",
                r#","presentation.icon":{"value":{"type":"text","value":"review"}},"status.state":{"value":{"type":"text","value":"ci_failing"}},"status.attention":{"value":{"type":"bool","value":true}}"#,
            ),
            ("i3", "other", ""),
        ] {
            let patch = format!(
                r#"{{"type":"metadata-patch","target":{{"kind":"entity","value":{{"kind":"issue","id":"{issue}"}}}},"source_id":"test","set":{{"flotilla.vessel":{{"value":{{"type":"text","value":"{vessel}"}}}},"display.label":{{"value":{{"type":"text","value":"{issue}"}}}}{presented}}},"unset":[]}}"#
            );
            assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(&patch), &mut error), 1);
        }
        let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
        assert_eq!(wheelhouse_dispatch(h, snapshot, node(snapshot, 1).activate, &mut error), 1);
        wheelhouse_snapshot_release(snapshot);
        let effects = wheelhouse_effects_take(h, &mut error);
        let mut effect = MaybeUninit::<EffectView>::uninit();
        assert_eq!(wheelhouse_effects_get(effects, 0, effect.as_mut_ptr()), 1);
        let request = effect.assume_init().request_id;
        wheelhouse_effects_release(effects);
        let id = WorkspaceId::from_bytes([7; 16]);
        assert_eq!(wheelhouse_complete(h, request, WHEELHOUSE_COMPLETE_MATERIALIZE, id.into(), text(""), &mut error), 1);
        let workspace = WorkspaceInput { id: id.into(), position: 0, name: text("worker"), selected: 1 };
        assert_eq!(wheelhouse_observe(h, &workspace, 1, ptr::null(), 0, &mut error), 1);

        let list = wheelhouse_layouts_get(h, id.into(), &mut error);
        let layouts: Vec<(String, String, u32)> = slice((*list).layouts, (*list).count)
            .unwrap()
            .iter()
            .map(|layout| (layout.name.read().unwrap(), layout.glyph.read().unwrap(), layout.flags))
            .collect();
        assert_eq!(
            layouts,
            [("agent".into(), "A".into(), WHEELHOUSE_LAYOUT_DEFAULT | WHEELHOUSE_LAYOUT_CURRENT), ("all".into(), String::new(), 0)]
        );
        wheelhouse_layouts_free(list);

        let arrangement = |error: &mut *mut c_char| {
            let arrangement = wheelhouse_arrangement_get(h, id.into(), error);
            let layout = (*arrangement).layout.read().unwrap();
            let notes = slice((*arrangement).notes, (*arrangement).note_count).unwrap();
            let unplaced: Vec<String> = notes
                .iter()
                .filter(|note| note.kind == WHEELHOUSE_NOTE_UNPLACED)
                .map(|note| format!("{} in {}", note.key.read().unwrap(), note.panel.read().unwrap()))
                .collect();
            assert!(notes.iter().filter(|note| note.kind != WHEELHOUSE_NOTE_UNPLACED).all(|note| note.panel.len == 0));
            let has = (*arrangement).has_layout;
            wheelhouse_arrangement_free(arrangement);
            (has, layout, unplaced)
        };
        assert_eq!(arrangement(&mut error), (1, "agent".into(), vec!["t:shell in main".to_owned()]));
        assert_eq!(wheelhouse_layout_choose(h, id.into(), 1, text("all"), &mut error), 1);
        assert_eq!(arrangement(&mut error), (1, "all".into(), vec![]));
        assert_eq!(wheelhouse_layout_choose(h, id.into(), 1, text("nope"), &mut error), 0);
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        error = ptr::null_mut();
        // A name that isn't UTF-8, and a null handle, fail with an error and
        // change nothing.
        let bad = [0xffu8, 0xfe];
        assert_eq!(wheelhouse_layout_choose(h, id.into(), 1, Text { data: bad.as_ptr(), len: bad.len() }, &mut error), 0);
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        error = ptr::null_mut();
        assert_eq!(arrangement(&mut error), (1, "all".into(), vec![]));
        assert_eq!(wheelhouse_layout_choose(ptr::null_mut(), id.into(), 0, text(""), &mut error), 0);
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        error = ptr::null_mut();
        assert!(wheelhouse_layouts_get(ptr::null_mut(), id.into(), &mut error).is_null());
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        error = ptr::null_mut();

        let slots = wheelhouse_slots_get(h, id.into(), &mut error);
        let roles: Vec<(String, u32, String)> = slice((*slots).slots, (*slots).slot_count)
            .unwrap()
            .iter()
            .map(|slot| (slot.key.read().unwrap(), slot.spec.has_role, slot.spec.role.read().unwrap()))
            .collect();
        assert_eq!(roles, [("primary".into(), 1, "agent".into()), ("t:shell".into(), 1, "shell".into())]);
        wheelhouse_slots_free(slots);

        let subject = EntityView { provider: text(PROVIDER), kind: text("vessel"), id: text("worker") };
        let strip = wheelhouse_strip_get(h, subject, &mut error);
        let listed: Vec<(u32, String, String, String, u32)> = slice((*strip).entries, (*strip).count)
            .unwrap()
            .iter()
            .map(|entry| {
                let id = format!("{}/{}", entry.entity.kind.read().unwrap(), entry.entity.id.read().unwrap());
                (entry.section, id, entry.icon.read().unwrap(), entry.status.read().unwrap(), entry.attention)
            })
            .collect();
        assert_eq!(
            listed,
            [
                (WHEELHOUSE_STRIP_LINKED, "issue/i1".into(), "review".into(), "ci_failing".into(), 1),
                (WHEELHOUSE_STRIP_LINKED, "issue/i2".into(), String::new(), String::new(), 0)
            ]
        );
        // Each entity's card presents what its entry does.
        for entry in slice((*strip).entries, (*strip).count).unwrap() {
            let card = wheelhouse_card_acquire(h, entry.entity, &mut error);
            let mut c = MaybeUninit::uninit();
            assert_eq!(wheelhouse_card(card, c.as_mut_ptr()), 1);
            let c = c.assume_init();
            assert_eq!(
                (c.icon.read().unwrap(), c.status.read().unwrap(), c.attention),
                (entry.icon.read().unwrap(), entry.status.read().unwrap(), entry.attention)
            );
            wheelhouse_card_release(card);
        }
        wheelhouse_strip_free(strip);
        let missing = EntityView { provider: text(PROVIDER), kind: text("vessel"), id: text("nobody") };
        let strip = wheelhouse_strip_get(h, missing, &mut error);
        assert_eq!((*strip).count, 0);
        wheelhouse_strip_free(strip);
        assert!(wheelhouse_strip_get(ptr::null_mut(), subject, &mut error).is_null());
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        error = ptr::null_mut();
        wheelhouse_destroy(h);
        assert!(error.is_null());
    }
}

// A layout's weights through C: an empty panel is shown at no weight, a
// commit of the shares shown records no override, a resize is flagged
// RESIZED, and resetting the layout gives its weights back.
#[test]
fn layout_weights_reach_c_as_shown() {
    unsafe {
        let template = r#"
workspace kind="vessel" {
  slot "shell" role="shell" kind="command" command="zsh"
  slot "logs" role="shell" kind="command" command="tail"
  layout "pair" {
    split axis="row" {
      tabs id="agents" weight=3 { slots role="agent"; };
      tabs id="side" weight=2 { slots role="shell"; };
      tabs id="files" weight=1 { slots role="files"; };
    };
  }
}
"#;
        let config = format!("{}\n{template}", include_str!("../../wheelhouse-core/tests/fixtures/abbreviation.kdl"));
        let mut error = ptr::null_mut();
        let h = create(&config);
        let patch = r#"{"type":"metadata-patch","target":{"kind":"entity","value":{"kind":"vessel","id":"worker"}},"source_id":"test","set":{"flotilla.vessel":{"value":{"type":"text","value":"worker"}},"display.label":{"value":{"type":"text","value":"worker"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(patch), &mut error), 1);
        let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
        assert_eq!(wheelhouse_dispatch(h, snapshot, node(snapshot, 1).activate, &mut error), 1);
        wheelhouse_snapshot_release(snapshot);
        let effects = wheelhouse_effects_take(h, &mut error);
        let mut effect = MaybeUninit::<EffectView>::uninit();
        assert_eq!(wheelhouse_effects_get(effects, 0, effect.as_mut_ptr()), 1);
        let request = effect.assume_init().request_id;
        wheelhouse_effects_release(effects);
        let id = WorkspaceId::from_bytes([7; 16]);
        assert_eq!(wheelhouse_complete(h, request, WHEELHOUSE_COMPLETE_MATERIALIZE, id.into(), text(""), &mut error), 1);
        let workspace = WorkspaceInput { id: id.into(), position: 0, name: text("worker"), selected: 1 };
        assert_eq!(wheelhouse_observe(h, &workspace, 1, ptr::null(), 0, &mut error), 1);

        // The weights shown, whether resized, and the generation.
        let read = |arrangement: *mut WheelhouseWorkspaceArrangement| {
            let a = &*arrangement;
            let weights: Vec<f64> = slice(a.doc.panels, a.doc.panel_count).unwrap().iter().skip(1).map(|panel| panel.weight).collect();
            let out = (weights, a.flags & (WHEELHOUSE_ARRANGEMENT_FLAG_SOFT | WHEELHOUSE_ARRANGEMENT_FLAG_RESIZED), a.owned, a.generation);
            wheelhouse_arrangement_free(arrangement);
            out
        };
        // Commit the document shown with these weights, as a host would,
        // selecting the side panel's second tab.
        let commit = |weights: [f64; 3], error: &mut *mut c_char| {
            let shown = wheelhouse_arrangement_get(h, id.into(), error);
            let mut panels: Vec<WheelhouseDocPanel> =
                slice((*shown).doc.panels, (*shown).doc.panel_count).unwrap().iter().map(|p| ptr::read(p)).collect();
            for (panel, weight) in panels.iter_mut().skip(1).zip(weights) {
                panel.weight = weight;
            }
            panels[2].selected = 1;
            let doc = WheelhouseArrangementDoc { panels: panels.as_ptr(), panel_count: panels.len() };
            let committed = wheelhouse_arrangement_commit(h, id.into(), &doc, (*shown).generation, error);
            wheelhouse_arrangement_free(shown);
            read(committed)
        };
        let (weights, resized, owned, _) = read(wheelhouse_arrangement_get(h, id.into(), &mut error));
        assert_eq!((weights, resized, owned), (vec![3.0, 2.0, 0.0], 0, 0));
        let (weights, resized, owned, _) = commit([0.6, 0.4, 0.0], &mut error);
        assert_eq!((weights, resized, owned), (vec![3.0, 2.0, 0.0], WHEELHOUSE_ARRANGEMENT_FLAG_SOFT, 0), "the shares shown are no resize");
        let (weights, resized, owned, generation) = commit([0.5, 0.5, 0.0], &mut error);
        let both = WHEELHOUSE_ARRANGEMENT_FLAG_SOFT | WHEELHOUSE_ARRANGEMENT_FLAG_RESIZED;
        assert_eq!((weights, resized, owned), (vec![2.5, 2.5, 0.0], both, 0), "a resize, in the layout's scale");
        assert_eq!(
            wheelhouse_arrangement_resolve(h, id.into(), WHEELHOUSE_ARRANGEMENT_FOLLOW, generation, ptr::null_mut(), &mut error),
            WHEELHOUSE_COMMITTED
        );
        let (weights, resized, _, _) = read(wheelhouse_arrangement_get(h, id.into(), &mut error));
        assert_eq!((weights, resized), (vec![3.0, 2.0, 0.0], 0), "reset");
        wheelhouse_destroy(h);
        assert!(error.is_null());
    }
}

// A display-variable control names its variable and whether it persists;
// both borrow from the snapshot, which outlives the sidebar.
#[test]
fn display_controls_name_their_variable_and_persistence() {
    unsafe {
        let h = create(
            r#"version 1
            display-variable "history" type="bool" default=false label="Show finished" icon="F" persist=true
            display-variable "temporary" type="bool" default=true label="Temporary" icon="T" persist=false
            region "tree" root-template="test" form="compact" placement="tree"
            placement "tree" { for "item" kind="test"; }
            template "test" slot="compact" node-kind="entity" {
                control "display-variable" variable="history"
                control "display-variable" variable="temporary"
                control "display-variable" variable="undeclared"
                control "open-config" label="Configure"
            }"#,
        );
        let mut error = ptr::null_mut();
        let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
        let control = |index| {
            let mut control = MaybeUninit::<ControlView>::uninit();
            assert_eq!(wheelhouse_snapshot_control(snapshot, index, control.as_mut_ptr()), 1);
            control.assume_init()
        };
        let controls: Vec<_> = (0..4).map(control).collect();
        let named: Vec<_> = controls.iter().map(|c| (c.variable.read().unwrap(), c.persist, c.label.read().unwrap())).collect();
        assert_eq!(
            named,
            [
                ("history".into(), 1, "Show finished".into()),
                ("temporary".into(), 0, "Temporary".into()),
                ("undeclared".into(), 0, "undeclared".into()),
                (String::new(), 0, String::new()),
            ]
        );
        assert_eq!(wheelhouse_dispatch(h, snapshot, controls[0].action, &mut error), 1);
        let next = wheelhouse_snapshot_acquire(h, &mut error);
        assert!((&(*next).controls)[0].checked && !(&(*snapshot).controls)[0].checked);
        wheelhouse_destroy(h);
        assert_eq!(controls[2].variable.read().unwrap(), "undeclared");
        wheelhouse_snapshot_release(snapshot);
        wheelhouse_snapshot_release(next);
        assert!(error.is_null());
    }
}

#[test]
fn panic_is_contained_and_poisoned_client_keeps_existing_snapshots_alive() {
    unsafe {
        let mut error = ptr::null_mut();
        let h = wheelhouse_create(ptr::null(), 0, &mut error);
        let s = wheelhouse_snapshot_acquire(h, &mut error);
        let count = wheelhouse_snapshot_node_count(s);
        let result: Option<()> = run(h, &mut error, |_| panic!("injected failure"));
        assert!(result.is_none());
        assert!(CStr::from_ptr(error).to_str().unwrap().contains("panicked"));
        wheelhouse_string_free(error);
        assert_eq!(wheelhouse_tick(h, 0, &mut error), 0);
        assert!(CStr::from_ptr(error).to_str().unwrap().contains("poisoned"));
        wheelhouse_string_free(error);
        wheelhouse_destroy(h);
        assert_eq!(wheelhouse_snapshot_node_count(s), count);
        wheelhouse_snapshot_release(s);
    }
}

// The native host receives a usable activation and a snapshot-scoped copy
// action for subjects, with resolved URLs owned by their effect batches.
#[cfg(feature = "json")]
#[test]
fn subject_rows_expose_open_and_copy_url_actions() {
    unsafe {
        let mut error = ptr::null_mut();
        let h = create(include_str!("../../../templates/flotilla-default.kdl"));
        for line in include_str!("../../../fixtures/subject-entities.jsonl").lines() {
            let frame: serde_json::Value = serde_json::from_str(line).unwrap();
            if frame["offset_ms"] != 0 {
                continue;
            }
            let patch = serde_json::to_string(&frame["patch"]).unwrap();
            assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(&patch), &mut error), 1);
        }
        let snapshot = wheelhouse_snapshot_acquire(h, &mut error);
        let index = (*snapshot).nodes.iter().position(|n| n.entity.id == "github/org/b!281").unwrap();
        let row = node(snapshot, index);
        assert_eq!(row.openable, 1);
        assert_ne!(row.copy_url, NONE);
        for (action, kind) in [(row.activate, 3), (row.copy_url, 4)] {
            assert_eq!(wheelhouse_dispatch(h, snapshot, action, &mut error), 1);
            let effects = wheelhouse_effects_take(h, &mut error);
            assert_eq!(wheelhouse_effects_count(effects), 1);
            let mut effect = MaybeUninit::<EffectView>::uninit();
            assert_eq!(wheelhouse_effects_get(effects, 0, effect.as_mut_ptr()), 1);
            let effect = effect.assume_init();
            assert_eq!(effect.kind, kind);
            assert_eq!(effect.recipe.read().unwrap(), "https://github.com/org/b/pull/281");
            wheelhouse_effects_release(effects);
        }
        let issue = EntityView { provider: text(PROVIDER), kind: text("issue"), id: text("github/org/a#115") };
        assert_eq!(wheelhouse_copy_subject_url(h, issue, &mut error), 1);
        let effects = wheelhouse_effects_take(h, &mut error);
        assert_eq!(wheelhouse_effects_count(effects), 1);
        wheelhouse_effects_release(effects);
        // An entity names its provider.
        let unnamed = EntityView { provider: text(""), ..issue };
        assert_eq!(wheelhouse_copy_subject_url(h, unnamed, &mut error), 0);
        assert!(CStr::from_ptr(error).to_str().unwrap().contains("provider"));
        wheelhouse_string_free(error);
        wheelhouse_snapshot_release(snapshot);
        wheelhouse_destroy(h);
    }
}

#[test]
fn create_rejects_null_nonempty_and_invalid_utf8() {
    unsafe {
        for (data, len) in [(ptr::null(), 1), ([255].as_ptr(), 1)] {
            let mut error = ptr::null_mut();
            assert!(wheelhouse_create(data, len, &mut error).is_null());
            assert!(!error.is_null());
            wheelhouse_string_free(error);
        }
    }
}

thread_local! {
    /// Moves a followed delta made, to show a session reordered something.
    static MOVES: std::cell::Cell<usize> = const { std::cell::Cell::new(0) };
}

/// A node's label, state, collapse and field texts.
type FollowedNode = (String, u32, u32, Vec<String>);

/// What a host following the stream knows: sections in order, each node's
/// content by section and key, and each child list.
#[derive(Debug, Default, PartialEq)]
struct Followed {
    sections: Vec<String>,
    /// (section, key) to label, state, collapsed and field texts; a section's
    /// own content has an empty key.
    nodes: BTreeMap<(String, String), FollowedNode>,
    children: BTreeMap<(String, String), Vec<String>>,
}

unsafe fn texts(first: usize, count: usize, get: impl Fn(usize, *mut FieldView) -> u32) -> Vec<String> {
    (first..first + count)
        .map(|i| {
            let mut field = MaybeUninit::uninit();
            assert_eq!(get(i, field.as_mut_ptr()), 1);
            field.assume_init().text.read().unwrap()
        })
        .collect()
}

/// The same from a snapshot, read through the C calls.
unsafe fn from_snapshot(h: *mut WheelhouseCore) -> Followed {
    let s = wheelhouse_snapshot_acquire(h, ptr::null_mut());
    let mut followed = Followed::default();
    let mut placed: Vec<(String, String)> = vec![];
    for i in 0..wheelhouse_snapshot_node_count(s) {
        let n = node(s, i);
        let fields = texts(n.first_field, n.field_count, |i, out| wheelhouse_snapshot_field(s, i, out));
        let at = if n.is_section != 0 {
            let name = n.label.read().unwrap();
            followed.sections.push(name.clone());
            (name, String::new())
        } else {
            let parent = placed[n.parent].clone();
            let key = n.key.read().unwrap();
            followed.children.entry(parent.clone()).or_default().push(key.clone());
            (parent.0, key)
        };
        followed.nodes.insert(at.clone(), (n.label.read().unwrap(), n.state, n.collapsed, fields));
        placed.push(at);
    }
    wheelhouse_snapshot_release(s);
    followed
}

impl Followed {
    /// Apply a delta through the C calls; returns the intents it lists.
    unsafe fn apply(&mut self, d: *const WheelhouseDelta) -> Vec<u64> {
        let list = |i: usize| {
            let mut op = MaybeUninit::uninit();
            assert_eq!(wheelhouse_delta_list_op(d, i, op.as_mut_ptr()), 1);
            op.assume_init()
        };
        let apply_list = |items: &mut Vec<String>, first: usize, count: usize| {
            for i in first..first + count {
                let op = list(i);
                match op.kind {
                    WHEELHOUSE_LIST_INSERT => items.insert(op.at, op.key.read().unwrap()),
                    WHEELHOUSE_LIST_REMOVE => drop(items.remove(op.at)),
                    WHEELHOUSE_LIST_MOVE => {
                        MOVES.with(|moves| moves.set(moves.get() + 1));
                        let item = items.remove(op.from);
                        items.insert(op.to, item);
                    }
                    other => panic!("list op {other}"),
                }
            }
        };
        for i in 0..wheelhouse_delta_op_count(d) {
            let mut op = MaybeUninit::uninit();
            assert_eq!(wheelhouse_delta_op(d, i, op.as_mut_ptr()), 1);
            let op = op.assume_init();
            let at = (op.section.read().unwrap(), op.key.read().unwrap());
            match op.kind {
                WHEELHOUSE_OP_SECTION | WHEELHOUSE_OP_UPSERT => {
                    let mut n = MaybeUninit::uninit();
                    assert_eq!(wheelhouse_delta_node(d, op.first, n.as_mut_ptr()), 1);
                    let n = n.assume_init();
                    assert_eq!(n.is_section, (op.kind == WHEELHOUSE_OP_SECTION) as u32);
                    let fields = texts(n.first_field, n.field_count, |i, out| wheelhouse_delta_field(d, i, out));
                    self.nodes.insert(at, (n.label.read().unwrap(), n.state, n.collapsed, fields));
                }
                WHEELHOUSE_OP_REMOVE_SECTION | WHEELHOUSE_OP_REMOVE => {
                    self.nodes.remove(&at);
                    self.children.remove(&at);
                }
                WHEELHOUSE_OP_SECTIONS => apply_list(&mut self.sections, op.first, op.count),
                WHEELHOUSE_OP_CHILDREN => {
                    let items = self.children.entry(at.clone()).or_default();
                    apply_list(items, op.first, op.count);
                    if items.is_empty() {
                        self.children.remove(&at);
                    }
                }
                WHEELHOUSE_OP_DISPLAY_VARIABLES | WHEELHOUSE_OP_DIAGNOSTICS | WHEELHOUSE_OP_ERRORS => {}
                other => panic!("op {other}"),
            }
        }
        (0..wheelhouse_delta_intent_count(d))
            .map(|i| {
                let mut request = 0;
                assert_eq!(wheelhouse_delta_intent(d, i, &mut request), 1);
                request
            })
            .collect()
    }
}

/// A host following the stream through the C calls has what the snapshot
/// presents after each change, and the intents it applies are listed.
#[test]
fn a_host_following_the_stream_has_what_the_snapshot_presents() {
    unsafe {
        let h = create(include_str!("../../../data/sidebar/daily-driver.kdl"));
        let mut error = ptr::null_mut();
        let mut subscription = 0;
        assert_eq!(wheelhouse_subscribe(h, &mut subscription, &mut error), 1);
        for line in include_str!("../../../data/sidebar/fixture.jsonl").lines() {
            assert_eq!(wheelhouse_apply_patch_json(h, 0, text(PROVIDER), text(line), &mut error), 1);
        }
        let mut followed = Followed::default();
        let follow = |followed: &mut Followed| {
            if wheelhouse_changed_since(h, subscription, ptr::null_mut()) == 0 {
                return vec![];
            }
            let d = wheelhouse_poll(h, subscription, ptr::null_mut());
            assert!(!d.is_null());
            let intents = followed.apply(d);
            wheelhouse_delta_release(d);
            assert_eq!(*followed, from_snapshot(h));
            intents
        };
        follow(&mut followed);
        assert!(followed.nodes.len() > 4, "{followed:?}");

        // Collapse and expand nodes by the keys the stream gave.
        let keys: Vec<String> = followed.children.values().flatten().take(4).cloned().collect();
        for key in &keys {
            let intent = IntentView {
                kind: WHEELHOUSE_INTENT_TOGGLE_PLACEMENT,
                key: text(key),
                entity: EntityView::empty(),
                name: text(""),
                has_value: 0,
                value: text(""),
            };
            let mut request = 0;
            assert_eq!(wheelhouse_apply_intent(h, &intent, &mut request, &mut error), WHEELHOUSE_INTENT_ACCEPTED);
            assert!(error.is_null());
            assert_eq!(follow(&mut followed), [request]);
        }

        // Activating one opens it, and its effect is queued.
        let intent = IntentView {
            kind: WHEELHOUSE_INTENT_ACTIVATE_PLACEMENT,
            key: text(&keys[0]),
            entity: EntityView::empty(),
            name: text(""),
            has_value: 0,
            value: text(""),
        };
        let mut request = 0;
        assert_eq!(wheelhouse_apply_intent(h, &intent, &mut request, &mut error), WHEELHOUSE_INTENT_ACCEPTED);
        assert_eq!(follow(&mut followed), [request]);
        let effects = wheelhouse_effects_take(h, ptr::null_mut());
        assert_eq!(wheelhouse_effects_count(effects), 1, "its effect is queued");
        wheelhouse_effects_release(effects);

        // A key no node has is refused; one that isn't a key is invalid.
        let mut intent = IntentView { key: text("5:other1:x1:y1:z"), ..intent };
        assert_eq!(wheelhouse_apply_intent(h, &intent, &mut request, &mut error), WHEELHOUSE_INTENT_REFUSED);
        assert!(CStr::from_ptr(error).to_string_lossy().contains("unknown placement"));
        wheelhouse_string_free(error);
        intent.key = text("9:short");
        assert_eq!(wheelhouse_apply_intent(h, &intent, &mut request, &mut error), WHEELHOUSE_INTENT_INVALID);
        wheelhouse_string_free(error);
        error = ptr::null_mut();
        assert_eq!(wheelhouse_changed_since(h, subscription, ptr::null_mut()), 0, "neither changed anything");

        // A fact change arrives as a delta too.
        let rename = r#"{"target":{"kind":"entity","value":{"kind":"vessel","id":"chip-v"}},"source_id":"fixture","set":{"display.label":{"value":{"type":"text","value":"Renamed"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 1, text(PROVIDER), text(rename), &mut error), 1);
        follow(&mut followed);
        assert!(followed.nodes.values().any(|(label, ..)| label == "Renamed"));

        // Renames that sort a node to the end of its list move it.
        for (i, id) in ["v", "multi", "chip-v"].into_iter().enumerate() {
            let rename = format!(
                r#"{{"target":{{"kind":"entity","value":{{"kind":"vessel","id":"{id}"}}}},"source_id":"fixture","set":{{"display.label":{{"value":{{"type":"text","value":"~{i}"}}}}}},"unset":[]}}"#
            );
            assert_eq!(wheelhouse_apply_patch_json(h, 2, text(PROVIDER), text(&rename), &mut error), 1);
            follow(&mut followed);
        }
        assert!(MOVES.with(|moves| moves.get()) > 0, "a rename reordered a list");

        assert_eq!(wheelhouse_unsubscribe(h, subscription, ptr::null_mut()), 1);
        assert!(wheelhouse_poll(h, subscription, &mut error).is_null());
        assert!(!error.is_null(), "polling an unsubscribed ID is an error");
        wheelhouse_string_free(error);
        wheelhouse_destroy(h);
    }
}

/// An unknown subscription, unsubscribed or never given, is an error from
/// wheelhouse_changed_since and wheelhouse_poll that names it, where a quiet
/// live one has none; and it doesn't poison the handle.
#[test]
fn an_unknown_subscription_is_an_error_not_quiet() {
    unsafe {
        let h = create(include_str!("../../../data/sidebar/daily-driver.kdl"));
        let mut error = ptr::null_mut();
        let (mut live, mut gone) = (0, 0);
        assert_eq!(wheelhouse_subscribe(h, &mut live, &mut error), 1);
        assert_eq!(wheelhouse_subscribe(h, &mut gone, &mut error), 1);
        assert_eq!(wheelhouse_unsubscribe(h, gone, &mut error), 1);
        assert_eq!(wheelhouse_unsubscribe(h, gone, &mut error), 0);
        assert!(error.is_null(), "unsubscribing an unknown ID is quiet");

        let d = wheelhouse_poll(h, live, &mut error);
        assert!(!d.is_null() && error.is_null(), "the first poll gives everything");
        wheelhouse_delta_release(d);
        assert_eq!(wheelhouse_changed_since(h, live, &mut error), 0);
        assert!(error.is_null(), "a quiet live subscription has no error");
        assert!(wheelhouse_poll(h, live, &mut error).is_null());
        assert!(error.is_null(), "a quiet live subscription has no error");

        let never = live.max(gone) + 100;
        for unknown in [gone, never] {
            let names = |error: *mut c_char| {
                assert!(!error.is_null(), "subscription {unknown} gave no error");
                let message = CStr::from_ptr(error).to_string_lossy().into_owned();
                wheelhouse_string_free(error);
                assert!(message.contains(&format!("subscription {unknown}")), "{message}");
            };
            assert_eq!(wheelhouse_changed_since(h, unknown, &mut error), 0);
            names(error);
            assert!(wheelhouse_poll(h, unknown, &mut error).is_null());
            names(error);
        }

        let mut again = 0;
        assert_eq!(wheelhouse_subscribe(h, &mut again, &mut error), 1, "the handle isn't poisoned");
        assert!(error.is_null() && again != gone && again != live);
        let d = wheelhouse_poll(h, again, &mut error);
        assert!(!d.is_null() && error.is_null());
        wheelhouse_delta_release(d);
        wheelhouse_destroy(h);
    }
}

unsafe fn effect_kinds(h: *mut WheelhouseCore) -> Vec<(u32, String)> {
    let effects = wheelhouse_effects_take(h, ptr::null_mut());
    let kinds = (0..wheelhouse_effects_count(effects))
        .map(|i| {
            let mut e = MaybeUninit::uninit();
            assert_eq!(wheelhouse_effects_get(effects, i, e.as_mut_ptr()), 1);
            let e = e.assume_init();
            (e.kind, e.entity.id.read().unwrap())
        })
        .collect();
    wheelhouse_effects_release(effects);
    kinds
}

/// Every catalog entity has its card, which answers what the core's catalog
/// card of it says, and its actions are intents that do what the core's
/// actions on the entity do.
#[test]
fn every_catalog_entity_has_its_card() {
    unsafe {
        let daily = include_str!("../../../data/sidebar/daily-driver.kdl");
        let fixture = include_str!("../../../data/sidebar/fixture.jsonl");
        // (config, facts, whether their provider has gone stale)
        let setups = [(daily, fixture, false), ("", include_str!("../../../fixtures/typed-detail.jsonl"), false), (daily, fixture, true)];
        let mut checked = 0;
        for (config, facts, stale) in setups {
            // Two cores with the same facts: one dispatches the core's
            // actions, the other applies the card's intents.
            let cores = [create(config), create(config)];
            for h in cores {
                for line in facts.lines() {
                    assert_eq!(wheelhouse_apply_patch_json(h, 5000, text(PROVIDER), text(line), ptr::null_mut()), 1);
                }
                assert_eq!(wheelhouse_provider_set_stale(h, text(PROVIDER), stale as u32, ptr::null_mut()), 1);
            }
            let [h, other] = cores;
            let (now_ms, cards) = (*h).sidebar.detail_cards();
            for d in &cards {
                let entity = d.entity.clone();
                let card = wheelhouse_card_acquire(h, EntityView::of(&entity), ptr::null_mut());
                assert!(!card.is_null(), "{entity:?} has a card");
                assert_eq!(wheelhouse_card_is_current(h, card, ptr::null_mut()), 1);
                let mut c = MaybeUninit::uninit();
                assert_eq!(wheelhouse_card(card, c.as_mut_ptr()), 1);
                let c = c.assume_init();
                assert_eq!(c.entity.read().unwrap(), entity);
                assert_eq!(
                    (c.label.read().unwrap(), c.field_count, c.error.read().unwrap(), c.has_workspace, c.workspace_id.bytes),
                    (
                        d.label.clone(),
                        d.fields.len(),
                        d.error.clone().unwrap_or_default(),
                        d.workspace_id.is_some() as u32,
                        workspace_view(d.workspace_id).bytes
                    )
                );
                assert_eq!((c.opening, c.now_ms), (d.opening as u32, now_ms));
                assert_eq!(c.has_url, (*h).sidebar.subject_url(&entity).is_some() as u32);
                let provider_stale = (*h).sidebar.provider_is_stale(&entity.provider);
                for (f, field) in d.fields.iter().enumerate() {
                    let mut cf = MaybeUninit::uninit();
                    assert_eq!(wheelhouse_card_field(card, f, cf.as_mut_ptr()), 1);
                    let cf = cf.assume_init();
                    let view = |v: &DetailFieldView| {
                        (
                            (v.name.read(), v.section.read(), v.role, v.label.read(), v.has_value, v.text.read()),
                            (v.source_key.read(), v.source_id.read(), v.has_observation, v.observed_at_ms, v.stale, v.relation_count),
                        )
                    };
                    assert_eq!(view(&cf), view(&detail_field_view(field, provider_stale)));
                    for (r, relation) in field.relations.iter().enumerate() {
                        let mut cr = MaybeUninit::uninit();
                        assert_eq!(wheelhouse_card_relation(card, f, r, ptr::null(), 0, cr.as_mut_ptr()), 1);
                        let cr = cr.assume_init();
                        let target = cr.entity.read().unwrap();
                        assert_eq!(&target, relation);
                        assert_eq!(cr.display_text.read().unwrap(), target.id);
                        // A relation already on the path is left out.
                        assert_eq!(wheelhouse_card_relation(card, f, r, &cr.entity, 1, cr_out()), 0);
                    }
                    assert_eq!(wheelhouse_card_relation(card, f, field.relations.len(), ptr::null(), 0, cr_out()), 0);
                }
                assert_eq!(wheelhouse_card_field(card, d.fields.len(), MaybeUninit::uninit().as_mut_ptr()), 0);
                for a in 0..3 {
                    let mut ca = MaybeUninit::uninit();
                    let has = wheelhouse_card_action(card, a, ca.as_mut_ptr());
                    assert_eq!(has, [1, c.has_url, 0][a], "{entity:?} action {a}");
                    if has == 0 {
                        continue;
                    }
                    let ca = ca.assume_init();
                    if a == 0 {
                        assert_eq!(
                            (ca.intent.read().unwrap(), ca.label.read().unwrap()),
                            (d.primary_intent.as_str().to_owned(), d.primary_label.clone())
                        );
                    }
                    assert_eq!(ca.entity.read().unwrap(), entity);
                    assert_eq!(ca.kind, [WHEELHOUSE_INTENT_ACTIVATE, WHEELHOUSE_INTENT_COPY_SUBJECT_URL][a]);
                    // The core's action on one core, the card's intent on the other.
                    let action =
                        [Action::Activate { entity: entity.clone() }, Action::CopySubjectUrl { entity: entity.clone() }][a].clone();
                    let dispatched = (*other).sidebar.dispatch(action).map(|effects| (*other).effects.extend(effects));
                    let intent =
                        IntentView { kind: ca.kind, key: text(""), entity: ca.entity, name: text(""), has_value: 0, value: text("") };
                    let mut error = ptr::null_mut();
                    let applied = wheelhouse_apply_intent(h, &intent, ptr::null_mut(), &mut error);
                    wheelhouse_string_free(error);
                    assert_eq!(applied == WHEELHOUSE_INTENT_ACCEPTED, dispatched.is_ok(), "{entity:?} action {a}");
                    assert_eq!(effect_kinds(h), effect_kinds(other), "{entity:?} action {a}");
                    checked += 1;
                }
                wheelhouse_card_release(card);
            }
            // An entity the catalog doesn't have has no card, and no error.
            let mut error = ptr::null_mut();
            let missing = EntityView { provider: text(PROVIDER), kind: text("vessel"), id: text("nowhere") };
            assert!(wheelhouse_card_acquire(h, missing, &mut error).is_null());
            assert!(error.is_null());
            wheelhouse_destroy(h);
            wheelhouse_destroy(other);
        }
        assert!(checked > 10, "{checked} actions checked");
    }
}

fn cr_out() -> *mut CardRelationView {
    Box::leak(Box::new(CardRelationView { entity: EntityView::empty(), display_text: Text::borrowed("") }))
}

/// A card is current until what the core presents changes, and only for
/// the core that gave it.
#[test]
fn a_card_is_current_until_the_core_changes() {
    unsafe {
        let config = include_str!("../../../data/sidebar/daily-driver.kdl");
        // Two cores with the same facts, so at the same revision.
        let (h, other) = (create(config), create(config));
        for core in [h, other] {
            for line in include_str!("../../../data/sidebar/fixture.jsonl").lines() {
                assert_eq!(wheelhouse_apply_patch_json(core, 0, text(PROVIDER), text(line), ptr::null_mut()), 1);
            }
        }
        let entity = EntityView { provider: text(PROVIDER), kind: text("vessel"), id: text("chip-v") };
        let card = wheelhouse_card_acquire(h, entity, ptr::null_mut());
        assert!(!card.is_null());
        assert_eq!(wheelhouse_card_is_current(h, card, ptr::null_mut()), 1);
        assert_eq!(wheelhouse_card_is_current(other, card, ptr::null_mut()), 0, "another core's card");
        assert_eq!(wheelhouse_card_is_current(h, ptr::null(), ptr::null_mut()), 0);
        let rename = r#"{"target":{"kind":"entity","value":{"kind":"vessel","id":"chip-v"}},"source_id":"fixture","set":{"display.label":{"value":{"type":"text","value":"Renamed"}}},"unset":[]}"#;
        assert_eq!(wheelhouse_apply_patch_json(h, 1, text(PROVIDER), text(rename), ptr::null_mut()), 1);
        assert_eq!(wheelhouse_card_is_current(h, card, ptr::null_mut()), 0);
        // The old card stays readable; a new one has the change.
        let mut c = MaybeUninit::uninit();
        assert_eq!(wheelhouse_card(card, c.as_mut_ptr()), 1);
        assert_eq!(c.assume_init().label.read().unwrap(), "Subject workspace");
        let renamed = wheelhouse_card_acquire(h, entity, ptr::null_mut());
        let mut c = MaybeUninit::uninit();
        assert_eq!(wheelhouse_card(renamed, c.as_mut_ptr()), 1);
        assert_eq!(c.assume_init().label.read().unwrap(), "Renamed");
        wheelhouse_card_release(card);
        wheelhouse_card_release(renamed);
        wheelhouse_destroy(h);
        wheelhouse_destroy(other);
    }
}

// ---------------------------------------------------------------------------
// Arrangement edits and intents through the ABI (#516): a document and each
// edit and intent kind cross and come back, and the result is the core's
// own `apply`. Tabs are numbers, carried in a tab's first eight bytes.
mod arrangement_abi {
    use super::super::*;
    use hegel::generators as gs;
    use hegel::TestCase;
    use std::ffi::c_void;
    use wheelhouse_core::arrangement::generate::{draw_dir, draw_panel, draw_tab, edit_step, tabs, Step};
    use wheelhouse_core::arrangement::{apply, apply_intent, Arrangement, Dir, Edit, Intent, Outcome, Owner, OwnerKind, Panel, PanelId};
    use wheelhouse_core::WorkspaceId;

    type Tab = u64;

    fn tab(t: Tab) -> WheelhouseTab {
        let mut bytes = [0; WHEELHOUSE_TAB_BYTES];
        bytes[..8].copy_from_slice(&t.to_le_bytes());
        // The rest is the host's: the core must carry it as it is.
        bytes[WHEELHOUSE_TAB_BYTES - 1] = 0xa5;
        WheelhouseTab { bytes }
    }

    fn number(t: &WheelhouseTab) -> Tab {
        assert_eq!(t.bytes[WHEELHOUSE_TAB_BYTES - 1], 0xa5, "a tab's bytes cross whole");
        u64::from_le_bytes(t.bytes[..8].try_into().expect("eight bytes"))
    }

    fn map<A: Clone + PartialEq, B: Clone + PartialEq>(a: &Arrangement<A>, f: &impl Fn(&A) -> B) -> Arrangement<B> {
        fn panel<A, B>(p: &Panel<A>, f: &impl Fn(&A) -> B) -> Panel<B> {
            Panel {
                id: p.id,
                weight: p.weight,
                children: p.children.iter().map(|c| panel(c, f)).collect(),
                tabs: p.tabs.iter().map(f).collect(),
                selected: p.selected.as_ref().map(f),
                named: p.named.clone(),
                options_from: p.options_from,
                restore_emptied: p.restore_emptied,
            }
        }
        Arrangement {
            root: a.root.as_ref().map(|r| panel(r, f)),
            root_axis: a.root_axis,
            next_id: a.next_id,
            removed: a.removed.iter().map(f).collect(),
        }
    }

    fn abi_dir(d: Option<Dir>) -> u32 {
        match d {
            None => WHEELHOUSE_DIR_NONE,
            Some(Dir::Left) => WHEELHOUSE_DIR_LEFT,
            Some(Dir::Up) => WHEELHOUSE_DIR_UP,
            Some(Dir::Right) => WHEELHOUSE_DIR_RIGHT,
            Some(Dir::Down) => WHEELHOUSE_DIR_DOWN,
        }
    }

    fn blank_edit(kind: u32) -> WheelhouseEdit {
        let none = tab(0);
        WheelhouseEdit {
            kind,
            dir: WHEELHOUSE_DIR_NONE,
            panel: 0,
            after_panel: 0,
            has_tab: 0,
            tab: none,
            has_after: 0,
            after: none,
            to: none,
            weight: 0.0,
            delta: 0.0,
            floor: 0.0,
            select: 0,
            may_close: 0,
        }
    }

    fn abi_edit(edit: &Edit<Tab>) -> WheelhouseEdit {
        let with_tab = |e: &mut WheelhouseEdit, t: Option<&Tab>| {
            if let Some(t) = t {
                (e.has_tab, e.tab) = (1, tab(*t));
            }
        };
        let with_after = |e: &mut WheelhouseEdit, t: Option<&Tab>| {
            if let Some(t) = t {
                (e.has_after, e.after) = (1, tab(*t));
            }
        };
        match edit {
            Edit::Clear => blank_edit(WHEELHOUSE_EDIT_CLEAR),
            Edit::Discard => blank_edit(WHEELHOUSE_EDIT_DISCARD),
            Edit::Split { panel, dir } => WheelhouseEdit { panel: panel.0, dir: abi_dir(Some(*dir)), ..blank_edit(WHEELHOUSE_EDIT_SPLIT) },
            Edit::Add { parent, weight } => {
                WheelhouseEdit { panel: parent.map_or(0, |p| p.0), weight: *weight, ..blank_edit(WHEELHOUSE_EDIT_ADD) }
            }
            Edit::Remove { panel } => WheelhouseEdit { panel: panel.0, ..blank_edit(WHEELHOUSE_EDIT_REMOVE) },
            Edit::Close { panel } => WheelhouseEdit { panel: panel.0, ..blank_edit(WHEELHOUSE_EDIT_CLOSE) },
            Edit::InsertTab { tab: t, to, after, select } => {
                let mut e = WheelhouseEdit { panel: to.0, select: *select as u32, ..blank_edit(WHEELHOUSE_EDIT_INSERT_TAB) };
                with_tab(&mut e, Some(t));
                with_after(&mut e, after.as_ref());
                e
            }
            Edit::DetachTab { tab: t } => {
                let mut e = blank_edit(WHEELHOUSE_EDIT_DETACH_TAB);
                with_tab(&mut e, Some(t));
                e
            }
            Edit::RemoveTab { tab: t } => {
                let mut e = blank_edit(WHEELHOUSE_EDIT_REMOVE_TAB);
                with_tab(&mut e, Some(t));
                e
            }
            Edit::Drop { tab: t, to, dir, after } => {
                let mut e = WheelhouseEdit { panel: to.0, dir: abi_dir(*dir), ..blank_edit(WHEELHOUSE_EDIT_DROP) };
                with_tab(&mut e, t.as_ref());
                with_after(&mut e, after.as_ref());
                e
            }
            Edit::Settle { panel, dir, may_close } => {
                WheelhouseEdit { panel: panel.0, dir: abi_dir(*dir), may_close: *may_close as u32, ..blank_edit(WHEELHOUSE_EDIT_SETTLE) }
            }
            Edit::Select { panel, tab: t } => {
                let mut e = WheelhouseEdit { panel: panel.0, ..blank_edit(WHEELHOUSE_EDIT_SELECT) };
                with_tab(&mut e, t.as_ref());
                e
            }
            Edit::Reorder { panel, after } => {
                WheelhouseEdit { panel: panel.0, after_panel: after.map_or(0, |p| p.0), ..blank_edit(WHEELHOUSE_EDIT_REORDER) }
            }
            Edit::Resize { panel, delta, floor } => {
                WheelhouseEdit { panel: panel.0, delta: *delta, floor: *floor, ..blank_edit(WHEELHOUSE_EDIT_RESIZE) }
            }
            Edit::Equalize { panel } => WheelhouseEdit { panel: panel.0, ..blank_edit(WHEELHOUSE_EDIT_EQUALIZE) },
            Edit::Readdress { from, to } => {
                let mut e = WheelhouseEdit { to: tab(*to), ..blank_edit(WHEELHOUSE_EDIT_READDRESS) };
                with_tab(&mut e, Some(from));
                e
            }
        }
    }

    /// The hidden rule's context: the hidden tabs.
    unsafe extern "C" fn hide(context: *mut c_void, t: *const WheelhouseTab) -> u32 {
        let hidden = &*(context as *const Vec<Tab>);
        hidden.contains(&number(&*t)) as u32
    }

    fn outcome(o: &WheelhouseEditOutcome) -> Outcome<Tab> {
        let id = |raw: u64| (raw != 0).then_some(PanelId(raw));
        Outcome {
            applied: o.applied != 0,
            panel: id(o.panel),
            source: id(o.source),
            heir: id(o.heir),
            removed: unsafe { slice(o.removed, o.removed_count) }.expect("removed").iter().map(number).collect(),
        }
    }

    unsafe fn read(doc: &WheelhouseEditDoc) -> Arrangement<Tab> {
        map(&arrangement::read_edit_doc(doc).expect("a document the core wrote reads back"), &number)
    }

    #[hegel::test(test_cases = 300)]
    fn every_edit_crosses_the_abi_and_matches_the_core(tc: TestCase) {
        let (mut a, _, next_tab) = wheelhouse_core::arrangement::generate::build(&tc, 20);
        // A dock restore's marks cross too.
        for id in wheelhouse_core::arrangement::generate::panel_ids(&a) {
            a.panel_mut(id).expect("there").restore_emptied = tc.draw(gs::booleans());
        }
        for _ in 0..tc.draw(gs::integers::<usize>().max_value(8)) {
            let Step::Edit { edit, hidden } = edit_step(&tc, &a, next_tab + 1) else { unreachable!("edit steps are edits") };
            let doc = arrangement::EditDoc::of(map(&a, &|t: &Tab| tab(*t)));
            unsafe {
                assert_eq!(read(&doc.doc()), a, "a document crosses and comes back");
                let mut error = ptr::null_mut();
                let rule: WheelhouseTabRule = Some(hide);
                let result =
                    wheelhouse_arrangement_apply(&doc.doc(), &abi_edit(&edit), rule, &hidden as *const _ as *mut c_void, &mut error);
                assert!(!result.is_null(), "{edit:?}");
                let expected = apply(&mut a, &edit, &|t| hidden.contains(t));
                assert_eq!(read(&(*result).doc), a, "{edit:?}");
                assert_eq!(outcome(&(*result).outcome), expected, "{edit:?}");
                assert_eq!((*result).has_source, 0);
                wheelhouse_edit_result_free(result);
            }
        }
    }

    fn blank_intent(kind: u32, owner: Owner) -> WheelhouseArrangementIntent {
        WheelhouseArrangementIntent {
            kind,
            owner_kind: if owner.kind == OwnerKind::Sidebar { WHEELHOUSE_OWNER_SIDEBAR } else { WHEELHOUSE_OWNER_WORKSPACE },
            owner: owner.id.into(),
            dir: WHEELHOUSE_DIR_NONE,
            panel: 0,
            after_panel: 0,
            has_tab: 0,
            tab: tab(0),
            has_after: 0,
            after: tab(0),
            delta: 0.0,
            floor: 0.0,
        }
    }

    fn abi_intent(intent: &Intent<Tab>) -> WheelhouseArrangementIntent {
        let b = |kind| blank_intent(kind, intent.owner());
        let tabbed = |kind, p: PanelId, t: Option<&Tab>, after: Option<&Tab>, d: Option<Dir>| {
            let mut i = WheelhouseArrangementIntent { panel: p.0, dir: abi_dir(d), ..b(kind) };
            if let Some(t) = t {
                (i.has_tab, i.tab) = (1, tab(*t));
            }
            if let Some(t) = after {
                (i.has_after, i.after) = (1, tab(*t));
            }
            i
        };
        match intent {
            Intent::SelectTab { panel, tab: t, .. } => tabbed(WHEELHOUSE_INTENT_SELECT_TAB, *panel, t.as_ref(), None, None),
            Intent::MoveTab { tab: t, to, after, .. } => tabbed(WHEELHOUSE_INTENT_MOVE_TAB, *to, Some(t), after.as_ref(), None),
            Intent::CloseTab { tab: t, .. } => tabbed(WHEELHOUSE_INTENT_CLOSE_TAB, PanelId(0), Some(t), None, None),
            Intent::DropTab { tab: t, to, dir, after, .. } => tabbed(WHEELHOUSE_INTENT_DROP_TAB, *to, Some(t), after.as_ref(), *dir),
            Intent::SplitPanel { panel, dir, .. } => tabbed(WHEELHOUSE_INTENT_SPLIT_PANEL, *panel, None, None, Some(*dir)),
            Intent::ClosePanel { panel, .. } => tabbed(WHEELHOUSE_INTENT_CLOSE_PANEL, *panel, None, None, None),
            Intent::ReorderPanel { panel, after, .. } => {
                WheelhouseArrangementIntent { panel: panel.0, after_panel: after.map_or(0, |p| p.0), ..b(WHEELHOUSE_INTENT_REORDER_PANEL) }
            }
            Intent::ResizePanel { panel, delta, floor, .. } => {
                WheelhouseArrangementIntent { panel: panel.0, delta: *delta, floor: *floor, ..b(WHEELHOUSE_INTENT_RESIZE_PANEL) }
            }
            Intent::EqualizePanel { panel, .. } => tabbed(WHEELHOUSE_INTENT_EQUALIZE_PANEL, *panel, None, None, None),
        }
    }

    /// Every intent kind, owners of both kinds, and drops from a second
    /// arrangement.
    #[hegel::test(test_cases = 300)]
    fn every_intent_crosses_the_abi_and_matches_the_core(tc: TestCase) {
        let (mut a, _, next_tab) = wheelhouse_core::arrangement::generate::build(&tc, 16);
        let (b, _, _) = wheelhouse_core::arrangement::generate::build(&tc, 10);
        // The second arrangement's tabs are its own.
        let mut b = map(&b, &|t: &Tab| t + 1000);
        let owner = Owner {
            kind: if tc.draw(gs::booleans()) { OwnerKind::Sidebar } else { OwnerKind::Workspace },
            id: WorkspaceId::from_bytes(tc.draw(gs::integers::<u128>()).to_le_bytes()),
        };
        let p = draw_panel(&tc, &a);
        let t = draw_tab(&tc, &a, next_tab);
        let after = tc.draw(gs::booleans()).then(|| draw_tab(&tc, &a, next_tab));
        let from_b = tc.draw(gs::booleans()) && !tabs(&b).is_empty();
        let intent = match tc.draw(gs::integers::<u8>().max_value(8)) {
            0 => Intent::SelectTab { owner, panel: p, tab: after },
            1 => Intent::MoveTab { owner, tab: t, to: p, after },
            2 => Intent::CloseTab { owner, tab: t },
            3 => {
                let t = if from_b { tc.draw(gs::sampled_from(tabs(&b))) } else { t };
                Intent::DropTab { owner, tab: t, to: p, dir: tc.draw(gs::booleans()).then(|| draw_dir(&tc)), after }
            }
            4 => Intent::SplitPanel { owner, panel: p, dir: draw_dir(&tc) },
            5 => Intent::ClosePanel { owner, panel: p },
            6 => Intent::ReorderPanel { owner, panel: p, after: tc.draw(gs::booleans()).then(|| draw_panel(&tc, &a)) },
            7 => Intent::ResizePanel {
                owner,
                panel: p,
                delta: tc.draw(gs::floats::<f32>().min_value(-1.0).max_value(1.0).allow_nan(false).allow_infinity(false)),
                floor: 0.05,
            },
            _ => Intent::EqualizePanel { owner, panel: p },
        };
        let hidden: Vec<Tab> = vec![];
        let doc = arrangement::EditDoc::of(map(&a, &|t: &Tab| tab(*t)));
        let source_doc = arrangement::EditDoc::of(map(&b, &|t: &Tab| tab(*t)));
        let with_source = matches!(intent, Intent::DropTab { .. }) && from_b;
        unsafe {
            let mut error = ptr::null_mut();
            let source_view = source_doc.doc();
            let result = wheelhouse_arrangement_apply_intent(
                &doc.doc(),
                &abi_intent(&intent),
                if with_source { &source_view } else { ptr::null() },
                None,
                ptr::null_mut(),
                &mut error,
            );
            assert!(!result.is_null(), "{intent:?}");
            let expected = apply_intent(&mut a, &intent, with_source.then_some(&mut b), &|t| hidden.contains(t));
            assert_eq!(read(&(*result).doc), a, "{intent:?}");
            assert_eq!(outcome(&(*result).outcome), expected.outcome, "{intent:?}");
            assert_eq!((*result).has_source != 0, expected.source.is_some(), "{intent:?}");
            if let Some(source) = expected.source {
                assert_eq!(read(&(*result).source_doc), b, "{intent:?}");
                assert_eq!(outcome(&(*result).source_outcome), source, "{intent:?}");
            }
            wheelhouse_edit_result_free(result);
            let name = wheelhouse_arrangement_intent_name(abi_intent(&intent).kind).read().expect("a name");
            assert_eq!(name, intent.name());
        }
    }

    /// A malformed document is an error, not a guess.
    #[test]
    fn a_malformed_document_is_refused() {
        unsafe {
            let a = map(
                &{
                    let mut a: Arrangement<Tab> = Arrangement::new();
                    apply(&mut a, &Edit::Clear, &|_| false);
                    apply(&mut a, &Edit::Split { panel: PanelId(1), dir: Dir::Left }, &|_| false);
                    a
                },
                &|t: &Tab| tab(*t),
            );
            let doc = arrangement::EditDoc::of(a);
            let mut view = doc.doc();
            let mut panels: Vec<WheelhouseEditPanel> = slice(view.panels, view.panel_count).expect("panels").to_vec();
            panels[1].parent = 2;
            view.panels = panels.as_ptr();
            let mut error = ptr::null_mut();
            let result = wheelhouse_arrangement_apply(&view, &blank_edit(WHEELHOUSE_EDIT_CLEAR), None, ptr::null_mut(), &mut error);
            assert!(result.is_null() && !error.is_null());
            wheelhouse_string_free(error);
        }
    }
}

// A window's overview through C: the workspaces the sidebar shows live are
// its tiles; a new current workspace is a flight the host draws through the
// scene until it settles, and without motion a cut; the overview opens and
// goes back a level at a time; bad input is an error.
#[test]
fn the_overview_reaches_c_and_flies_between_workspaces() {
    unsafe {
        let mut error = ptr::null_mut();
        let h = create("");
        let (first, second) = (WorkspaceId::from_bytes([1; 16]), WorkspaceId::from_bytes([2; 16]));
        let workspaces = [
            WorkspaceInput { id: first.into(), position: 0, name: text("one"), selected: 1 },
            WorkspaceInput { id: second.into(), position: 1, name: text("two"), selected: 0 },
        ];
        assert_eq!(wheelhouse_observe(h, workspaces.as_ptr(), 2, ptr::null(), 0, &mut error), 1);
        let frame = |now: f64| {
            let frame = wheelhouse_overview_frame(h, now, &mut ptr::null_mut());
            let f = &*frame;
            let tiles: Vec<(String, u32)> =
                slice(f.tiles, f.tile_count).unwrap().iter().map(|t| (t.title.read().unwrap(), t.current)).collect();
            let rect = slice(f.tiles, f.tile_count).unwrap().iter().find(|t| t.current == 1).map(|t| t.rect);
            let fits = rect.is_some_and(|r| (r.x0 - f.view.x0).abs() < 1e-9 && (r.y1 - f.view.y1).abs() < 1e-9);
            let headings: Vec<String> = slice(f.groups, f.group_count).unwrap().iter().map(|g| g.heading.read().unwrap()).collect();
            let summary = (f.mode, f.flying, f.settled, f.has_view, fits, tiles, headings);
            wheelhouse_overview_frame_free(frame);
            summary
        };
        let two = |current: u32| vec![("one".to_owned(), 1 - current), ("two".to_owned(), current)];
        let other = vec![wheelhouse_core::overview::OTHER_WORKSPACES.to_owned()];
        // Before any update there is no camera: draw the workspace as itself.
        assert_eq!(frame(0.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 0, 0, 0, false, vec![], vec![]));
        assert_eq!(wheelhouse_overview_update(h, first.into(), 1.6, 0.0, 1, &mut error), 1);
        assert_eq!(frame(0.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 0, 1, 1, true, two(0), other.clone()));
        // Switching flies, then settles on the new workspace.
        assert_eq!(wheelhouse_overview_update(h, second.into(), 1.6, 1000.0, 1, &mut error), 1);
        assert_eq!(frame(1000.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 1, 0, 1, false, two(1), other.clone()));
        assert_eq!(frame(2000.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 0, 1, 1, true, two(1), other.clone()));
        // Without motion a switch is a cut.
        assert_eq!(wheelhouse_overview_update(h, first.into(), 1.6, 3000.0, 0, &mut error), 1);
        assert_eq!(frame(3000.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 0, 1, 1, true, two(0), other.clone()));
        // Opening the overview, then going back a level at a time.
        assert_eq!(wheelhouse_overview_update(h, first.into(), 1.6, 4000.0, 1, &mut error), 1);
        assert_eq!(wheelhouse_overview_open(h, 4000.0, &mut error), 1);
        let opened = frame(4000.0);
        assert_eq!((opened.0, opened.1, opened.2), (WHEELHOUSE_OVERVIEW_OVERVIEW, 1, 0));
        assert_eq!(wheelhouse_overview_zoom_group(h, 0, 5000.0, &mut error), 1);
        assert_eq!(frame(6000.0).0, WHEELHOUSE_OVERVIEW_GROUP);
        assert_eq!(wheelhouse_overview_back(h, 6000.0, &mut error), 1);
        assert_eq!(frame(7000.0).0, WHEELHOUSE_OVERVIEW_OVERVIEW);
        // Picking a tile flies into it; the host's next update confirms it.
        assert_eq!(wheelhouse_overview_pick(h, second.into(), 7000.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, second.into(), 1.6, 7016.0, 1, &mut error), 1);
        assert_eq!(frame(7016.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 1, 0, 1, false, two(1), other.clone()));
        assert_eq!(frame(9000.0), (WHEELHOUSE_OVERVIEW_WORKSPACE, 0, 1, 1, true, two(1), other.clone()));
        assert_eq!(wheelhouse_overview_back(h, 9000.0, &mut error), 0);
        assert!(error.is_null());
        // A workspace leaving the sidebar leaves the scene.
        assert_eq!(wheelhouse_observe(h, workspaces.as_ptr(), 1, ptr::null(), 0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, first.into(), 1.6, 9500.0, 0, &mut error), 1);
        assert_eq!(frame(9500.0).5, vec![("one".to_owned(), 1)]);
        // A new view mode is drawn moving, and without motion cuts.
        assert_eq!(wheelhouse_overview_set_view_mode(h, WHEELHOUSE_OVERVIEW_GRID, 9500.0, &mut error), 1);
        assert_eq!(frame(9500.0).1, 0);
        assert_eq!(wheelhouse_overview_update(h, first.into(), 1.6, 10000.0, 1, &mut error), 1);
        assert_eq!(wheelhouse_overview_set_view_mode(h, WHEELHOUSE_OVERVIEW_ROWS, 10000.0, &mut error), 1);
        assert_eq!(frame(10000.0).1, 1);
        assert_eq!(frame(20000.0).1, 0);
        // A workspace arriving moves the others while the open one stays
        // open: drawn again until they arrive, and still drawn as itself.
        assert_eq!(wheelhouse_overview_close(h, 20000.0, &mut error), 1);
        assert_eq!(wheelhouse_observe(h, workspaces.as_ptr(), 2, ptr::null(), 0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, first.into(), 1.6, 30000.0, 1, &mut error), 1);
        assert_eq!((frame(30100.0).1, frame(30100.0).2), (1, 1));
        assert_eq!((frame(31000.0).1, frame(31000.0).2), (0, 1));
        assert_eq!(wheelhouse_overview_set_view_mode(h, 7, 20000.0, &mut error), 0);
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        error = ptr::null_mut();
        assert!(wheelhouse_overview_frame(ptr::null_mut(), 0.0, &mut error).is_null());
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        wheelhouse_destroy(h);
    }
}

/// Sizes with no cells behind them open an empty table, allocating nothing
/// from the sizes; cells open by row, the first the header.
#[test]
fn a_table_of_given_cells_opens_from_its_cells_alone() {
    unsafe {
        for (columns, rows) in [(usize::MAX, 0), (0, usize::MAX)] {
            let table = wheelhouse_table_open_cells(ptr::null(), columns, rows, ptr::null(), 20.0, 8.0);
            assert!(!table.is_null());
            wheelhouse_table_free(table);
        }
        let cells = [text("name"), text("crew"), text("Kestrel"), text("4")];
        let table = wheelhouse_table_open_cells(cells.as_ptr(), 2, 2, [0, 1].as_ptr(), 20.0, 8.0);
        assert!(!table.is_null());
        let mut align = 9;
        assert_eq!(wheelhouse_table_cell(table, 0, 1, &mut align).read().unwrap(), "4");
        assert_eq!(align, WHEELHOUSE_TABLE_ALIGN_RIGHT);
        assert_eq!(wheelhouse_table_header(table, 1, 0, 0).read().unwrap(), "name");
        wheelhouse_table_free(table);
    }
}

// Dragging the overview through C: a press, its moves as shares of the
// region and its release slide a long row; past its end the frame has the
// stretch drawn in and says how far, and is flying while it springs back.
// A press says whether it caught something.
#[test]
fn the_overviews_drags_reach_c() {
    unsafe {
        let mut error = ptr::null_mut();
        let h = create("");
        let ids: Vec<WorkspaceId> = (1..=12u8).map(|i| WorkspaceId::from_bytes([i; 16])).collect();
        let names: Vec<String> = (1..=12).map(|i| format!("w{i}")).collect();
        let workspaces: Vec<WorkspaceInput> = ids
            .iter()
            .zip(&names)
            .enumerate()
            .map(|(i, (id, name))| WorkspaceInput { id: (*id).into(), position: i, name: text(name), selected: (i == 0) as u32 })
            .collect();
        assert_eq!(wheelhouse_observe(h, workspaces.as_ptr(), workspaces.len(), ptr::null(), 0, &mut error), 1);
        assert_eq!(wheelhouse_overview_set_grid(h, WHEELHOUSE_OVERVIEW_SHELVES, 0.0, 1000.0, 0.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 0.0, 1, &mut error), 1);
        assert_eq!(wheelhouse_overview_open(h, 0.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 5000.0, 1, &mut error), 1);
        // The row's slide and first tile, the stretch, and flying.
        let frame = |now: f64| {
            let frame = wheelhouse_overview_frame(h, now, &mut ptr::null_mut());
            let f = &*frame;
            let group = &slice(f.groups, f.group_count).unwrap()[0];
            let tile = slice(f.tiles, f.tile_count).unwrap()[0].rect;
            let summary = (group.slide, group.slide_min, tile.x0, f.stretch_group, f.stretch_x, f.stretch_y, f.flying);
            wheelhouse_overview_frame_free(frame);
            summary
        };
        let (slide, slide_min, x0, stretch_group, _, _, flying) = frame(5000.0);
        assert!(slide == 0.0 && slide_min < 0.0, "a long row");
        assert_eq!((stretch_group, flying), (NONE, 0));

        let mut caught = 7;
        assert_eq!(wheelhouse_overview_grab(h, 0, 5000.0, &mut caught, &mut error), 1);
        assert_eq!(caught, 0);
        assert_eq!(wheelhouse_overview_drag(h, 0.5, 0.0, 5016.0, &mut error), 1);
        let (slide, _, dragged_x0, stretch_group, stretch_x, stretch_y, _) = frame(5016.0);
        assert_eq!((slide, stretch_group, stretch_y), (0.0, 0, 0.0));
        assert!(stretch_x > 0.0 && (dragged_x0 - x0 - stretch_x).abs() < 1e-9, "the stretch is drawn in");
        assert_eq!(wheelhouse_overview_release(h, 5016.0, &mut error), 1);
        assert_eq!(frame(5016.0).6, 1, "flying while it springs back");
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 5050.0, 1, &mut error), 1);
        assert_eq!(wheelhouse_overview_grab(h, 0, 5050.0, ptr::null_mut(), &mut error), 1);
        assert_eq!(wheelhouse_overview_release(h, 5050.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 9000.0, 1, &mut error), 1);
        let (slide, _, _, stretch_group, stretch_x, _, flying) = frame(9000.0);
        assert_eq!((slide, stretch_group, stretch_x, flying), (0.0, NONE, 0.0, 0));

        // A flick glides on, and a press catches it.
        assert_eq!(wheelhouse_overview_grab(h, 0, 9000.0, &mut caught, &mut error), 1);
        assert_eq!(wheelhouse_overview_drag(h, -0.1, 0.0, 9016.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_release(h, 9016.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 9050.0, 1, &mut error), 1);
        assert_eq!(frame(9050.0).6, 1, "flying while it glides");
        assert_eq!(wheelhouse_overview_grab(h, NONE, 9050.0, &mut caught, &mut error), 1);
        assert_eq!(caught, 1);
        assert!(error.is_null());

        assert_eq!(wheelhouse_overview_grab(ptr::null_mut(), 0, 0.0, &mut caught, &mut error), 0);
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        wheelhouse_destroy(h);
    }
}

// Zooming a grid through C: the frame says when a grid can zoom and when it
// is all in view; a zoom about a point is a cut, and Fit all flies out to
// the whole grid. Rows don't zoom.
#[test]
fn the_overviews_zoom_reaches_c() {
    unsafe {
        let mut error = ptr::null_mut();
        let h = create("");
        let ids: Vec<WorkspaceId> = (1..=12u8).map(|i| WorkspaceId::from_bytes([i; 16])).collect();
        let names: Vec<String> = (1..=12).map(|i| format!("w{i}")).collect();
        let workspaces: Vec<WorkspaceInput> = ids
            .iter()
            .zip(&names)
            .enumerate()
            .map(|(i, (id, name))| WorkspaceInput { id: (*id).into(), position: i, name: text(name), selected: (i == 0) as u32 })
            .collect();
        assert_eq!(wheelhouse_observe(h, workspaces.as_ptr(), workspaces.len(), ptr::null(), 0, &mut error), 1);
        assert_eq!(wheelhouse_overview_set_view_mode(h, WHEELHOUSE_OVERVIEW_GRID, 0.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_set_grid(h, WHEELHOUSE_OVERVIEW_SHELVES, 0.0, 1000.0, 0.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 0.0, 1, &mut error), 1);
        assert_eq!(wheelhouse_overview_open(h, 0.0, &mut error), 1);
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 5000.0, 1, &mut error), 1);
        // The view's width, whether a grid zooms and fits, and flying.
        let frame = |now: f64| {
            let frame = wheelhouse_overview_frame(h, now, &mut ptr::null_mut());
            let f = &*frame;
            let summary = (f.view.x1 - f.view.x0, f.zoomable, f.fits_all, f.flying);
            wheelhouse_overview_frame_free(frame);
            summary
        };
        let (fit, zoomable, fits_all, _) = frame(5000.0);
        assert_eq!((zoomable, fits_all), (1, 1));
        assert_eq!(wheelhouse_overview_zoom(h, 2.0, 0.5, 0.5, &mut error), 1);
        let (zoomed, _, fits_all, flying) = frame(5000.0);
        assert!((zoomed - fit / 2.0).abs() < 1e-9 * fit && fits_all == 0 && flying == 0);
        assert_eq!(wheelhouse_overview_fit_all(h, 5000.0, &mut error), 1);
        assert_eq!(frame(5000.0).3, 1, "Fit all flies");
        assert_eq!(wheelhouse_overview_update(h, ids[0].into(), 1.6, 9000.0, 1, &mut error), 1);
        let (width, _, fits_all, _) = frame(9000.0);
        assert!((width - fit).abs() < 1e-9 * fit && fits_all == 1);
        assert_eq!(wheelhouse_overview_set_view_mode(h, WHEELHOUSE_OVERVIEW_ROWS, 9000.0, &mut error), 1);
        assert_eq!(frame(9000.0).1, 0, "rows don't zoom");
        assert!(error.is_null());

        assert_eq!(wheelhouse_overview_zoom(ptr::null_mut(), 2.0, 0.5, 0.5, &mut error), 0);
        assert!(!error.is_null());
        wheelhouse_string_free(error);
        wheelhouse_destroy(h);
    }
}
