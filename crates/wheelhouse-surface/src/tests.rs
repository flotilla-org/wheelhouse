use hegel::generators as gs;
use hegel::TestCase;
use serde_json::{json, Value};

use super::*;

fn surface(components: Value, data: Value) -> Surface {
    let components = components.as_array().expect("a list").clone();
    Surface::create(CreateSurface { surface_id: "s".into(), components, data_model: Some(data), ..CreateSurface::default() })
        .expect("a surface")
}

fn kinds(surface: &Surface) -> Vec<(ItemKind, u32)> {
    surface.items().iter().map(|i| (i.kind, i.depth)).collect()
}

#[test]
fn a_message_from_the_spec_parses_and_unknown_components_are_marked() {
    // updateComponents in the spec's own wording, cut down.
    let line = r#"{"version": "1.0", "updateComponents": {"surfaceId": "contact", "components": [
        {"id": "root", "component": "Card", "child": "col"},
        {"id": "col", "component": "Column", "children": ["icon", "name", "pref", "news", "send"]},
        {"id": "icon", "component": "Icon", "name": "mail"},
        {"id": "name", "component": "TextField", "label": "Name", "value": {"@path": "/contact/name"}},
        {"id": "pref", "component": "ChoicePicker", "options": [{"label": "Email", "value": "email"}, {"label": "Phone", "value": "phone"}],
         "value": {"@path": "/contact/preference"}},
        {"id": "news", "component": "CheckBox", "label": "Subscribe", "value": {"@path": "/contact/subscribe"}},
        {"id": "send.label", "component": "Text", "text": "Send"},
        {"id": "send", "component": "Button", "child": "send.label", "variant": "primary",
         "action": {"event": {"name": "submitContactForm", "context": {"subscribed": {"@path": "/contact/subscribe"}}}},
         "checks": [{"condition": {"@call": "required", "args": {"value": {"@path": "/contact/name"}}}}]}]}}"#;
    let envelope: Envelope = serde_json::from_str(line).expect("an envelope");
    let Message::UpdateComponents(update) = envelope.message else { panic!("updateComponents") };
    let mut s = Surface::create(CreateSurface { surface_id: update.surface_id, ..CreateSurface::default() }).unwrap();
    s.update_components(update.components).unwrap();
    s.update_data("/contact", json!({ "preference": ["phone"], "subscribe": true }));
    use ItemKind::*;
    assert_eq!(
        kinds(&s),
        [(Card, 0), (Column, 1), (Unsupported, 2), (TextField, 2), (Picker, 2), (Choice, 3), (Choice, 3), (CheckBox, 2), (Button, 2)]
    );
    let items = s.items();
    assert_eq!([items[5].on, items[6].on], [false, true]);
    assert!(items[7].on);
    let send = &items[8];
    assert_eq!((send.text.as_str(), send.variant, send.enabled), ("Send", Variant::Primary, false));
}

#[test]
fn inputs_write_their_paths_and_a_button_gives_its_action_once_its_checks_pass() {
    let mut s = surface(
        json!([
            { "id": "root", "component": "Column", "children": ["name", "tags", "go"] },
            { "id": "name", "component": "TextField", "label": "Name", "value": { "@path": "/name" } },
            { "id": "tags", "component": "ChoicePicker", "variant": "multipleSelection",
              "options": [{ "label": "A", "value": "a" }, { "label": "B", "value": "b" }], "value": { "@path": "/tags" } },
            { "id": "go.label", "component": "Text", "text": "Go" },
            { "id": "go", "component": "Button", "child": "go.label",
              "action": { "event": { "name": "go", "context": { "who": { "@path": "/name" }, "fixed": 3 } } },
              "checks": [{ "condition": { "@call": "length", "args": { "value": { "@path": "/name" }, "min": 2 } } }] }
        ]),
        json!({}),
    );
    assert!(s.input("go", Input::Press).is_err(), "a failing check refuses the press");
    s.input("name", Input::Text("Al")).unwrap();
    s.input("tags", Input::Choose("b")).unwrap();
    s.input("tags", Input::Choose("a")).unwrap();
    s.input("tags", Input::Choose("b")).unwrap();
    assert_eq!(s.data(), &json!({ "name": "Al", "tags": ["a"] }));
    assert!(s.input("tags", Input::Choose("c")).is_err(), "only an offered choice");
    let action = s.input("go", Input::Press).unwrap().expect("an action");
    assert_eq!(action.name, "go");
    assert_eq!(Value::Object(action.context), json!({ "who": "Al", "fixed": 3 }));
}

/// What the Codex bridge sends for a command approval.
fn approval() -> Value {
    json!({ "type": "object", "required": ["decision"], "properties": { "decision": { "title": "Decision", "oneOf": [
        { "const": "accept", "title": "Approve" },
        { "const": "acceptForSession", "title": "Approve for this session" },
        { "const": "decline", "title": "Deny" }
    ] } } })
}

#[test]
fn a_few_choices_are_buttons_and_the_one_pressed_is_the_answer() {
    let anchor = json!({ "toolCallId": "call-1" });
    let (form, create) = Form::compile("i0", &approval(), Some("Run this?"), Some(anchor.clone())).unwrap();
    let mut s = Surface::create(create).unwrap();
    assert_eq!(s.anchor(), Some(&anchor));
    let items = s.items();
    let buttons: Vec<_> = items.iter().filter(|i| i.kind == ItemKind::Button).map(|i| (i.text.as_str(), i.variant)).collect();
    assert_eq!(buttons, [("Approve", Variant::Primary), ("Approve for this session", Variant::Default), ("Deny", Variant::Default)]);
    assert_eq!(items[1].text, "Run this?");
    let action = form.input(&mut s, "answer1", Input::Press).unwrap().unwrap();
    let answer = form.answer(&s, &action).unwrap();
    assert_eq!(answer, json!({ "decision": "acceptForSession" }));
    assert_eq!(form.summary(&answer, false), "Approve for this session");
}

#[test]
fn a_question_takes_a_choice_or_other_text_and_submits_once_answered() {
    // What the Codex bridge sends for requestUserInput.
    let schema = json!({ "type": "object", "required": ["branch", "note"], "properties": {
        "branch": { "title": "Branch", "description": "Which branch?", "anyOf": [
            { "const": "main", "title": "main", "description": "The default" },
            { "const": "dev", "title": "dev" },
            { "type": "string", "title": "Other" }
        ] },
        "note": { "title": "Note", "anyOf": [{ "type": "string", "title": "Other" }] }
    } });
    let (form, create) = Form::compile("i1", &schema, None, None).unwrap();
    let mut s = Surface::create(create).unwrap();
    let submit = |s: &Surface| s.items().into_iter().find(|i| i.id == "submit").unwrap().enabled;
    let choice = s.items().into_iter().find(|i| i.kind == ItemKind::Choice).unwrap();
    assert_eq!((choice.text.as_str(), choice.detail.as_str()), ("main", "The default"));
    assert!(!submit(&s));
    form.input(&mut s, "f0.choice", Input::Choose("dev")).unwrap();
    assert!(!submit(&s), "the note is still unanswered");
    form.input(&mut s, "f1.other", Input::Text("ship it")).unwrap();
    assert!(submit(&s));
    form.input(&mut s, "f0.other", Input::Text("release")).unwrap();
    assert_eq!(s.data().pointer("/f0/choice"), Some(&json!([])), "other text replaces the choice");
    let action = form.input(&mut s, "submit", Input::Press).unwrap().unwrap();
    let answer = form.answer(&s, &action).unwrap();
    assert_eq!(answer, json!({ "branch": "release", "note": "ship it" }));
    assert_eq!(form.summary(&answer, true), "Branch: release · Note: ship it");
    form.input(&mut s, "f0.choice", Input::Choose("main")).unwrap();
    assert_eq!(s.data().pointer("/f0/other"), Some(&json!("")), "a choice replaces other text");
}

#[test]
fn a_compiled_surface_is_an_a2ui_message() {
    let (_, create) = Form::compile("i0", &approval(), None, None).unwrap();
    let line = serde_json::to_string(&Envelope { version: VERSION.into(), message: Message::CreateSurface(create.clone()) }).unwrap();
    let back: Envelope = serde_json::from_str(&line).unwrap();
    assert_eq!(back.message, Message::CreateSurface(create));
    assert!(line.starts_with(r#"{"version":"1.0","createSurface":{"surfaceId":"i0","catalogId":"#), "{line}");
}

#[test]
fn a_cycle_of_references_ends() {
    let s = surface(json!([{ "id": "root", "component": "Column", "children": ["root", "x"] }]), json!({}));
    assert_eq!(kinds(&s), [(ItemKind::Column, 0), (ItemKind::Unsupported, 1), (ItemKind::Unsupported, 1)]);
}

/// Answering every field of a form of choice and text fields, in any order
/// and with any changes of mind, submits an answer the schema allows: each
/// field once, with an offered choice or the text last typed.
#[hegel::test(test_cases = 300)]
fn a_submitted_answer_is_one_the_schema_allows(tc: TestCase) {
    let field_count = tc.draw(gs::integers::<usize>().min_value(1).max_value(4));
    let mut properties = serde_json::Map::new();
    let mut offered = vec![];
    for f in 0..field_count {
        let n = tc.draw(gs::integers::<usize>().max_value(5));
        let other = n == 0 || tc.draw(gs::booleans());
        let mut options: Vec<Value> = (0..n).map(|i| json!({ "const": format!("v{f}.{i}"), "title": format!("V{i}") })).collect();
        if other {
            options.push(json!({ "type": "string", "title": "Other" }));
        }
        properties.insert(format!("q{f}"), json!({ "title": format!("Q{f}"), "anyOf": options }));
        offered.push((n, other));
    }
    let required: Vec<String> = (0..field_count).map(|f| format!("q{f}")).collect();
    let schema = json!({ "type": "object", "required": required, "properties": properties });
    let (form, create) = Form::compile("p", &schema, None, None).unwrap();
    let mut s = Surface::create(create).unwrap();
    let mut expected = serde_json::Map::new();
    let buttons = field_count == 1 && !offered[0].1 && offered[0].0 <= 3;
    let steps = tc.draw(gs::integers::<usize>().min_value(field_count).max_value(field_count * 3));
    for step in 0..steps {
        // Each field is visited at least once, then at random.
        let f = if step < field_count { step } else { tc.draw(gs::integers::<usize>().max_value(field_count - 1)) };
        let (n, other) = offered[f];
        let use_text = other && (n == 0 || tc.draw(gs::booleans()));
        if buttons {
            continue;
        }
        if use_text {
            let text = format!("t{step}");
            form.input(&mut s, &format!("f{f}.other"), Input::Text(&text)).unwrap();
            expected.insert(format!("q{f}"), json!(text));
        } else {
            let i = tc.draw(gs::integers::<usize>().max_value(n - 1));
            form.input(&mut s, &format!("f{f}.choice"), Input::Choose(&format!("v{f}.{i}"))).unwrap();
            expected.insert(format!("q{f}"), json!(format!("v{f}.{i}")));
        }
    }
    if buttons {
        let i = tc.draw(gs::integers::<usize>().max_value(offered[0].0 - 1));
        let action = form.input(&mut s, &format!("answer{i}"), Input::Press).unwrap().unwrap();
        assert_eq!(form.answer(&s, &action), Some(json!({ "q0": format!("v0.{i}") })));
        return;
    }
    let action = form.input(&mut s, "submit", Input::Press).unwrap().expect("every field answered, so Submit is enabled");
    assert_eq!(form.answer(&s, &action), Some(Value::Object(expected)));
}
