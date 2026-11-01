//! From the stream form to the record form (#548, decision 1).
use std::collections::{HashMap, HashSet};

use serde_json::Value;

use super::{Event, FunctionCall, Message, MessagesSnapshot, Role, ToolCall};

#[derive(Debug, Default)]
struct Thread {
    messages: Vec<Message>,
    index: HashMap<String, usize>,
    /// A tool call's message and place among its calls.
    calls: HashMap<String, (usize, usize)>,
    open: HashSet<String>,
    seq: u64,
}

impl Thread {
    fn upsert(&mut self, id: &str, role: Role) -> &mut Message {
        let at = match self.index.get(id) {
            Some(&at) => at,
            None => {
                self.messages.push(Message { id: id.to_owned(), role, ..Message::default() });
                self.index.insert(id.to_owned(), self.messages.len() - 1);
                self.messages.len() - 1
            }
        };
        &mut self.messages[at]
    }

    fn append(&mut self, id: &str, delta: &str) {
        if let Some(&at) = self.index.get(id) {
            match &mut self.messages[at].content {
                Some(Value::String(s)) => s.push_str(delta),
                content => *content = Some(Value::String(delta.to_owned())),
            }
        }
    }

    fn replace(&mut self, messages: &[Message]) {
        self.messages = messages.to_vec();
        self.index = self.messages.iter().enumerate().map(|(at, m)| (m.id.clone(), at)).collect();
        self.calls.clear();
        for (at, message) in self.messages.iter().enumerate() {
            for (call, tool) in message.tool_calls.iter().enumerate() {
                self.calls.insert(tool.id.clone(), (at, call));
            }
        }
        self.open.clear();
    }
}

/// Folds stream events, per thread, into messages in the record form.
#[derive(Debug, Default)]
pub struct Fold {
    threads: Vec<(String, Thread)>,
    index: HashMap<String, usize>,
}

impl Fold {
    fn thread(&mut self, id: &str) -> &mut Thread {
        let at = *self.index.entry(id.to_owned()).or_insert_with(|| {
            self.threads.push((id.to_owned(), Thread::default()));
            self.threads.len() - 1
        });
        &mut self.threads[at].1
    }

    pub fn push(&mut self, event: &Event) {
        if matches!(event, Event::Hello(_) | Event::Unknown) {
            return;
        }
        let thread = self.thread(event.thread_id());
        thread.seq = thread.seq.max(event.seq());
        match event {
            Event::TextMessageStart(e) => {
                thread.upsert(&e.message_id, e.role).role = e.role;
                thread.append(&e.message_id, "");
                thread.open.insert(e.message_id.clone());
            }
            Event::TextMessageContent(e) => thread.append(&e.message_id, &e.delta),
            Event::TextMessageEnd(e) => {
                thread.open.remove(&e.message_id);
            }
            Event::ReasoningMessageStart(e) => {
                thread.upsert(&e.message_id, Role::Reasoning);
                thread.append(&e.message_id, "");
                thread.open.insert(e.message_id.clone());
            }
            Event::ReasoningMessageContent(e) => thread.append(&e.message_id, &e.delta),
            Event::ReasoningMessageEnd(e) => {
                thread.open.remove(&e.message_id);
            }
            Event::ToolCallStart(e) => {
                // A call belongs to an assistant message; one without a parent
                // gets a message of its own.
                let parent = e.parent_message_id.as_deref().unwrap_or(&e.tool_call_id);
                thread.upsert(parent, Role::Assistant);
                let at = thread.index[parent];
                let calls = &mut thread.messages[at].tool_calls;
                let call = match calls.iter().position(|c| c.id == e.tool_call_id) {
                    Some(call) => call,
                    None => {
                        calls.push(ToolCall { id: e.tool_call_id.clone(), call_type: "function".into(), ..ToolCall::default() });
                        calls.len() - 1
                    }
                };
                let tool = &mut calls[call];
                tool.function = FunctionCall { name: e.tool_call_name.clone(), arguments: String::new() };
                tool.title.clone_from(&e.title);
                tool.kind = e.kind;
                thread.calls.insert(e.tool_call_id.clone(), (at, call));
                thread.open.insert(e.tool_call_id.clone());
            }
            Event::ToolCallArgs(e) => {
                if let Some(&(at, call)) = thread.calls.get(&e.tool_call_id) {
                    thread.messages[at].tool_calls[call].function.arguments.push_str(&e.delta);
                }
            }
            Event::ToolCallEnd(e) => {
                thread.open.remove(&e.tool_call_id);
            }
            Event::ToolCallResult(e) => {
                let id = if e.message_id.is_empty() { format!("result:{}", e.tool_call_id) } else { e.message_id.clone() };
                let message = thread.upsert(&id, Role::Tool);
                message.role = Role::Tool;
                message.content = Some(Value::String(e.content.clone()));
                message.tool_call_id = Some(e.tool_call_id.clone());
                message.status = e.status;
                message.media_type.clone_from(&e.media_type);
                message.structured.clone_from(&e.structured);
                message.truncated.clone_from(&e.truncated);
            }
            Event::MessagesSnapshot(e) => thread.replace(&e.messages),
            Event::ActivitySnapshot(e) => {
                let exists = thread.index.contains_key(&e.message_id);
                if !exists || e.replace != Some(false) {
                    let message = thread.upsert(&e.message_id, Role::Activity);
                    message.role = Role::Activity;
                    message.activity_type = Some(e.activity_type.clone());
                    message.content = Some(e.content.clone());
                }
            }
            Event::ActivityDelta(e) => {
                if let Some(&at) = thread.index.get(&e.message_id) {
                    let content = thread.messages[at].content.get_or_insert(Value::Null);
                    for op in &e.patch {
                        patch(content, op);
                    }
                }
            }
            _ => {}
        }
    }

    /// Threads in the order they first appeared.
    pub fn threads(&self) -> impl Iterator<Item = &str> {
        self.threads.iter().map(|(id, _)| id.as_str())
    }

    pub fn messages(&self, thread: &str) -> &[Message] {
        self.index.get(thread).map_or(&[], |&at| &self.threads[at].1.messages)
    }

    /// Whether a message or tool call has started but not ended.
    pub fn is_open(&self, thread: &str, id: &str) -> bool {
        self.index.get(thread).is_some_and(|&at| self.threads[at].1.open.contains(id))
    }

    /// A late joiner's snapshot: completed items and open ones with their
    /// content so far, as of the thread's latest `seq`.
    pub fn snapshot(&self, thread: &str) -> MessagesSnapshot {
        let seq = self.index.get(thread).map_or(0, |&at| self.threads[at].1.seq);
        MessagesSnapshot { thread_id: thread.to_owned(), seq, messages: self.messages(thread).to_vec() }
    }
}

/// One JSON Patch operation: add, remove and replace. The others are
/// skipped, as an unknown event would be.
fn patch(doc: &mut Value, op: &Value) {
    let (Some(kind), Some(path)) = (op.get("op").and_then(Value::as_str), op.get("path").and_then(Value::as_str)) else { return };
    let value = op.get("value").cloned().unwrap_or(Value::Null);
    if path.is_empty() {
        match kind {
            "add" | "replace" => *doc = value,
            "remove" => *doc = Value::Null,
            _ => {}
        }
        return;
    }
    let tokens: Vec<String> = path.split('/').skip(1).map(|t| t.replace("~1", "/").replace("~0", "~")).collect();
    let (last, parents) = tokens.split_last().expect("a non-empty path has a token");
    let mut at = doc;
    for token in parents {
        let next = match at {
            Value::Object(map) => map.get_mut(token),
            Value::Array(items) => token.parse::<usize>().ok().and_then(|i| items.get_mut(i)),
            _ => None,
        };
        match next {
            Some(next) => at = next,
            None => return,
        }
    }
    match (at, kind) {
        (Value::Object(map), "add" | "replace") => {
            map.insert(last.clone(), value);
        }
        (Value::Object(map), "remove") => {
            map.remove(last);
        }
        (Value::Array(items), "add") if last == "-" => items.push(value),
        (Value::Array(items), "add") => {
            if let Some(i) = last.parse::<usize>().ok().filter(|&i| i <= items.len()) {
                items.insert(i, value);
            }
        }
        (Value::Array(items), "replace") => {
            if let Some(item) = last.parse::<usize>().ok().and_then(|i| items.get_mut(i)) {
                *item = value;
            }
        }
        (Value::Array(items), "remove") => {
            if let Some(i) = last.parse::<usize>().ok().filter(|&i| i < items.len()) {
                items.remove(i);
            }
        }
        _ => {}
    }
}
