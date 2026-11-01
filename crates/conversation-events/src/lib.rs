//! The conversation event contract, decided on wheelhouse#548: AG-UI's
//! event set with its names and camelCase fields, plus our additions. Readers
//! skip unknown events and unknown fields; changes within a major version are
//! additive only.
use serde::{Deserialize, Serialize};
use serde_json::Value;

mod fold;
mod jsonl;

pub use fold::Fold;
pub use jsonl::{parse_line, read, to_line, write, LineError, Lines};

/// The contract's major version, which a stream's [`Hello`] carries.
pub const CONTRACT: u32 = 1;

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
    ReasoningStart(ReasoningStart),
    ReasoningMessageStart(ReasoningMessageStart),
    ReasoningMessageContent(ReasoningMessageContent),
    ReasoningMessageEnd(ReasoningMessageEnd),
    ReasoningEnd(ReasoningEnd),
    MessagesSnapshot(MessagesSnapshot),
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
            Event::ReasoningStart(e) => &e.thread_id,
            Event::ReasoningMessageStart(e) => &e.thread_id,
            Event::ReasoningMessageContent(e) => &e.thread_id,
            Event::ReasoningMessageEnd(e) => &e.thread_id,
            Event::ReasoningEnd(e) => &e.thread_id,
            Event::MessagesSnapshot(e) => &e.thread_id,
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
            Event::ReasoningStart(e) => e.seq,
            Event::ReasoningMessageStart(e) => e.seq,
            Event::ReasoningMessageContent(e) => e.seq,
            Event::ReasoningMessageEnd(e) => e.seq,
            Event::ReasoningEnd(e) => e.seq,
            Event::MessagesSnapshot(e) => e.seq,
            Event::ActivitySnapshot(e) => e.seq,
            Event::ActivityDelta(e) => e.seq,
            Event::Custom(e) => e.seq,
            Event::Raw(e) => e.seq,
        }
    }
}

/// A stream's first event.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Hello {
    pub contract: u32,
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
    /// Such as `completed`, `interrupted` or `aborted`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub outcome: Option<String>,
}

pub const COMPLETED: &str = "completed";

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
    Interrupted,
    AwaitingApproval,
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
    /// A short summary of the input, written by the producer.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<ToolKind>,
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
    /// [`MARKDOWN`], [`ANSI`], [`DIFF`], [`PLAIN`] or `image/*`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub media_type: Option<String>,
    /// Such as a diff's patch.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub structured: Option<Value>,
    #[serde(default)]
    pub truncated: Option<Truncated>,
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
