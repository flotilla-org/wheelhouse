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
    /// Where its latest event was read.
    at: u64,
    typed: Typed,
    /// While earlier events may still arrive (backfill, a page): the latest
    /// snapshot read and the deltas read after it, so one read late folds
    /// in where it belongs.
    base: Option<(u64, Value)>,
    deltas: BTreeMap<u64, Vec<Value>>,
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

    fn fold(&self) -> Value {
        let mut value = self.base.as_ref().map_or(Value::Null, |(_, value)| value.clone());
        self.deltas.values().flatten().for_each(|op| patch(&mut value, op));
        value
    }
}

enum Change<'a> {
    Snapshot(Value),
    Delta(&'a [Value]),
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
    settled: bool,
}

impl Documents {
    pub fn snapshot(&mut self, at: u64, thread: &str, key: String, media_type: Option<String>, value: Value) {
        self.change(at, thread, key, media_type, Change::Snapshot(value));
    }

    pub fn delta(&mut self, at: u64, thread: &str, key: String, media_type: Option<String>, ops: &[Value]) {
        self.change(at, thread, key, media_type, Change::Delta(ops));
    }

    fn change(&mut self, at: u64, thread: &str, key: String, media_type: Option<String>, change: Change) {
        let documents = self.threads.entry(thread.to_owned()).or_default();
        let new = !documents.contains_key(&key);
        let document = documents.entry(key.clone()).or_insert_with(|| Document {
            key,
            media_type: None,
            value: Value::Null,
            at,
            typed: Typed::None,
            base: None,
            deltas: BTreeMap::new(),
        });
        let late = !new && at < document.at;
        if document.base.as_ref().is_some_and(|(base, _)| at <= *base) || (late && self.settled) {
            return;
        }
        let mut value = document.value.clone();
        match change {
            Change::Snapshot(snapshot) => {
                document.deltas = document.deltas.split_off(&at);
                if !self.settled {
                    document.base = Some((at, snapshot.clone()));
                }
                value = snapshot;
            }
            Change::Delta(ops) => {
                if !self.settled {
                    document.deltas.insert(at, ops.to_vec());
                }
                ops.iter().for_each(|op| patch(&mut value, op));
            }
        }
        if late {
            value = document.fold();
        }
        document.at = document.at.max(at);
        let media_type = match late {
            true => document.media_type.clone().or(media_type),
            false => media_type.or_else(|| document.media_type.clone()),
        };
        if !new && value == document.value && media_type == document.media_type {
            return;
        }
        document.typed = typed(&document.key, &value);
        (document.value, document.media_type) = (value, media_type);
        self.revision += 1;
    }

    /// No earlier event will arrive: what was kept to fold one in goes. It
    /// holds from then on; an event read late after it is dropped, as one
    /// was before documents kept anything.
    pub fn settle(&mut self) {
        self.settled = true;
        for document in self.threads.values_mut().flat_map(BTreeMap::values_mut) {
            (document.base, document.deltas) = (None, BTreeMap::new());
        }
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
