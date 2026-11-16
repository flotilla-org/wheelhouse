//! The Automation Endpoint's intents (ADR 0014's amendment; #643):
//! `POST /v1/intents/apply` applies one of the Core's intents by its stable
//! name, with Automation IDs for its owner, panels and Views, and answers
//! with the receipt: accepted with its request and the cursor of the frame
//! it shows in, or refused with why. The UI applies it at a frame's end; the
//! answer waits for the next frame's end, by when it is committed.
use std::sync::Arc;

use axum::{
    extract::{rejection::JsonRejection, State},
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    Json,
};
use serde::{Deserialize, Serialize};
use wheelhouse_core::{
    arrangement::{Dir, Owner, OwnerKind},
    automation::{PanelAddress, ViewAddress, ViewOwner},
    EntityRef,
};

use super::{
    cbor_wanted,
    commands::{queue, Refusal, Work},
    encode, reply, Automation,
};

/// A side, as an intent's `dir` names it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Side {
    Left,
    Up,
    Right,
    Down,
}

impl From<Side> for Dir {
    fn from(side: Side) -> Dir {
        match side {
            Side::Left => Dir::Left,
            Side::Up => Dir::Up,
            Side::Right => Dir::Right,
            Side::Down => Dir::Down,
        }
    }
}

impl From<Dir> for Side {
    fn from(dir: Dir) -> Side {
        match dir {
            Dir::Left => Side::Left,
            Dir::Up => Side::Up,
            Dir::Right => Side::Right,
            Dir::Down => Side::Down,
        }
    }
}

/// `POST /v1/intents/apply`'s body: the intent's stable name, its owner, and
/// its fields, every panel, tab and row by its Automation ID.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "intent", rename_all = "kebab-case", deny_unknown_fields)]
pub enum Body {
    SelectTab {
        owner: String,
        panel: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        tab: Option<String>,
    },
    MoveTab {
        owner: String,
        tab: String,
        to: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        after: Option<String>,
    },
    CloseTab {
        owner: String,
        tab: String,
    },
    DropTab {
        owner: String,
        tab: String,
        to: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        dir: Option<Side>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        after: Option<String>,
    },
    SplitPanel {
        owner: String,
        panel: String,
        dir: Side,
    },
    ClosePanel {
        owner: String,
        panel: String,
    },
    ReorderPanel {
        owner: String,
        panel: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        after: Option<String>,
    },
    ResizePanel {
        owner: String,
        panel: String,
        delta: f32,
        #[serde(default)]
        floor: f32,
    },
    EqualizePanel {
        owner: String,
        panel: String,
    },
    CopySubjectUrl {
        owner: String,
        entity: EntityRef,
    },
    ActivatePlacement {
        owner: String,
        key: String,
    },
    Activate {
        owner: String,
        entity: EntityRef,
    },
    TogglePlacement {
        owner: String,
        key: String,
    },
    SetVariable {
        owner: String,
        key: String,
        name: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        value: Option<String>,
    },
    ToggleDisplayVariable {
        owner: String,
        name: String,
    },
}

/// An arrangement intent (`arrangement::Intent`), its panels by their ID in
/// the owner's arrangement as the Semantic Tree gives them: a number, or the
/// word a provider's document named it by, which only the UI can resolve.
#[derive(Clone, Debug, PartialEq)]
pub enum Arrangement {
    SelectTab { panel: String, tab: Option<ViewAddress> },
    MoveTab { tab: ViewAddress, to: String, after: Option<ViewAddress> },
    CloseTab { tab: ViewAddress },
    DropTab { tab: ViewAddress, to: String, dir: Option<Dir>, after: Option<ViewAddress> },
    SplitPanel { panel: String, dir: Dir },
    ClosePanel { panel: String },
    ReorderPanel { panel: String, after: Option<String> },
    ResizePanel { panel: String, delta: f32, floor: f32 },
    EqualizePanel { panel: String },
}

/// A sidebar row, `section/<region-key>/row/<node-key>`: the core keys
/// intents by the node key (a placement's encoded key).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Row {
    pub region: String,
    pub key: String,
}

impl Row {
    pub fn from_automation_id(id: &str) -> Option<Row> {
        let (region, key) = id.strip_prefix("section/")?.split_once("/row/")?;
        (!region.is_empty() && !key.is_empty()).then(|| Row { region: region.to_owned(), key: key.to_owned() })
    }

    pub fn automation_id(&self) -> String {
        format!("section/{}/row/{}", self.region, self.key)
    }
}

/// A sidebar intent (`sidebar::Intent`), its rows by Automation ID.
#[derive(Clone, Debug, PartialEq)]
pub enum Sidebar {
    CopySubjectUrl { entity: EntityRef },
    ActivatePlacement { row: Row },
    Activate { entity: EntityRef },
    TogglePlacement { row: Row },
    SetVariable { row: Row, name: String, value: Option<String> },
    ToggleDisplayVariable { name: String },
}

/// An intent and the owner it is for: an arrangement's, or a window's
/// sidebar (named by the window or by a workspace it shows).
#[derive(Clone, Debug, PartialEq)]
pub enum Intent {
    Arrangement { owner: Owner, intent: Arrangement },
    Sidebar { owner: ViewOwner, intent: Sidebar },
}

impl Arrangement {
    /// Its stable name, the Core's (`arrangement::Intent::name`).
    pub fn name(&self) -> &'static str {
        match self {
            Arrangement::SelectTab { .. } => "select-tab",
            Arrangement::MoveTab { .. } => "move-tab",
            Arrangement::CloseTab { .. } => "close-tab",
            Arrangement::DropTab { .. } => "drop-tab",
            Arrangement::SplitPanel { .. } => "split-panel",
            Arrangement::ClosePanel { .. } => "close-panel",
            Arrangement::ReorderPanel { .. } => "reorder-panel",
            Arrangement::ResizePanel { .. } => "resize-panel",
            Arrangement::EqualizePanel { .. } => "equalize-panel",
        }
    }
}

impl Sidebar {
    /// Its stable name, the Core's (`sidebar::Intent::name`).
    pub fn name(&self) -> &'static str {
        match self {
            Sidebar::CopySubjectUrl { .. } => "copy-subject-url",
            Sidebar::ActivatePlacement { .. } => "activate-placement",
            Sidebar::Activate { .. } => "activate",
            Sidebar::TogglePlacement { .. } => "toggle-placement",
            Sidebar::SetVariable { .. } => "set-variable",
            Sidebar::ToggleDisplayVariable { .. } => "toggle-display-variable",
        }
    }
}

/// An arrangement's owner from its Automation ID: a workspace's, or for
/// `window/<uuid>` its sidebar's dock.
fn arrangement_owner(id: &str) -> Result<Owner, String> {
    match ViewOwner::from_automation_id(id) {
        Some(ViewOwner::Workspace(id)) => Ok(Owner { kind: OwnerKind::Workspace, id }),
        Some(ViewOwner::Window(id)) => Ok(Owner { kind: OwnerKind::Sidebar, id }),
        None => Err(format!("{id} is no workspace or window")),
    }
}

fn view_owner(owner: Owner) -> ViewOwner {
    match owner.kind {
        OwnerKind::Workspace | OwnerKind::Dock => ViewOwner::Workspace(owner.id),
        OwnerKind::Sidebar => ViewOwner::Window(owner.id),
    }
}

impl Intent {
    /// The intent `body` names, its Automation IDs mapped to the Core's
    /// types; or why it names none. A panel must be its owner's, as must a
    /// tab, but a dropped one, which may come from another arrangement.
    pub fn from_body(body: &Body) -> Result<Intent, String> {
        let panel = |owner: Owner, id: &str| match PanelAddress::from_automation_id(id) {
            Some(panel) if panel.owner == view_owner(owner) => Ok(panel.id),
            Some(_) => Err(format!("{id} isn't {}'s", view_owner(owner).automation_id())),
            None => Err(format!("{id} is no panel")),
        };
        let view = |id: &str| ViewAddress::from_automation_id(id).ok_or_else(|| format!("{id} is no view"));
        let tab = |owner: Owner, id: &str| match view(id)? {
            tab if tab.owner == view_owner(owner) => Ok(tab),
            _ => Err(format!("{id} isn't {}'s", view_owner(owner).automation_id())),
        };
        let row = |id: &str| Row::from_automation_id(id).ok_or_else(|| format!("{id} is no sidebar row"));
        let sidebar = |id: &str| ViewOwner::from_automation_id(id).ok_or_else(|| format!("{id} is no workspace or window"));
        let arranged = |owner: &str, intent: &dyn Fn(Owner) -> Result<Arrangement, String>| {
            let owner = arrangement_owner(owner)?;
            Ok(Intent::Arrangement { owner, intent: intent(owner)? })
        };
        match body {
            Body::SelectTab { owner, panel: p, tab: t } => {
                arranged(owner, &|o| Ok(Arrangement::SelectTab { panel: panel(o, p)?, tab: t.as_deref().map(|t| tab(o, t)).transpose()? }))
            }
            Body::MoveTab { owner, tab: t, to, after } => arranged(owner, &|o| {
                let after = after.as_deref().map(|a| tab(o, a)).transpose()?;
                Ok(Arrangement::MoveTab { tab: tab(o, t)?, to: panel(o, to)?, after })
            }),
            Body::CloseTab { owner, tab: t } => arranged(owner, &|o| Ok(Arrangement::CloseTab { tab: tab(o, t)? })),
            Body::DropTab { owner, tab: t, to, dir, after } => arranged(owner, &|o| {
                let after = after.as_deref().map(|a| tab(o, a)).transpose()?;
                Ok(Arrangement::DropTab { tab: view(t)?, to: panel(o, to)?, dir: dir.map(Dir::from), after })
            }),
            Body::SplitPanel { owner, panel: p, dir } => {
                arranged(owner, &|o| Ok(Arrangement::SplitPanel { panel: panel(o, p)?, dir: (*dir).into() }))
            }
            Body::ClosePanel { owner, panel: p } => arranged(owner, &|o| Ok(Arrangement::ClosePanel { panel: panel(o, p)? })),
            Body::ReorderPanel { owner, panel: p, after } => arranged(owner, &|o| {
                Ok(Arrangement::ReorderPanel { panel: panel(o, p)?, after: after.as_deref().map(|a| panel(o, a)).transpose()? })
            }),
            Body::ResizePanel { owner, panel: p, delta, floor } => {
                if !delta.is_finite() || !floor.is_finite() {
                    return Err("delta and floor must be finite".into());
                }
                arranged(owner, &|o| Ok(Arrangement::ResizePanel { panel: panel(o, p)?, delta: *delta, floor: *floor }))
            }
            Body::EqualizePanel { owner, panel: p } => arranged(owner, &|o| Ok(Arrangement::EqualizePanel { panel: panel(o, p)? })),
            Body::CopySubjectUrl { owner, entity } => {
                Ok(Intent::Sidebar { owner: sidebar(owner)?, intent: Sidebar::CopySubjectUrl { entity: entity.clone() } })
            }
            Body::ActivatePlacement { owner, key } => {
                Ok(Intent::Sidebar { owner: sidebar(owner)?, intent: Sidebar::ActivatePlacement { row: row(key)? } })
            }
            Body::Activate { owner, entity } => {
                Ok(Intent::Sidebar { owner: sidebar(owner)?, intent: Sidebar::Activate { entity: entity.clone() } })
            }
            Body::TogglePlacement { owner, key } => {
                Ok(Intent::Sidebar { owner: sidebar(owner)?, intent: Sidebar::TogglePlacement { row: row(key)? } })
            }
            Body::SetVariable { owner, key, name, value } => Ok(Intent::Sidebar {
                owner: sidebar(owner)?,
                intent: Sidebar::SetVariable { row: row(key)?, name: name.clone(), value: value.clone() },
            }),
            Body::ToggleDisplayVariable { owner, name } => {
                Ok(Intent::Sidebar { owner: sidebar(owner)?, intent: Sidebar::ToggleDisplayVariable { name: name.clone() } })
            }
        }
    }

    /// The body that names this intent, its IDs as Automation IDs.
    pub fn body(&self) -> Body {
        match self {
            Intent::Arrangement { owner, intent } => {
                let o = view_owner(*owner);
                let owner = o.automation_id();
                let panel = |id: &String| PanelAddress { owner: o, id: id.clone() }.automation_id();
                let view = |view: &ViewAddress| view.automation_id();
                match intent {
                    Arrangement::SelectTab { panel: p, tab } => Body::SelectTab { owner, panel: panel(p), tab: tab.as_ref().map(view) },
                    Arrangement::MoveTab { tab, to, after } => {
                        Body::MoveTab { owner, tab: view(tab), to: panel(to), after: after.as_ref().map(view) }
                    }
                    Arrangement::CloseTab { tab } => Body::CloseTab { owner, tab: view(tab) },
                    Arrangement::DropTab { tab, to, dir, after } => {
                        Body::DropTab { owner, tab: view(tab), to: panel(to), dir: dir.map(Side::from), after: after.as_ref().map(view) }
                    }
                    Arrangement::SplitPanel { panel: p, dir } => Body::SplitPanel { owner, panel: panel(p), dir: (*dir).into() },
                    Arrangement::ClosePanel { panel: p } => Body::ClosePanel { owner, panel: panel(p) },
                    Arrangement::ReorderPanel { panel: p, after } => {
                        Body::ReorderPanel { owner, panel: panel(p), after: after.as_ref().map(panel) }
                    }
                    Arrangement::ResizePanel { panel: p, delta, floor } => {
                        Body::ResizePanel { owner, panel: panel(p), delta: *delta, floor: *floor }
                    }
                    Arrangement::EqualizePanel { panel: p } => Body::EqualizePanel { owner, panel: panel(p) },
                }
            }
            Intent::Sidebar { owner, intent } => {
                let owner = owner.automation_id();
                match intent {
                    Sidebar::CopySubjectUrl { entity } => Body::CopySubjectUrl { owner, entity: entity.clone() },
                    Sidebar::ActivatePlacement { row } => Body::ActivatePlacement { owner, key: row.automation_id() },
                    Sidebar::Activate { entity } => Body::Activate { owner, entity: entity.clone() },
                    Sidebar::TogglePlacement { row } => Body::TogglePlacement { owner, key: row.automation_id() },
                    Sidebar::SetVariable { row, name, value } => {
                        Body::SetVariable { owner, key: row.automation_id(), name: name.clone(), value: value.clone() }
                    }
                    Sidebar::ToggleDisplayVariable { name } => Body::ToggleDisplayVariable { owner, name: name.clone() },
                }
            }
        }
    }

    pub fn name(&self) -> &'static str {
        match self {
            Intent::Arrangement { intent, .. } => intent.name(),
            Intent::Sidebar { intent, .. } => intent.name(),
        }
    }
}

/// The Core's receipt, with the cursor of the frame an accepted intent
/// shows in.
#[derive(Debug, PartialEq, Serialize, Deserialize)]
pub struct Receipt {
    pub accepted: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub request: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cursor: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}

pub async fn apply(State(automation): State<Arc<Automation>>, headers: HeaderMap, body: Result<Json<Body>, JsonRejection>) -> Response {
    let cbor = cbor_wanted(&headers);
    let refused = |reason: String| {
        let receipt = Receipt { accepted: false, request: None, cursor: None, reason: Some(reason) };
        (StatusCode::UNPROCESSABLE_ENTITY, reply(cbor, encode(&receipt, cbor))).into_response()
    };
    if let Err(refusal) = automation.acting().await {
        return refusal.into_response();
    }
    let body = match body {
        Ok(Json(body)) => body,
        Err(rejection) if rejection.status() == StatusCode::UNPROCESSABLE_ENTITY => return refused(rejection.body_text()),
        Err(rejection) => return Refusal::new(rejection.status(), rejection.body_text()).into_response(),
    };
    let intent = match Intent::from_body(&body) {
        Ok(intent) => intent,
        Err(reason) => return refused(reason),
    };
    match queue(&automation, Work::Apply(intent)).await {
        Ok(done) => reply(cbor, encode(&Receipt { accepted: true, request: done.request, cursor: Some(done.cursor), reason: None }, cbor)),
        Err(Refusal(StatusCode::UNPROCESSABLE_ENTITY, reason)) => refused(reason),
        Err(refusal) => refusal.into_response(),
    }
}

#[cfg(test)]
mod tests;
