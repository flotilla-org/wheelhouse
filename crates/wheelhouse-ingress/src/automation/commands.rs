//! The Automation Endpoint's commands (ADR 0014; #643): `GET /v1/commands`
//! lists the commands the palette lists and those that opt in, from the
//! command tables the UI gives once, and `POST /v1/commands/invoke` runs
//! one on the UI thread, with registers built from its target's Automation
//! ID or the caller's Caller Context. The request waits for the frame that
//! ran it, and answers with that frame's cursor.
use std::sync::{atomic::Ordering::SeqCst, Arc};

use axum::{
    extract::{rejection::JsonRejection, State},
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    Json,
};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use tokio::sync::oneshot;
use wheelhouse_core::{
    automation::{PanelAddress, ViewAddress, ViewOwner},
    WorkspaceId,
};

use super::{caller_views, cbor_wanted, encode, reply, Automation, FRAME_WAIT, MODE_UNKNOWN, SCHEMA};
use crate::{AUTOMATION_FULL, AUTOMATION_OFF};

/// `UIShell_CmdFlag_ListInUI`: the palette lists it.
pub const LIST_IN_UI: u32 = 1 << 0;
/// `UIShell_CmdFlag_ListInTab`: it opens a tab, in a panel.
pub const LIST_IN_TAB: u32 = 1 << 2;
/// `UIShell_CmdFlag_Automation`: listed here though the palette doesn't.
pub const AUTOMATION: u32 = 1 << 6;
/// The most invocations waiting for the UI; more get 503.
pub const MAX_QUEUED: usize = 64;

/// What a command's query slot takes, as its argument's name.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ArgKind {
    FilePath,
    String,
    Cursor,
    Cfg,
    /// A binary view's offset (`goto_address`).
    Address,
    /// Another command, as `command/<name>` (`run_command`, `open_tab`).
    Command,
}

impl ArgKind {
    /// Its name in a listing and in an invocation's `args`.
    pub fn name(self) -> &'static str {
        match self {
            ArgKind::FilePath => "file_path",
            ArgKind::String => "string",
            ArgKind::Cursor => "cursor",
            ArgKind::Cfg => "cfg",
            ArgKind::Address => "address",
            ArgKind::Command => "command",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Arg {
    pub kind: ArgKind,
    pub required: bool,
}

/// A command table entry, as the UI gives it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Command {
    pub name: String,
    pub display_name: String,
    pub description: String,
    /// Comma-separated, as the table has them.
    pub tags: String,
    /// The entry's context filter: `workspace`, `panel` or `view` for one
    /// that acts on a target, else empty.
    pub context: String,
    pub flags: u32,
    pub arg: Option<Arg>,
}

/// What a command acts on, which a target or the Caller Context names.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Targets {
    None,
    Workspace,
    Panel,
    View,
}

impl Command {
    pub fn listed(&self) -> bool {
        self.flags & (LIST_IN_UI | AUTOMATION) != 0
    }

    /// One that opens a tab opens it in a panel; otherwise its context says.
    pub fn targets(&self) -> Targets {
        match self.context.as_str() {
            "view" => Targets::View,
            "panel" => Targets::Panel,
            _ if self.flags & LIST_IN_TAB != 0 => Targets::Panel,
            "workspace" => Targets::Workspace,
            _ => Targets::None,
        }
    }

    fn entry(&self) -> Entry {
        Entry {
            id: format!("command/{}", self.name),
            display_name: self.display_name.clone(),
            description: self.description.clone(),
            tags: self.tags.split(',').map(str::trim).filter(|tag| !tag.is_empty()).map(str::to_owned).collect(),
            args: self.arg.iter().map(|arg| ArgEntry { name: arg.kind, required: arg.required }).collect(),
            targets: self.targets(),
        }
    }
}

/// `GET /v1/commands`.
#[derive(Debug, PartialEq, Serialize, Deserialize)]
pub struct Listing {
    pub schema: u32,
    pub commands: Vec<Entry>,
}

#[derive(Debug, PartialEq, Serialize, Deserialize)]
pub struct Entry {
    pub id: String,
    pub display_name: String,
    pub description: String,
    pub tags: Vec<String>,
    pub args: Vec<ArgEntry>,
    pub targets: Targets,
}

#[derive(Debug, PartialEq, Serialize, Deserialize)]
pub struct ArgEntry {
    pub name: ArgKind,
    pub required: bool,
}

/// The table's listed commands, in its order. A name a later table has
/// again is the first one's, as the UI looks commands up.
pub fn listing(table: &[Command]) -> Listing {
    let commands = table.iter().enumerate().filter(|(index, command)| command.listed() && first(table, *index)).map(|(_, c)| c.entry());
    Listing { schema: SCHEMA, commands: commands.collect() }
}

fn first(table: &[Command], index: usize) -> bool {
    !table[..index].iter().any(|earlier| earlier.name == table[index].name)
}

/// The command tables as the UI gives them, entry by entry.
#[derive(Default)]
pub struct Table {
    commands: Vec<Command>,
    malformed: bool,
}

impl Table {
    pub fn push(&mut self, command: Command) {
        self.commands.push(command);
    }

    /// An entry had text that wasn't UTF-8, or an argument this doesn't know.
    pub fn invalidate(&mut self) {
        self.malformed = true;
    }

    pub fn into_commands(self) -> Option<Vec<Command>> {
        (!self.malformed).then_some(self.commands)
    }
}

/// What an invocation's target names.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Target {
    Workspace(WorkspaceId),
    Window(WorkspaceId),
    Panel(PanelAddress),
    View(ViewAddress),
}

impl Target {
    pub fn from_automation_id(id: &str) -> Option<Target> {
        if let Some(view) = ViewAddress::from_automation_id(id) {
            return Some(Target::View(view));
        }
        if let Some(panel) = PanelAddress::from_automation_id(id) {
            return Some(Target::Panel(panel));
        }
        match ViewOwner::from_automation_id(id)? {
            ViewOwner::Workspace(id) => Some(Target::Workspace(id)),
            ViewOwner::Window(id) => Some(Target::Window(id)),
        }
    }

    pub fn automation_id(&self) -> String {
        match self {
            Target::Workspace(id) => ViewOwner::Workspace(*id).automation_id(),
            Target::Window(id) => ViewOwner::Window(*id).automation_id(),
            Target::Panel(panel) => panel.automation_id(),
            Target::View(view) => view.automation_id(),
        }
    }

    /// Whether a command that acts on `targets` can act on this: a panel's
    /// on its Selected View, and a View's on its panel.
    fn serves(&self, targets: Targets) -> bool {
        match targets {
            Targets::None | Targets::Workspace => true,
            Targets::Panel | Targets::View => matches!(self, Target::Panel(_) | Target::View(_)),
        }
    }
}

/// An argument's value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ArgValue {
    /// A file path, a string, a cfg's name or a command's.
    Text(String),
    Cursor {
        line: u64,
        column: u64,
    },
    Address(u64),
}

/// An argument as given: what it is, and its value.
pub type Given = (ArgKind, ArgValue);

/// A command to run, on what, with what.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Invocation {
    pub command: String,
    pub target: Option<Target>,
    pub arg: Option<Given>,
}

/// Why the endpoint or the UI wouldn't run one, as its status and reason.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Refusal(pub StatusCode, pub String);

impl Refusal {
    fn new(status: StatusCode, reason: impl Into<String>) -> Self {
        Refusal(status, reason.into())
    }
}

impl IntoResponse for Refusal {
    fn into_response(self) -> Response {
        (self.0, [("cache-control", "no-store")], self.1).into_response()
    }
}

/// `POST /v1/commands/invoke`'s body.
#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Invoke {
    pub command: String,
    #[serde(default)]
    pub target: Option<String>,
    #[serde(default)]
    pub session: Option<String>,
    #[serde(default)]
    pub args: Map<String, Value>,
}

/// The command `body` names and its argument, checked against `table`;
/// the target is resolved apart, as the Caller Context needs the tree.
pub fn plan<'a>(table: &'a [Command], body: &Invoke) -> Result<(&'a Command, Option<Given>), Refusal> {
    let unknown = || Refusal::new(StatusCode::NOT_FOUND, format!("no command {}", body.command));
    let name = body.command.strip_prefix("command/").ok_or_else(unknown)?;
    let command = table.iter().find(|command| command.name == name).ok_or_else(unknown)?;
    if !command.listed() {
        return Err(Refusal::new(
            StatusCode::FORBIDDEN,
            format!("{} is not invocable: neither the palette nor automation lists it", body.command),
        ));
    }
    let unprocessable = |reason: String| Refusal::new(StatusCode::UNPROCESSABLE_ENTITY, reason);
    let name = command.arg.map(|arg| arg.kind.name());
    if let Some(extra) = body.args.keys().find(|key| Some(key.as_str()) != name) {
        return Err(unprocessable(format!("{} takes no argument {extra}", body.command)));
    }
    let (Some(arg), Some(name)) = (command.arg, name) else {
        return Ok((command, None));
    };
    let Some(value) = body.args.get(name) else {
        return match arg.required {
            true => Err(unprocessable(format!("{} needs its argument {name}", body.command))),
            false => Ok((command, None)),
        };
    };
    let bad = || unprocessable(format!("{}'s argument {name} is malformed", body.command));
    let value = match arg.kind {
        ArgKind::FilePath | ArgKind::String | ArgKind::Cfg => ArgValue::Text(value.as_str().ok_or_else(bad)?.to_owned()),
        // The command it runs is held to what invoking it would be: listed,
        // and with no argument it would need, as it gets none.
        ArgKind::Command => {
            let named = value.as_str().and_then(|v| v.strip_prefix("command/")).ok_or_else(bad)?;
            let Some(inner) = table.iter().find(|command| command.name == named) else {
                return Err(Refusal::new(StatusCode::NOT_FOUND, format!("no command command/{named}")));
            };
            if !inner.listed() {
                return Err(Refusal::new(
                    StatusCode::FORBIDDEN,
                    format!("command/{named} is not invocable: neither the palette nor automation lists it"),
                ));
            }
            if inner.arg.is_some_and(|arg| arg.required) {
                return Err(unprocessable(format!("command/{named} needs an argument; invoke it itself")));
            }
            ArgValue::Text(named.to_owned())
        }
        ArgKind::Address => ArgValue::Address(value.as_u64().ok_or_else(bad)?),
        ArgKind::Cursor => {
            #[derive(Deserialize)]
            #[serde(deny_unknown_fields)]
            struct Cursor {
                line: u64,
                #[serde(default = "first_column")]
                column: u64,
            }
            fn first_column() -> u64 {
                1
            }
            let cursor: Cursor = serde_json::from_value(value.clone()).map_err(|_| bad())?;
            // The UI's text points are signed.
            if cursor.line == 0 || cursor.column == 0 || i64::try_from(cursor.line.max(cursor.column)).is_err() {
                return Err(bad());
            }
            ArgValue::Cursor { line: cursor.line, column: cursor.column }
        }
    };
    Ok((command, Some((arg.kind, value))))
}

/// The target `body` names, or else the one View its Caller Context
/// shows. A command that acts on nothing runs without one when neither
/// says.
fn target(named: Option<Target>, targets: Targets, context: Option<Vec<String>>) -> Result<Option<Target>, Refusal> {
    if let Some(target) = named {
        return Ok(Some(target));
    }
    let views = context.unwrap_or_default();
    match (views.as_slice(), targets) {
        ([view], _) => Ok(ViewAddress::from_automation_id(view).map(Target::View)),
        (_, Targets::None) => Ok(None),
        ([], _) => Err(Refusal::new(StatusCode::BAD_REQUEST, "it needs a target, and no Caller Context shows the session")),
        _ => Err(Refusal::new(StatusCode::CONFLICT, format!("the Caller Context shows several views: {}", views.join(", ")))),
    }
}

/// An invocation the UI has yet to take, and where its answer goes.
pub struct Queued {
    pub invocation: Invocation,
    pub reply: oneshot::Sender<Result<String, Refusal>>,
}

impl Automation {
    /// Whether the setting lets a client act: off is 404, as for the reads;
    /// observe is 403. Before any frame has said, one is waited for.
    async fn acting(self: &Arc<Self>) -> Result<(), Refusal> {
        if self.mode.load(SeqCst) == MODE_UNKNOWN {
            self.fresh(false).await.map_err(|status| Refusal::new(status, ""))?;
        }
        match self.mode.load(SeqCst) {
            AUTOMATION_OFF => Err(Refusal::new(StatusCode::NOT_FOUND, "")),
            AUTOMATION_FULL => Ok(()),
            _ => Err(Refusal::new(StatusCode::FORBIDDEN, "automation is observe: commands and intents need automation: full")),
        }
    }
}

pub async fn commands(State(automation): State<Arc<Automation>>, headers: HeaderMap) -> Response {
    let cbor = cbor_wanted(&headers);
    if let Err(refusal) = automation.acting().await {
        return refusal.into_response();
    }
    match automation.commands.get() {
        Some(table) => reply(cbor, encode(&listing(table), cbor)),
        None => Refusal::new(StatusCode::SERVICE_UNAVAILABLE, "the UI has given no command tables").into_response(),
    }
}

#[derive(Serialize)]
struct Ran {
    cursor: String,
}

pub async fn invoke(State(automation): State<Arc<Automation>>, headers: HeaderMap, body: Result<Json<Invoke>, JsonRejection>) -> Response {
    let cbor = cbor_wanted(&headers);
    match invoked(&automation, body).await {
        Ok(cursor) => reply(cbor, encode(&Ran { cursor }, cbor)),
        Err(refusal) => refusal.into_response(),
    }
}

async fn invoked(automation: &Arc<Automation>, body: Result<Json<Invoke>, JsonRejection>) -> Result<String, Refusal> {
    automation.acting().await?;
    let Json(body) = body.map_err(|rejection| Refusal::new(rejection.status(), rejection.body_text()))?;
    let table =
        automation.commands.get().ok_or_else(|| Refusal::new(StatusCode::SERVICE_UNAVAILABLE, "the UI has given no command tables"))?;
    let (command, arg) = plan(table, &body)?;
    let targets = command.targets();
    let named = match &body.target {
        Some(id) => Some(
            Target::from_automation_id(id)
                .ok_or_else(|| Refusal::new(StatusCode::UNPROCESSABLE_ENTITY, format!("no target can be {id}")))?,
        ),
        None => None,
    };
    if let Some(named) = named.as_ref().filter(|named| !named.serves(targets)) {
        return Err(Refusal::new(
            StatusCode::UNPROCESSABLE_ENTITY,
            format!("{} acts on a panel or a view, not {}", body.command, named.automation_id()),
        ));
    }
    // The Caller Context, resolved as `self` does, only when needed.
    let context = match (&named, body.session.as_deref().filter(|session| !session.is_empty())) {
        (None, Some(session)) => {
            let published = automation.fresh(false).await.map_err(|status| Refusal::new(status, ""))?;
            let all = published.tree.as_deref().map_or(&[][..], Vec::as_slice);
            caller_views(all, session, None).map(|views| views.into_iter().map(|view| view.id).collect())
        }
        _ => None,
    };
    let target = target(named, targets, context)?;
    let invocation = Invocation { command: command.name.clone(), target, arg };
    let answer = enqueue(automation, invocation)?;
    (automation.wake)();
    // Taken at one frame's end, run in the next, answered at its end.
    match tokio::time::timeout(FRAME_WAIT, answer).await {
        Ok(Ok(answer)) => answer,
        _ => Err(Refusal::new(StatusCode::SERVICE_UNAVAILABLE, "the UI didn't run it in time")),
    }
}

/// `invocation` queued for the UI, and where its answer will come; refused
/// when the queue is full of clients that haven't gone.
fn enqueue(automation: &Automation, invocation: Invocation) -> Result<oneshot::Receiver<Result<String, Refusal>>, Refusal> {
    let (reply, answer) = oneshot::channel();
    let mut queue = automation.invocations.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    // Those whose client has gone hold no place.
    if queue.len() >= MAX_QUEUED {
        queue.retain(|queued| !queued.reply.is_closed());
    }
    if queue.len() >= MAX_QUEUED {
        return Err(Refusal::new(StatusCode::SERVICE_UNAVAILABLE, "too many invocations are waiting"));
    }
    queue.push(Queued { invocation, reply });
    Ok(answer)
}

/// The UI's side of invocation.
pub trait Host {
    /// Queue `invocation` to run in the next frame, under `ticket`; or why not.
    fn invoke(&mut self, ticket: u64, invocation: &Invocation) -> Result<(), Refusal>;
    /// After the frame `ticket` ran in: whether it ran, or what refused it.
    fn settle(&mut self, ticket: u64) -> Result<(), Refusal>;
}

/// Invocations the UI is running: queued at one frame's end, settled at
/// the next.
#[derive(Default)]
pub struct Running {
    next: u64,
    running: Vec<(u64, oneshot::Sender<Result<String, Refusal>>)>,
}

impl Running {
    /// Whether this frame ran any, so its end must build the tree for a
    /// cursor that follows them.
    pub fn any(&self) -> bool {
        !self.running.is_empty()
    }

    /// At a frame's end, once its tree is published as `generation`: answer
    /// those this frame ran with its cursor, then queue those waiting for
    /// the next.
    pub fn frame_end(&mut self, automation: &Automation, mode: u32, generation: u64, host: &mut impl Host) {
        for (ticket, reply) in self.running.drain(..) {
            let _ = reply.send(host.settle(ticket).map(|()| automation.cursor(generation)));
        }
        let queued = std::mem::take(&mut *automation.invocations.lock().unwrap_or_else(|poisoned| poisoned.into_inner()));
        for Queued { invocation, reply } in queued {
            // A client that has gone has its command left unrun.
            if reply.is_closed() {
                continue;
            }
            let refused = match mode {
                AUTOMATION_FULL => None,
                AUTOMATION_OFF => Some(Refusal::new(StatusCode::NOT_FOUND, "")),
                _ => Some(Refusal::new(StatusCode::FORBIDDEN, "automation is observe: commands and intents need automation: full")),
            };
            self.next += 1;
            match refused.map_or_else(|| host.invoke(self.next, &invocation), Err) {
                Ok(()) => self.running.push((self.next, reply)),
                Err(refusal) => {
                    let _ = reply.send(Err(refusal));
                }
            }
        }
        if self.any() {
            (automation.wake)();
        }
    }
}

#[cfg(test)]
mod tests;
