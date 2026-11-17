use std::collections::{BTreeMap, HashMap};

use conversation_events::{patch, AgentCommand, AgentTask, ChecklistItem, CHECKLIST, COMMANDS, TASKS};
use serde::de::DeserializeOwned;
use serde_json::Value;

/// A thread's keyed state document, as the latest of its events left it.
#[derive(Debug, Clone, PartialEq)]
pub struct Document {
    pub key: String,
    pub media_type: Option<String>,
    pub value: Value,
    /// Where its latest event was read: an older one, read later by backfill
    /// or a page, is stale.
    at: u64,
    typed: Typed,
}

/// The documents with a known shape, read once per change.
#[derive(Debug, Clone, PartialEq, Default)]
pub enum Typed {
    #[default]
    None,
    Checklist(Vec<ChecklistItem>),
    Tasks(Vec<AgentTask>),
    Commands(Vec<AgentCommand>),
}

impl Document {
    pub fn typed(&self) -> &Typed {
        &self.typed
    }
}

/// Entries this version can't read are left out, so one bad entry doesn't
/// cost the rest.
fn entries<T: DeserializeOwned>(value: &Value) -> Vec<T> {
    value.as_array().into_iter().flatten().filter_map(|entry| serde_json::from_value(entry.clone()).ok()).collect()
}

fn typed(key: &str, value: &Value) -> Typed {
    match key {
        CHECKLIST => Typed::Checklist(entries(value)),
        TASKS => Typed::Tasks(entries(value)),
        COMMANDS => Typed::Commands(entries(value)),
        _ => Typed::None,
    }
}

/// Every thread's documents, by thread and key: a subagent's checklist in
/// the same file is its own thread's (#692), never its parent's.
#[derive(Debug, Default)]
pub(crate) struct Documents {
    threads: HashMap<String, BTreeMap<String, Document>>,
    revision: u64,
}

impl Documents {
    pub fn snapshot(&mut self, at: u64, thread: &str, key: String, media_type: Option<String>, value: Value) {
        self.change(at, thread, key, media_type, |document| *document = value);
    }

    pub fn delta(&mut self, at: u64, thread: &str, key: String, media_type: Option<String>, ops: &[Value]) {
        self.change(at, thread, key, media_type, |document| ops.iter().for_each(|op| patch(document, op)));
    }

    fn change(&mut self, at: u64, thread: &str, key: String, media_type: Option<String>, f: impl FnOnce(&mut Value)) {
        let documents = self.threads.entry(thread.to_owned()).or_default();
        let new = !documents.contains_key(&key);
        let document =
            documents.entry(key.clone()).or_insert_with(|| Document { key, media_type: None, value: Value::Null, at, typed: Typed::None });
        if at < document.at {
            return;
        }
        document.at = at;
        let mut value = document.value.clone();
        f(&mut value);
        let media_type = media_type.or_else(|| document.media_type.clone());
        if !new && value == document.value && media_type == document.media_type {
            return;
        }
        document.typed = typed(&document.key, &value);
        (document.value, document.media_type) = (value, media_type);
        self.revision += 1;
    }

    /// Moves whenever any document is added or changes, and only then.
    pub fn revision(&self) -> u64 {
        self.revision
    }

    /// Document `key` of the threads `own` takes: the one changed last.
    pub fn get(&self, key: &str, own: impl Fn(&str) -> bool) -> Option<&Document> {
        let documents = self.threads.iter().filter(|(thread, _)| own(thread));
        documents.filter_map(|(_, documents)| documents.get(key)).max_by_key(|document| document.at)
    }

    /// The documents of the threads `own` takes, by key; where two have one
    /// key, the one changed last.
    pub fn of(&self, own: impl Fn(&str) -> bool) -> Vec<&Document> {
        let mut out: BTreeMap<&str, &Document> = BTreeMap::new();
        for (_, documents) in self.threads.iter().filter(|(thread, _)| own(thread)) {
            for (key, document) in documents {
                if out.get(key.as_str()).is_none_or(|kept| kept.at < document.at) {
                    out.insert(key, document);
                }
            }
        }
        out.into_values().collect()
    }
}
