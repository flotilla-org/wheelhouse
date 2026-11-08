//! A2UI v1.0 surfaces (a2ui.org; wheelhouse#695): a producer's UI as a flat
//! list of components over a JSON data model, drawn by the host. This crate
//! keeps a surface's state and resolves it into [`Item`]s for a frontend to
//! draw; inputs update the data model and a Button's agent action comes back
//! as an [`Action`]. It draws a subset of the basic catalog, which
//! [`CATALOG`] names; anything else is an [`ItemKind::Unsupported`] Item.
use std::collections::{HashMap, HashSet};

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

mod form;

pub use form::Form;

/// The catalog this renderer supports: the basic catalog's Text, Row,
/// Column, Card, Divider, Button, ChoicePicker, TextField and CheckBox, with
/// the functions required, length, and, or and not; a ChoicePicker option
/// may also carry a `description`.
pub const CATALOG: &str = "https://github.com/flotilla-org/wheelhouse/catalogs/a2ui/v1";

/// The A2UI version these messages carry.
pub const VERSION: &str = "1.0";

/// What a surface's `metadata.extensions` names the thing it is about with.
pub const ANCHOR: &str = "wheelhouse_anchor";

/// One agent-to-renderer message.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Envelope {
    pub version: String,
    #[serde(flatten)]
    pub message: Message,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Message {
    CreateSurface(CreateSurface),
    UpdateComponents(UpdateComponents),
    UpdateDataModel(UpdateDataModel),
    DeleteSurface(DeleteSurface),
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CreateSurface {
    pub surface_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub catalog_id: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub components: Vec<Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub data_model: Option<Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub metadata: Option<Value>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateComponents {
    pub surface_id: String,
    pub components: Vec<Value>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateDataModel {
    pub surface_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    pub value: Value,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeleteSurface {
    pub surface_id: String,
}

#[derive(Debug, Clone, PartialEq, Deserialize)]
struct Check {
    condition: Value,
}

#[derive(Debug, Clone, PartialEq, Deserialize)]
struct Choice {
    label: Value,
    value: String,
    #[serde(default)]
    description: Option<Value>,
}

/// Dynamic values (literals, `{"@path"}` and `{"@call"}`) stay as JSON and
/// resolve when drawn.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(tag = "component")]
enum Component {
    Text {
        text: Value,
        #[serde(default)]
        variant: Option<String>,
    },
    Row {
        #[serde(default)]
        children: Value,
    },
    Column {
        #[serde(default)]
        children: Value,
    },
    Card {
        child: String,
    },
    Divider,
    Button {
        child: String,
        #[serde(default)]
        variant: Option<String>,
        action: Value,
        #[serde(default)]
        checks: Vec<Check>,
    },
    ChoicePicker {
        #[serde(default)]
        label: Option<Value>,
        #[serde(default)]
        variant: Option<String>,
        options: Vec<Choice>,
        value: Value,
    },
    TextField {
        label: Value,
        value: Value,
        #[serde(default)]
        placeholder: Option<Value>,
        #[serde(default)]
        variant: Option<String>,
    },
    CheckBox {
        label: Value,
        value: Value,
    },
    #[serde(other)]
    Unsupported,
}

#[derive(Debug, Clone, PartialEq, Deserialize)]
struct Entry {
    id: String,
    #[serde(flatten)]
    component: Component,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum ItemKind {
    #[default]
    Text,
    Row,
    Column,
    Card,
    Divider,
    Button,
    /// A ChoicePicker, followed by its Choices one level deeper.
    Picker,
    Choice,
    TextField,
    CheckBox,
    /// A component outside [`CATALOG`], or a reference to none.
    Unsupported,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum Variant {
    #[default]
    Default,
    /// A Button's main call to action.
    Primary,
    /// A Button drawn as a link.
    Borderless,
    /// A Text's smaller, weaker style.
    Caption,
    /// A Picker, and its Choices, that take several.
    Multiple,
    /// A TextField whose text is hidden.
    Obscured,
    LongText,
    Number,
}

/// A component resolved for drawing, in pre-order: its children follow it
/// one `depth` deeper.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Default)]
pub struct Item {
    /// Its component's; a Choice's is its Picker's.
    pub id: String,
    pub kind: ItemKind,
    pub depth: u32,
    pub variant: Variant,
    /// A Text's, a Button's label, a Choice's label, a TextField's value.
    pub text: String,
    /// A Picker's, a TextField's or a CheckBox's label.
    pub label: String,
    /// A Choice's description, a TextField's placeholder.
    pub detail: String,
    /// A Choice's value.
    pub value: String,
    /// A Text that is wholly `**strong**` or a `#` heading, drawn strong.
    pub strong: bool,
    /// A chosen Choice, a checked CheckBox.
    pub on: bool,
    /// A Button whose checks pass.
    pub enabled: bool,
}

/// What the user did to a component.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Input<'a> {
    Press,
    /// A Picker's option, by value.
    Choose(&'a str),
    Check(bool),
    Text(&'a str),
}

/// A Button's agent action, its context resolved.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Action {
    pub name: String,
    pub surface_id: String,
    pub source_component_id: String,
    pub context: Map<String, Value>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Surface {
    id: String,
    components: HashMap<String, Component>,
    data: Value,
    metadata: Value,
}

/// How deep a component tree may nest before the rest is cut off: a cycle
/// of references would otherwise never end.
const MAX_DEPTH: u32 = 32;

impl Surface {
    pub fn create(create: CreateSurface) -> Result<Surface, String> {
        let mut surface = Surface {
            id: create.surface_id,
            components: HashMap::new(),
            data: create.data_model.unwrap_or_else(|| Value::Object(Map::new())),
            metadata: create.metadata.unwrap_or(Value::Null),
        };
        surface.update_components(create.components)?;
        Ok(surface)
    }

    pub fn id(&self) -> &str {
        &self.id
    }

    /// Adds or replaces components by id.
    pub fn update_components(&mut self, components: Vec<Value>) -> Result<(), String> {
        for value in components {
            let entry: Entry = serde_json::from_value(value).map_err(|e| e.to_string())?;
            self.components.insert(entry.id, entry.component);
        }
        Ok(())
    }

    /// Replaces the value at a JSON Pointer, or the whole model at `/`; null
    /// removes it.
    pub fn update_data(&mut self, path: &str, value: Value) {
        if path.is_empty() || path == "/" {
            self.data = value;
            return;
        }
        let tokens: Vec<String> = path.trim_start_matches('/').split('/').map(|t| t.replace("~1", "/").replace("~0", "~")).collect();
        let (last, parents) = tokens.split_last().expect("a path past the root has a token");
        let mut at = &mut self.data;
        for token in parents {
            if !at.is_object() {
                *at = Value::Object(Map::new());
            }
            at = at.as_object_mut().expect("an object").entry(token.clone()).or_insert(Value::Null);
        }
        if !at.is_object() {
            *at = Value::Object(Map::new());
        }
        let object = at.as_object_mut().expect("an object");
        if value.is_null() {
            object.remove(last);
        } else {
            object.insert(last.clone(), value);
        }
    }

    pub fn data(&self) -> &Value {
        &self.data
    }

    /// What `metadata.extensions[ANCHOR]` says the surface is about.
    pub fn anchor(&self) -> Option<&Value> {
        self.metadata.get("extensions")?.get(ANCHOR)
    }

    pub fn items(&self) -> Vec<Item> {
        let mut items = vec![];
        let mut seen = HashSet::new();
        self.walk("root", 0, &mut seen, &mut items);
        items
    }

    fn walk<'a>(&'a self, id: &'a str, depth: u32, seen: &mut HashSet<&'a str>, items: &mut Vec<Item>) {
        let item = |kind| Item { id: id.to_owned(), kind, depth, ..Item::default() };
        let Some(component) = self.components.get(id).filter(|_| depth < MAX_DEPTH && seen.insert(id)) else {
            items.push(item(ItemKind::Unsupported));
            return;
        };
        match component {
            Component::Text { text, variant } => {
                let text = self.string(text);
                let (text, strong) = strong(&text);
                let variant = if variant.as_deref() == Some("caption") { Variant::Caption } else { Variant::Default };
                items.push(Item { text, strong, variant, ..item(ItemKind::Text) });
            }
            Component::Row { children } | Component::Column { children } => {
                let kind = if matches!(component, Component::Row { .. }) { ItemKind::Row } else { ItemKind::Column };
                items.push(item(kind));
                for child in children.as_array().into_iter().flatten().filter_map(Value::as_str) {
                    self.walk(child, depth + 1, seen, items);
                }
            }
            Component::Card { child } => {
                items.push(item(ItemKind::Card));
                self.walk(child, depth + 1, seen, items);
            }
            Component::Divider => items.push(item(ItemKind::Divider)),
            Component::Button { child, variant, checks, .. } => {
                let variant = match variant.as_deref() {
                    Some("primary") => Variant::Primary,
                    Some("borderless") => Variant::Borderless,
                    _ => Variant::Default,
                };
                // A Text child is the label; anything else isn't drawn.
                let text = match self.components.get(child.as_str()) {
                    Some(Component::Text { text, .. }) => strong(&self.string(text)).0,
                    _ => String::new(),
                };
                let enabled = checks.iter().all(|c| truthy(&self.eval(&c.condition)));
                items.push(Item { text, variant, enabled, ..item(ItemKind::Button) });
            }
            Component::ChoicePicker { label, variant, options, value } => {
                let variant = if variant.as_deref() == Some("multipleSelection") { Variant::Multiple } else { Variant::Default };
                let label = label.as_ref().map(|l| self.string(l)).unwrap_or_default();
                items.push(Item { label, variant, ..item(ItemKind::Picker) });
                let chosen = strings(&self.eval(value));
                for option in options {
                    items.push(Item {
                        depth: depth + 1,
                        variant,
                        text: self.string(&option.label),
                        detail: option.description.as_ref().map(|d| self.string(d)).unwrap_or_default(),
                        value: option.value.clone(),
                        on: chosen.contains(&option.value),
                        ..item(ItemKind::Choice)
                    });
                }
            }
            Component::TextField { label, value, placeholder, variant } => {
                let variant = match variant.as_deref() {
                    Some("obscured") => Variant::Obscured,
                    Some("longText") => Variant::LongText,
                    Some("number") => Variant::Number,
                    _ => Variant::Default,
                };
                items.push(Item {
                    variant,
                    label: self.string(label),
                    text: self.string(value),
                    detail: placeholder.as_ref().map(|p| self.string(p)).unwrap_or_default(),
                    ..item(ItemKind::TextField)
                });
            }
            Component::CheckBox { label, value } => {
                let on = truthy(&self.eval(value));
                items.push(Item { label: self.string(label), on, ..item(ItemKind::CheckBox) });
            }
            Component::Unsupported => items.push(item(ItemKind::Unsupported)),
        }
    }

    /// Applies an input to the component `id`: an input component writes
    /// its bound path, and a Button whose checks pass gives its agent
    /// action. A Button's local function call isn't run.
    pub fn input(&mut self, id: &str, input: Input) -> Result<Option<Action>, String> {
        let component = self.components.get(id).ok_or_else(|| format!("no component {id}"))?.clone();
        let bound = |value: &Value| {
            value.get("@path").and_then(Value::as_str).map(str::to_owned).ok_or_else(|| format!("{id} isn't bound to the data model"))
        };
        match (&component, input) {
            (Component::Button { action, checks, .. }, Input::Press) => {
                if !checks.iter().all(|c| truthy(&self.eval(&c.condition))) {
                    return Err(format!("{id}'s checks don't pass"));
                }
                let Some(event) = action.get("event") else { return Ok(None) };
                let name = event.get("name").and_then(Value::as_str).ok_or("an event without a name")?;
                let context =
                    event.get("context").and_then(Value::as_object).map(|c| c.iter().map(|(k, v)| (k.clone(), self.eval(v))).collect());
                Ok(Some(Action {
                    name: name.to_owned(),
                    surface_id: self.id.clone(),
                    source_component_id: id.to_owned(),
                    context: context.unwrap_or_default(),
                }))
            }
            (Component::ChoicePicker { variant, options, value, .. }, Input::Choose(choice)) => {
                if !options.iter().any(|o| o.value == choice) {
                    return Err(format!("{id} doesn't offer {choice}"));
                }
                let path = bound(value)?;
                let mut chosen = strings(&self.eval(value));
                if variant.as_deref() == Some("multipleSelection") {
                    match chosen.iter().position(|c| c == choice) {
                        Some(at) => {
                            chosen.remove(at);
                        }
                        None => chosen.push(choice.to_owned()),
                    }
                } else {
                    chosen = vec![choice.to_owned()];
                }
                self.update_data(&path, Value::from(chosen));
                Ok(None)
            }
            (Component::TextField { value, .. }, Input::Text(text)) => {
                let path = bound(value)?;
                self.update_data(&path, Value::from(text));
                Ok(None)
            }
            (Component::CheckBox { value, .. }, Input::Check(on)) => {
                let path = bound(value)?;
                self.update_data(&path, Value::from(on));
                Ok(None)
            }
            _ => Err(format!("{id} doesn't take {input:?}")),
        }
    }

    fn string(&self, value: &Value) -> String {
        match self.eval(value) {
            Value::String(s) => s,
            Value::Null => String::new(),
            v => v.to_string(),
        }
    }

    /// Resolves a dynamic value: a `{"@path"}` binding, a `{"@call"}` to a
    /// function [`CATALOG`] has, or a literal. An unknown function is null,
    /// so a check on it fails.
    fn eval(&self, value: &Value) -> Value {
        if let Some(path) = value.get("@path").and_then(Value::as_str) {
            let path = if path == "/" { "" } else { path };
            return self.data.pointer(path).cloned().unwrap_or(Value::Null);
        }
        let Some(call) = value.get("@call").and_then(Value::as_str) else { return value.clone() };
        let args = value.get("args").cloned().unwrap_or(Value::Null);
        let arg = |name: &str| args.get(name).map(|a| self.eval(a)).unwrap_or(Value::Null);
        let values = || {
            args.get("values")
                .and_then(Value::as_array)
                .map(|v| v.iter().map(|a| truthy(&self.eval(a))).collect::<Vec<_>>())
                .unwrap_or_default()
        };
        match call {
            "required" => Value::from(!empty(&arg("value"))),
            "length" => {
                let len = arg("value").as_str().map_or(0, |s| s.chars().count()) as u64;
                let min = args.get("min").and_then(Value::as_u64).unwrap_or(0);
                let max = args.get("max").and_then(Value::as_u64).unwrap_or(u64::MAX);
                Value::from(min <= len && len <= max)
            }
            "and" => Value::from(values().iter().all(|v| *v)),
            "or" => Value::from(values().iter().any(|v| *v)),
            "not" => Value::from(!truthy(&arg("value"))),
            _ => Value::Null,
        }
    }
}

/// A boolean, or a validation result's `valid`.
fn truthy(value: &Value) -> bool {
    match value {
        Value::Bool(b) => *b,
        Value::Object(o) => o.get("valid").and_then(Value::as_bool).unwrap_or(false),
        _ => false,
    }
}

fn empty(value: &Value) -> bool {
    match value {
        Value::Null => true,
        Value::String(s) => s.is_empty(),
        Value::Array(a) => a.is_empty(),
        Value::Object(o) => o.is_empty(),
        _ => false,
    }
}

fn strings(value: &Value) -> Vec<String> {
    match value {
        Value::Array(a) => a.iter().filter_map(Value::as_str).map(str::to_owned).collect(),
        Value::String(s) => vec![s.clone()],
        _ => vec![],
    }
}

/// Text supports simple markdown; of it, a whole-text `**strong**` or a
/// heading is drawn strong and the rest as written.
fn strong(text: &str) -> (String, bool) {
    if let Some(inner) = text.strip_prefix("**").and_then(|t| t.strip_suffix("**")).filter(|t| !t.is_empty() && !t.contains("**")) {
        return (inner.to_owned(), true);
    }
    let heading = text.trim_start_matches('#');
    if heading.len() < text.len() && heading.starts_with(' ') && !text.contains('\n') {
        return (heading.trim().to_owned(), true);
    }
    (text.to_owned(), false)
}

#[cfg(test)]
mod tests;
