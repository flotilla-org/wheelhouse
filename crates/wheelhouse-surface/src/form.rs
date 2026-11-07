//! A JSON Schema for an answer, compiled into a surface: an AG-UI
//! Interrupt's `responseSchema`, or later an MCP elicitation's
//! `requestedSchema`, which use the same flat subset. A field with a few
//! choices and nothing else is a row of Buttons, the first primary; anything
//! else is a form with a Submit Button that its required fields' checks hold.
use serde_json::{json, Map, Value};

use crate::{Action, CreateSurface, Input, Surface, ANCHOR, CATALOG};

/// Past this many choices, a lone field is a Picker and Submit, not Buttons.
const MAX_BUTTONS: usize = 3;

#[derive(Debug, Clone, PartialEq)]
struct Choice {
    /// The const the answer carries.
    value: Value,
    /// Its text: a Picker's values are strings.
    key: String,
    title: String,
    description: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
enum Kind {
    Choice { choices: Vec<Choice>, other: bool, multiple: bool },
    Text { number: Option<Number> },
    Bool,
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Number {
    Integer,
    Real,
}

#[derive(Debug, Clone, PartialEq)]
struct Field {
    name: String,
    title: Option<String>,
    description: Option<String>,
    kind: Kind,
    required: bool,
    /// `writeOnly`: its text is obscured as typed, and never shown after.
    secret: bool,
}

/// A compiled schema: how its surface's data and actions become an answer.
#[derive(Debug, Clone, PartialEq)]
pub struct Form {
    fields: Vec<Field>,
}

impl Form {
    /// The form for `schema` and its surface, which says `message` first and
    /// carries `anchor` as its [`ANCHOR`] extension. None for a schema with no
    /// fields.
    pub fn compile(surface_id: &str, schema: &Value, message: Option<&str>, anchor: Option<Value>) -> Option<(Form, CreateSurface)> {
        let fields = fields(schema);
        if fields.is_empty() {
            return None;
        }
        let form = Form { fields };
        let mut components = vec![];
        let mut root = vec![];
        if let Some(message) = message.filter(|m| !m.is_empty()) {
            components.push(json!({ "id": "message", "component": "Text", "text": message }));
            root.push("message".to_owned());
        }
        let mut data = Map::new();
        let actions = match form.buttons() {
            Some((field, choices)) => choices
                .iter()
                .enumerate()
                .map(|(j, choice)| {
                    let id = format!("answer{j}");
                    let context = json!({ field.name.as_str(): choice.value });
                    let variant = if j == 0 { "primary" } else { "default" };
                    components.push(json!({ "id": format!("{id}.label"), "component": "Text", "text": choice.title }));
                    components.push(json!({ "id": id, "component": "Button", "child": format!("{id}.label"), "variant": variant,
                        "action": { "event": { "name": "answer", "context": context } } }));
                    id
                })
                .collect(),
            None => {
                let mut checks = vec![];
                for (i, field) in form.fields.iter().enumerate() {
                    root.push(form.field(i, field, &mut components, &mut data, &mut checks));
                }
                components.push(json!({ "id": "submit.label", "component": "Text", "text": "Submit" }));
                components.push(json!({ "id": "submit", "component": "Button", "child": "submit.label", "variant": "primary",
                    "action": { "event": { "name": "submit" } }, "checks": checks }));
                vec!["submit".to_owned()]
            }
        };
        components.push(json!({ "id": "actions", "component": "Row", "children": actions }));
        root.push("actions".to_owned());
        components.push(json!({ "id": "root", "component": "Column", "children": root }));
        let metadata = anchor.map(|anchor| json!({ "extensions": { ANCHOR: anchor } }));
        let create = CreateSurface {
            surface_id: surface_id.to_owned(),
            catalog_id: Some(CATALOG.to_owned()),
            components,
            data_model: Some(Value::Object(data)),
            metadata,
        };
        Some((form, create))
    }

    /// How many fields it asks.
    pub fn field_count(&self) -> usize {
        self.fields.len()
    }

    /// The lone field a row of Buttons answers, with its choices.
    fn buttons(&self) -> Option<(&Field, &[Choice])> {
        match self.fields.as_slice() {
            [field @ Field { kind: Kind::Choice { choices, other: false, multiple: false }, .. }] if choices.len() <= MAX_BUTTONS => {
                Some((field, choices))
            }
            _ => None,
        }
    }

    fn field(
        &self,
        i: usize,
        field: &Field,
        components: &mut Vec<Value>,
        data: &mut Map<String, Value>,
        checks: &mut Vec<Value>,
    ) -> String {
        let id = format!("f{i}");
        let path = |leaf: &str| format!("/{id}/{leaf}");
        let mut children = vec![];
        let mut text = |suffix: &str, text: &str, variant: &str, children: &mut Vec<String>| {
            let child = format!("{id}.{suffix}");
            components.push(json!({ "id": child, "component": "Text", "text": text, "variant": variant }));
            children.push(child);
        };
        if let Some(title) = &field.title {
            text("title", &format!("**{title}**"), "body", &mut children);
        }
        if let Some(description) = &field.description {
            text("description", description, "caption", &mut children);
        }
        let label = field.title.clone().unwrap_or_else(|| field.name.clone());
        let mut slot = Map::new();
        let required = |leaf: &str| json!({ "@call": "required", "args": { "value": { "@path": path(leaf) } } });
        match &field.kind {
            Kind::Choice { choices, other, multiple } => {
                let options: Vec<Value> = choices
                    .iter()
                    .map(|c| match &c.description {
                        Some(d) => json!({ "label": c.title, "value": c.key, "description": d }),
                        None => json!({ "label": c.title, "value": c.key }),
                    })
                    .collect();
                let variant = if *multiple { "multipleSelection" } else { "mutuallyExclusive" };
                if !choices.is_empty() {
                    components.push(json!({ "id": format!("{id}.choice"), "component": "ChoicePicker", "variant": variant,
                        "options": options, "value": { "@path": path("choice") } }));
                    children.push(format!("{id}.choice"));
                    slot.insert("choice".into(), json!([]));
                }
                if *other {
                    // With no choices, the text is the answer, not another one.
                    let label = if choices.is_empty() { "Answer" } else { "Other" };
                    let variant = if field.secret { "obscured" } else { "shortText" };
                    components.push(json!({ "id": format!("{id}.other"), "component": "TextField", "label": label,
                        "placeholder": label, "variant": variant, "value": { "@path": path("other") } }));
                    children.push(format!("{id}.other"));
                    slot.insert("other".into(), json!(""));
                }
                if field.required {
                    let condition = match (choices.is_empty(), *other) {
                        (false, true) => json!({ "@call": "or", "args": { "values": [required("choice"), required("other")] } }),
                        (true, _) => required("other"),
                        (false, false) => required("choice"),
                    };
                    checks.push(json!({ "condition": condition, "message": format!("Answer {label}") }));
                }
            }
            Kind::Text { number } => {
                let variant = if field.secret {
                    "obscured"
                } else if number.is_some() {
                    "number"
                } else {
                    "shortText"
                };
                components.push(json!({ "id": format!("{id}.text"), "component": "TextField", "label": label,
                    "variant": variant, "value": { "@path": path("text") } }));
                children.push(format!("{id}.text"));
                slot.insert("text".into(), json!(""));
                if field.required {
                    checks.push(json!({ "condition": required("text"), "message": format!("Answer {label}") }));
                }
            }
            Kind::Bool => {
                components.push(json!({ "id": format!("{id}.check"), "component": "CheckBox", "label": label,
                    "value": { "@path": path("check") } }));
                children.push(format!("{id}.check"));
                slot.insert("check".into(), json!(false));
            }
        }
        components.push(json!({ "id": id, "component": "Column", "children": children }));
        data.insert(id.clone(), Value::Object(slot));
        id
    }

    /// Applies an input to the surface. Choosing a single choice clears the
    /// field's "Other" text, and typing one clears its choice, so the answer
    /// shown is the one sent.
    pub fn input(&self, surface: &mut Surface, id: &str, input: Input) -> Result<Option<Action>, String> {
        let action = surface.input(id, input)?;
        let single = |field: &str| {
            let i: usize = field.strip_prefix('f')?.parse().ok()?;
            matches!(self.fields.get(i)?.kind, Kind::Choice { multiple: false, .. }).then_some(())
        };
        match (id.split_once('.'), input) {
            (Some((field, "choice")), Input::Choose(_)) if single(field).is_some() => {
                surface.update_data(&format!("/{field}/other"), json!(""))
            }
            (Some((field, "other")), Input::Text(text)) if !text.is_empty() && single(field).is_some() => {
                surface.update_data(&format!("/{field}/choice"), json!([]))
            }
            _ => {}
        }
        Ok(action)
    }

    /// The answer an action gives: a Button's choice, or Submit's fields
    /// from the surface's data. None for an action this form didn't make.
    pub fn answer(&self, surface: &Surface, action: &Action) -> Option<Value> {
        match action.name.as_str() {
            "answer" => {
                let (field, choices) = self.buttons()?;
                let value = action.context.get(&field.name)?;
                choices.iter().any(|c| &c.value == value).then(|| json!({ field.name.as_str(): value }))
            }
            "submit" => {
                let data = surface.data();
                let mut answer = Map::new();
                for (i, field) in self.fields.iter().enumerate() {
                    let at = |leaf: &str| data.pointer(&format!("/f{i}/{leaf}"));
                    let text = |leaf: &str| at(leaf).and_then(Value::as_str).filter(|t| !t.is_empty()).map(str::to_owned);
                    let value = match &field.kind {
                        Kind::Choice { choices, multiple, .. } => {
                            let chosen: Vec<Value> = at("choice")
                                .and_then(Value::as_array)
                                .into_iter()
                                .flatten()
                                .filter_map(|k| choices.iter().find(|c| Some(c.key.as_str()) == k.as_str()))
                                .map(|c| c.value.clone())
                                .collect();
                            let other = text("other").map(Value::from);
                            if *multiple {
                                Some(Value::from(chosen.into_iter().chain(other).collect::<Vec<_>>()))
                                    .filter(|v| v.as_array().is_some_and(|a| !a.is_empty()))
                            } else {
                                other.or_else(|| chosen.into_iter().next())
                            }
                        }
                        Kind::Text { number: Some(number) } => text("text").map(|t| parse_number(t, *number)),
                        Kind::Text { number: None } => text("text").map(Value::from),
                        Kind::Bool => Some(at("check").cloned().unwrap_or(Value::Bool(false))),
                    };
                    match value {
                        Some(value) => {
                            answer.insert(field.name.clone(), value);
                        }
                        None if field.required => return None,
                        None => {}
                    }
                }
                Some(Value::Object(answer))
            }
            _ => None,
        }
    }

    /// An answer in words: each field's choice by its title, prefixed with
    /// the field's title when `titled`.
    pub fn summary(&self, answer: &Value, titled: bool) -> String {
        let words = |field: &Field, value: &Value| -> String {
            let one = |v: &Value| {
                match &field.kind {
                    _ if field.secret => Some("•••".to_owned()),
                    Kind::Choice { choices, .. } => choices.iter().find(|c| &c.value == v).map(|c| c.title.clone()),
                    Kind::Bool => Some(if v.as_bool() == Some(true) { "Yes" } else { "No" }.to_owned()),
                    Kind::Text { .. } => None,
                }
                .unwrap_or_else(|| v.as_str().map_or_else(|| v.to_string(), str::to_owned))
            };
            match value {
                Value::Array(values) => values.iter().map(one).collect::<Vec<_>>().join(", "),
                v => one(v),
            }
        };
        self.fields
            .iter()
            .filter_map(|field| {
                let words = words(field, answer.get(&field.name)?);
                Some(if titled { format!("{}: {words}", field.title.as_deref().unwrap_or(&field.name)) } else { words })
            })
            .collect::<Vec<_>>()
            .join(" · ")
    }
}

/// The fields of an object schema: its `required` names in order, then the
/// rest of its properties.
fn fields(schema: &Value) -> Vec<Field> {
    let Some(properties) = schema.get("properties").and_then(Value::as_object) else { return vec![] };
    let required: Vec<&str> = schema.get("required").and_then(Value::as_array).into_iter().flatten().filter_map(Value::as_str).collect();
    let rest = properties.keys().map(String::as_str).filter(|n| !required.contains(n));
    required
        .iter()
        .copied()
        .chain(rest)
        .filter_map(|name| {
            let property = properties.get(name)?;
            let text = |key: &str| property.get(key).and_then(Value::as_str).filter(|t| !t.is_empty()).map(str::to_owned);
            Some(Field {
                name: name.to_owned(),
                title: text("title"),
                description: text("description"),
                kind: kind(property),
                required: required.contains(&name),
                secret: property.get("writeOnly").and_then(Value::as_bool) == Some(true),
            })
        })
        .collect()
}

fn kind(property: &Value) -> Kind {
    let of_type = |t: &str| property.get("type").and_then(Value::as_str) == Some(t);
    if of_type("array") {
        if let Some(Kind::Choice { choices, other, .. }) = property.get("items").and_then(choices) {
            return Kind::Choice { choices, other, multiple: true };
        }
    }
    match choices(property) {
        Some(choice) => choice,
        None if of_type("boolean") => Kind::Bool,
        None if of_type("integer") => Kind::Text { number: Some(Number::Integer) },
        None if of_type("number") => Kind::Text { number: Some(Number::Real) },
        None => Kind::Text { number: None },
    }
}

/// Typed text as the number a field asks for: an integer as one, and only a
/// finite number, which JSON can carry; anything else stays text, for the
/// producer to refuse.
fn parse_number(text: String, number: Number) -> Value {
    let integer = text.parse::<i64>().ok().map(Value::from);
    let real = || text.parse::<f64>().ok().and_then(serde_json::Number::from_f64).map(Value::Number);
    match number {
        Number::Integer => integer,
        Number::Real => integer.or_else(real),
    }
    .unwrap_or(Value::String(text))
}

/// A schema's choices: `oneOf`/`anyOf` consts, where a plain `type: string`
/// entry takes other text, or an `enum` with its `enumNames`.
fn choices(schema: &Value) -> Option<Kind> {
    let mut keys = vec![];
    // A Picker's values are strings, so the const 1 and "1" would share one.
    let mut choice = |value: &Value, title: Option<&str>, description: Option<&str>| {
        let text = value.as_str().map_or_else(|| value.to_string(), str::to_owned);
        let mut key = text.clone();
        for n in 2.. {
            if !keys.contains(&key) {
                break;
            }
            key = format!("{text}#{n}");
        }
        keys.push(key.clone());
        Choice {
            value: value.clone(),
            title: title.map_or_else(|| text.clone(), str::to_owned),
            key,
            description: description.map(str::to_owned),
        }
    };
    if let Some(options) = schema.get("oneOf").or(schema.get("anyOf")).and_then(Value::as_array) {
        let choices = options
            .iter()
            .filter_map(|o| {
                Some(choice(o.get("const")?, o.get("title").and_then(Value::as_str), o.get("description").and_then(Value::as_str)))
            })
            .collect();
        let other = options.iter().any(|o| o.get("const").is_none() && o.get("type").and_then(Value::as_str) == Some("string"));
        let choices: Vec<Choice> = choices;
        return (!choices.is_empty() || other).then_some(Kind::Choice { choices, other, multiple: false });
    }
    let values = schema.get("enum").and_then(Value::as_array)?;
    let names = schema.get("enumNames").and_then(Value::as_array);
    let title = |i: usize| names.and_then(|n| n.get(i)).and_then(Value::as_str);
    let choices = values.iter().enumerate().map(|(i, v)| choice(v, title(i), None)).collect();
    Some(Kind::Choice { choices, other: false, multiple: false })
}
