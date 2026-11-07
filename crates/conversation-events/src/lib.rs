//! The conversation event contract, decided on wheelhouse#548: AG-UI's
//! event set with its names and camelCase fields, plus our additions. Readers
//! skip unknown events and unknown fields; changes within a major version are
//! additive only.
use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};
use serde_json::Value;

mod fold;
mod jsonl;

pub use fold::{patch, Fold};
pub use jsonl::{parse_line, read, to_line, write, LineError, Lines};

/// The contract's major version, which a stream's [`Hello`] carries.
pub const CONTRACT: u32 = 2;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum Event {
    Hello(Hello),
    RunStarted(RunStarted),
    RunFinished(RunFinished),
    RunError(RunError),
    TextMessageStart(TextMessageStart),
    TextMessageContent(TextMessageContent),
    TextMessageEnd(TextMessageEnd),
    ToolCallStart(ToolCallStart),
    ToolCallArgs(ToolCallArgs),
    ToolCallEnd(ToolCallEnd),
    ToolCallResult(ToolCallResult),
    ToolCallOutput(ToolCallOutput),
    ReasoningStart(ReasoningStart),
    ReasoningMessageStart(ReasoningMessageStart),
    ReasoningMessageContent(ReasoningMessageContent),
    ReasoningMessageEnd(ReasoningMessageEnd),
    ReasoningEnd(ReasoningEnd),
    MessagesSnapshot(MessagesSnapshot),
    StateSnapshot(StateSnapshot),
    StateDelta(StateDelta),
    ActivitySnapshot(ActivitySnapshot),
    ActivityDelta(ActivityDelta),
    Custom(Custom),
    Raw(Raw),
    /// An event type this version doesn't know: skipped.
    #[serde(other)]
    Unknown,
}

impl Event {
    /// Empty for a Hello or an unknown event.
    pub fn thread_id(&self) -> &str {
        match self {
            Event::Hello(_) | Event::Unknown => "",
            Event::RunStarted(e) => &e.thread_id,
            Event::RunFinished(e) => &e.thread_id,
            Event::RunError(e) => &e.thread_id,
            Event::TextMessageStart(e) => &e.thread_id,
            Event::TextMessageContent(e) => &e.thread_id,
            Event::TextMessageEnd(e) => &e.thread_id,
            Event::ToolCallStart(e) => &e.thread_id,
            Event::ToolCallArgs(e) => &e.thread_id,
            Event::ToolCallEnd(e) => &e.thread_id,
            Event::ToolCallResult(e) => &e.thread_id,
            Event::ToolCallOutput(e) => &e.thread_id,
            Event::ReasoningStart(e) => &e.thread_id,
            Event::ReasoningMessageStart(e) => &e.thread_id,
            Event::ReasoningMessageContent(e) => &e.thread_id,
            Event::ReasoningMessageEnd(e) => &e.thread_id,
            Event::ReasoningEnd(e) => &e.thread_id,
            Event::MessagesSnapshot(e) => &e.thread_id,
            Event::StateSnapshot(e) => &e.thread_id,
            Event::StateDelta(e) => &e.thread_id,
            Event::ActivitySnapshot(e) => &e.thread_id,
            Event::ActivityDelta(e) => &e.thread_id,
            Event::Custom(e) => &e.thread_id,
            Event::Raw(e) => &e.thread_id,
        }
    }

    /// 0 for a Hello or an unknown event.
    pub fn seq(&self) -> u64 {
        match self {
            Event::Hello(_) | Event::Unknown => 0,
            Event::RunStarted(e) => e.seq,
            Event::RunFinished(e) => e.seq,
            Event::RunError(e) => e.seq,
            Event::TextMessageStart(e) => e.seq,
            Event::TextMessageContent(e) => e.seq,
            Event::TextMessageEnd(e) => e.seq,
            Event::ToolCallStart(e) => e.seq,
            Event::ToolCallArgs(e) => e.seq,
            Event::ToolCallEnd(e) => e.seq,
            Event::ToolCallResult(e) => e.seq,
            Event::ToolCallOutput(e) => e.seq,
            Event::ReasoningStart(e) => e.seq,
            Event::ReasoningMessageStart(e) => e.seq,
            Event::ReasoningMessageContent(e) => e.seq,
            Event::ReasoningMessageEnd(e) => e.seq,
            Event::ReasoningEnd(e) => e.seq,
            Event::MessagesSnapshot(e) => e.seq,
            Event::StateSnapshot(e) => e.seq,
            Event::StateDelta(e) => e.seq,
            Event::ActivitySnapshot(e) => e.seq,
            Event::ActivityDelta(e) => e.seq,
            Event::Custom(e) => e.seq,
            Event::Raw(e) => e.seq,
        }
    }
}

/// A stream's first event. A session log's has no thread and no
/// capabilities; a watch stream's names both (wheelhouse#691).
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Hello {
    pub contract: u32,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub thread_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub capabilities: Option<Capabilities>,
    /// Present when the events that follow start with a snapshot rather than
    /// resuming after the `seq` the watcher asked for.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub snapshot: Option<Snapshot>,
}

/// What a watcher of the thread may do.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Capabilities {
    /// Whether the thread takes input: a View shows its composer only then.
    #[serde(default)]
    pub input: bool,
    /// The input modes besides the producer's own choice: `steer`, `queue`.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub modes: Vec<String>,
    /// Whether the thread publishes a `commands` state document.
    #[serde(default)]
    pub commands: bool,
    /// The files the agent can reach.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub roots: Vec<Root>,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Root {
    pub path: String,
    /// Whether `path` is on the watcher's machine.
    #[serde(default)]
    pub local: bool,
    /// Where the root's files are searched (`GET {search}?q=…&limit=…`); a
    /// root without one offers no completion (wheelhouse#694).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub search: Option<String>,
}

/// A snapshot holds the newest completed items; these say what is older.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Snapshot {
    /// Completed items before the snapshot, for estimating their height.
    #[serde(default)]
    pub earlier_items: u64,
    /// The snapshot's oldest `seq`: `?before=` it pages back.
    #[serde(default)]
    pub oldest_seq: u64,
}

/// A page of older completed items (`?before=N&limit=…`), oldest first.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Page {
    #[serde(default)]
    pub events: Vec<Event>,
    #[serde(default)]
    pub earlier_items: u64,
    #[serde(default)]
    pub oldest_seq: u64,
}

/// A subagent's run names the tool call that started it.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Parent {
    pub thread_id: String,
    pub tool_call_id: String,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunStarted {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub run_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub parent_run_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub parent: Option<Parent>,
    /// The request the run started from, echoed so every watcher sees how
    /// an interrupt was answered.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub input: Option<RunAgentInput>,
}

/// AG-UI's `RunAgentInput`, as far as a watcher reads it.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunAgentInput {
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub messages: Vec<Message>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub resume: Vec<ResumeEntry>,
}

/// Something a run needs from outside before it can continue, such as an
/// approval or an answer (CONTEXT.md, "Interrupt").
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Interrupt {
    pub id: String,
    #[serde(default)]
    pub reason: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    /// Set when it asks to approve a tool call.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_call_id: Option<String>,
    /// A JSON Schema for the answer; a question's choices go here.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub response_schema: Option<Value>,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResumeEntry {
    pub interrupt_id: String,
    pub status: ResumeStatus,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub payload: Option<Value>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ResumeStatus {
    #[default]
    Resolved,
    Cancelled,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunFinished {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub run_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
    /// None means success.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub outcome: Option<Outcome>,
}

/// How a run ended, as AG-UI 1.0's `RunFinishedOutcome`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "lowercase")]
pub enum Outcome {
    #[serde(rename_all = "camelCase")]
    Success {
        #[serde(default, skip_serializing_if = "Vec::is_empty")]
        pending_tool_call_ids: Vec<String>,
    },
    /// The run waits for these; a new run resumes from them.
    Interrupt {
        #[serde(default)]
        interrupts: Vec<Interrupt>,
    },
    Cancelled,
    /// An outcome this version doesn't know, read as success as AG-UI says.
    /// Only read: written back, it would be `{"type":"unknown"}`.
    #[serde(other)]
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunError {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub message: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub code: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    Developer,
    System,
    #[default]
    Assistant,
    User,
    Tool,
    Reasoning,
    Activity,
    #[serde(other)]
    Other,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TextMessageStart {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
    #[serde(default)]
    pub role: Role,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub origin: Option<Origin>,
}

/// Who a message came from when it isn't the session's own user or agent
/// (#693): its kind and the producer's scalar attribution fields.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
pub struct Origin {
    pub kind: OriginKind,
    #[serde(flatten)]
    pub fields: BTreeMap<String, Value>,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Default, Serialize, Deserialize)]
#[serde(from = "String", into = "String")]
pub enum OriginKind {
    #[default]
    Human,
    Orchestrator,
    Peer,
    TaskNotification,
    /// A kind this version doesn't know, kept as written.
    Other(String),
}

impl From<String> for OriginKind {
    fn from(kind: String) -> Self {
        match kind.as_str() {
            "human" => OriginKind::Human,
            "orchestrator" => OriginKind::Orchestrator,
            "peer" => OriginKind::Peer,
            "task-notification" => OriginKind::TaskNotification,
            _ => OriginKind::Other(kind),
        }
    }
}

impl From<OriginKind> for String {
    fn from(kind: OriginKind) -> Self {
        match kind {
            OriginKind::Human => "human".into(),
            OriginKind::Orchestrator => "orchestrator".into(),
            OriginKind::Peer => "peer".into(),
            OriginKind::TaskNotification => "task-notification".into(),
            OriginKind::Other(kind) => kind,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TextMessageContent {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
    #[serde(default)]
    pub delta: String,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TextMessageEnd {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ToolKind {
    Shell,
    Edit,
    Read,
    Search,
    Agent,
    Web,
    Mcp,
    #[default]
    #[serde(other)]
    Other,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ToolStatus {
    Ok,
    Failed,
    Cancelled,
    /// A status this version doesn't know.
    #[serde(other)]
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCallStart {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub tool_call_id: String,
    #[serde(default)]
    pub tool_call_name: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub parent_message_id: Option<String>,
    /// A short summary of the input, written by the producer. A later
    /// `ToolCallEnd` or `ToolCallResult` may replace it, or `kind`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<ToolKind>,
    /// When the call started, in milliseconds since the Unix epoch, as
    /// AG-UI's `timestamp`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub timestamp: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCallArgs {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub tool_call_id: String,
    #[serde(default)]
    pub delta: String,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCallEnd {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub tool_call_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<ToolKind>,
}

/// Output past about 64 KiB is cut; the rest is in the native log.
#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Truncated {
    pub total_bytes: u64,
    /// A path or id in the native log.
    #[serde(rename = "ref", default)]
    pub reference: String,
}

pub const MARKDOWN: &str = "text/markdown";
pub const ANSI: &str = "text/x-ansi";
pub const DIFF: &str = "text/x-diff";
pub const PLAIN: &str = "text/plain";
/// [`SearchResult`]s in `structured`.
pub const SEARCH_RESULTS: &str = "application/vnd.flotilla.search-results+json";

#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct SearchResult {
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub url: String,
    #[serde(default)]
    pub domain: String,
    #[serde(default)]
    pub snippet: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub thumbnail: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCallResult {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub message_id: String,
    pub tool_call_id: String,
    #[serde(default)]
    pub content: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub role: Option<Role>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub status: Option<ToolStatus>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<ToolKind>,
    /// [`MARKDOWN`], [`ANSI`], [`DIFF`], [`PLAIN`] or `image/*`; `content`
    /// is always a readable fallback for `structured`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
    /// Such as a diff's patch.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub structured: Option<Value>,
    #[serde(default)]
    pub truncated: Option<Truncated>,
    /// When the result came, in milliseconds since the Unix epoch, as
    /// AG-UI's `timestamp`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub timestamp: Option<u64>,
}

/// Output while a call runs, in the stream form only: the fold drops it, and
/// the call's `ToolCallResult` replaces it.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCallOutput {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub tool_call_id: String,
    #[serde(default)]
    pub delta: String,
    /// Set on the first delta; a shell's defaults to [`ANSI`].
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReasoningStart {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
}

/// Reasoning carries summaries for live views; the stored projection omits
/// it, and encrypted content is never carried.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReasoningMessageStart {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub role: Option<Role>,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReasoningMessageContent {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
    #[serde(default)]
    pub delta: String,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReasoningMessageEnd {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReasoningEnd {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct FunctionCall {
    #[serde(default)]
    pub name: String,
    /// JSON, as a string.
    #[serde(default)]
    pub arguments: String,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCall {
    pub id: String,
    #[serde(rename = "type", default = "function")]
    pub call_type: String,
    #[serde(default)]
    pub function: FunctionCall,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<ToolKind>,
}

fn function() -> String {
    "function".into()
}

/// The record form: a completed item, or an open one with its content so
/// far, shaped like a MessagesSnapshot entry. Which fields apply follows the
/// role, as in AG-UI's message union.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Message {
    pub id: String,
    #[serde(default)]
    pub role: Role,
    /// A string, input parts (user), or an object (activity).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub content: Option<Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub origin: Option<Origin>,
    /// An assistant message's.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub tool_calls: Vec<ToolCall>,
    /// A tool message's, and the result fields after it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_call_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub status: Option<ToolStatus>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub structured: Option<Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub truncated: Option<Truncated>,
    /// An activity message's.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub activity_type: Option<String>,
}

impl Message {
    /// The content as text: a string as it is, input parts' text joined.
    pub fn text(&self) -> String {
        match &self.content {
            Some(Value::String(s)) => s.clone(),
            Some(Value::Array(parts)) => parts.iter().filter_map(|p| p.get("text").and_then(Value::as_str)).collect::<Vec<_>>().join("\n"),
            _ => String::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MessagesSnapshot {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub messages: Vec<Message>,
}

/// A state document of the thread, whole. No `key` is AG-UI's single
/// document; a keyed one (`status`, `commands`) is typed by `mediaType`.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StateSnapshot {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub key: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
    #[serde(default)]
    pub snapshot: Value,
}

/// `delta` is a JSON Patch (RFC 6902) against the document `key` names.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StateDelta {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub key: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
    #[serde(default)]
    pub delta: Vec<Value>,
}

/// The `status` document's key; its `snapshot` is one of the three below.
pub const STATUS: &str = "status";
pub const STATUS_IDLE: &str = "idle";
pub const STATUS_RUNNING: &str = "running";
pub const STATUS_REQUIRES_ACTION: &str = "requires_action";

/// Plans and to-dos.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActivitySnapshot {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
    #[serde(default)]
    pub activity_type: String,
    #[serde(default)]
    pub content: Value,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub replace: Option<bool>,
}

/// `patch` is a JSON Patch (RFC 6902) against the activity's content.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActivityDelta {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    pub message_id: String,
    #[serde(default)]
    pub activity_type: String,
    #[serde(default)]
    pub patch: Vec<Value>,
}

/// `compaction` is drawn as a Divider; Flotilla's own are under `flotilla.*`.
#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Custom {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub value: Value,
}

pub const COMPACTION: &str = "compaction";

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Raw {
    #[serde(default)]
    pub thread_id: String,
    #[serde(default)]
    pub seq: u64,
    #[serde(default)]
    pub event: Value,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
}

#[cfg(test)]
mod tests;
