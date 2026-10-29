//! Marshalling the core can't see: what C reads from snapshots and effects.
use super::*;
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
