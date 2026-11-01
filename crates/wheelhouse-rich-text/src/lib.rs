//! Rich Text for markdown (#558) and conversations (#614); decided on #538,
//! #540, #541, #546 and #548.
use std::collections::HashMap;
use std::time::Duration;

pub use wheelhouse_stream::{ListOp, SubscriptionId, UnknownSubscription};

mod conversation;
mod find;
#[cfg(any(test, feature = "test-support"))]
pub mod generate;
pub mod heights;
mod ingest;
mod log;
mod pages;
mod ring;
mod select;
mod tail;
mod tree;
mod view;

#[cfg(test)]
mod conversation_tests;
#[cfg(test)]
mod property_tests;
#[cfg(test)]
mod scale_tests;
#[cfg(test)]
mod tests;

pub use conversation::{TAIL_BYTES, TAIL_ITEMS};
pub use find::{Find, FindOptions, FindStatus, Match, FIND_BUDGET, MATCH_LIMIT};
pub use log::{CHUNK_BYTES, CHUNK_LINES, LINE_CAP};
pub use pages::PAGE;
pub use ring::{Delta, Op, RING_CAPACITY};
pub use select::{copy_block, rendered, Format, Point, Selection};
pub use tree::{
    Alert, Align, Attr, Cell, Children, Color, Detail, Entry, InlineObject, Key, Kind, NodeContent, NodeId, Part, Role, SectionKind, Span,
    Table, ToolKind, ToolStatus, Tree, NO_STRING, TABLE_MEDIA_TYPE,
};
pub use view::{Anchor, Metrics, Position, Thumb, View, Window, WindowEntry, OVERSCAN};

#[derive(Debug)]
struct Subscriber {
    /// The generation its last delta brought it to; None before its first.
    generation: Option<u64>,
    view: View,
}

#[derive(Debug)]
pub struct RichText {
    doc: Tree,
    ring: ring::Ring,
    generation: u64,
    subscribers: HashMap<SubscriptionId, Subscriber>,
    next_subscription: u64,
    conversation: Option<Box<conversation::Conversation>>,
}

/// Backfill's slice per poll (#540, decision 10).
pub const BACKFILL_BUDGET: Duration = Duration::from_millis(2);

impl RichText {
    pub fn open(markdown: &str) -> RichText {
        RichText::with_ring_capacity(markdown, RING_CAPACITY)
    }

    /// `capacity` counts changes plus child-list operations; a subscriber
    /// that falls further behind gets a snapshot.
    pub fn with_ring_capacity(markdown: &str, capacity: usize) -> RichText {
        let mut rich = RichText {
            doc: Tree::default(),
            ring: ring::Ring::new(capacity),
            generation: 0,
            subscribers: HashMap::new(),
            next_subscription: 1,
            conversation: None,
        };
        rich.reload(markdown);
        rich
    }

    /// A conversation from contract JSON Lines (#548): its newest items
    /// first; [`RichText::backfill`] fills in the rest.
    pub fn open_conversation(jsonl: &[u8]) -> RichText {
        RichText::conversation_with_ring_capacity(jsonl, RING_CAPACITY)
    }

    pub fn conversation_with_ring_capacity(jsonl: &[u8], capacity: usize) -> RichText {
        RichText::tuned_conversation(jsonl, capacity, conversation::Tuning::default())
    }

    pub(crate) fn tuned_conversation(jsonl: &[u8], capacity: usize, tuning: conversation::Tuning) -> RichText {
        let mut rich = RichText::with_ring_capacity("", capacity);
        let conversation = rich.edit(|sink| conversation::Conversation::open(jsonl, tuning, sink));
        rich.conversation = Some(Box::new(conversation));
        rich
    }

    pub fn is_conversation(&self) -> bool {
        self.conversation.is_some()
    }

    /// Bytes appended to a conversation's file, in any pieces: a line waits
    /// for its newline. Returns whether anything changed; false for markdown.
    pub fn append(&mut self, bytes: &[u8]) -> bool {
        let Some(mut conversation) = self.conversation.take() else { return false };
        let before = self.generation;
        self.edit(|sink| conversation.append(bytes, sink));
        self.conversation = Some(conversation);
        self.generation != before
    }

    /// Ingests earlier lines for about `budget`, inserting them at the
    /// front; the anchor keeps each View still. Returns the bytes left.
    pub fn backfill(&mut self, budget: Duration) -> usize {
        let Some(mut conversation) = self.conversation.take() else { return 0 };
        let left = self.edit(|sink| conversation.backfill(budget, sink));
        self.conversation = Some(conversation);
        left
    }

    /// The bytes backfill has yet to read; 0 for markdown.
    pub fn backfill_remaining(&self) -> usize {
        self.conversation.as_ref().map_or(0, |c| c.remaining())
    }

    fn edit<T>(&mut self, f: impl FnOnce(&mut conversation::Sink) -> T) -> T {
        let anchors: Vec<(SubscriptionId, _)> = self.subscribers.iter().map(|(&id, s)| (id, s.view.anchor_was(&self.doc))).collect();
        let mut sink = conversation::Sink {
            doc: &mut self.doc,
            ring: &mut self.ring,
            generation: self.generation + 1,
            views: self.subscribers.values_mut().map(|s| &mut s.view).collect(),
            changed: false,
        };
        let out = f(&mut sink);
        if sink.changed {
            self.generation += 1;
        }
        for (id, anchor) in anchors {
            self.subscribers.get_mut(&id).unwrap().view.settle(&self.doc, anchor);
        }
        out
    }

    /// Nodes whose key and content are unchanged keep their versions. A
    /// conversation becomes markdown. Returns whether anything changed.
    /// Offsets are `u32`, so markdown is under 4 GiB.
    pub fn reload(&mut self, markdown: &str) -> bool {
        self.conversation = None;
        assert!(u32::try_from(markdown.len()).is_ok(), "markdown under 4 GiB");
        let before = view::was(&self.doc);
        let anchors: Vec<(SubscriptionId, _)> = self.subscribers.iter().map(|(&id, s)| (id, s.view.anchor_was(&self.doc))).collect();
        let staged = ingest::parse(markdown);
        let changed = self.doc.reconcile(staged, markdown, &mut self.ring, self.generation + 1);
        if changed {
            self.generation += 1;
        }
        for (id, anchor) in anchors {
            let subscriber = self.subscribers.get_mut(&id).unwrap();
            subscriber.view.sync(&self.doc, &before, anchor);
        }
        changed
    }

    pub fn doc(&self) -> &Tree {
        &self.doc
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }

    pub fn subscribe(&mut self) -> SubscriptionId {
        let id = SubscriptionId(self.next_subscription);
        self.next_subscription += 1;
        self.subscribers.insert(id, Subscriber { generation: None, view: View::new(&self.doc) });
        id
    }

    pub fn unsubscribe(&mut self, id: SubscriptionId) -> bool {
        self.subscribers.remove(&id).is_some()
    }

    fn subscriber(&self, id: SubscriptionId) -> Result<&Subscriber, UnknownSubscription> {
        self.subscribers.get(&id).ok_or(UnknownSubscription(id))
    }

    pub fn changed_since(&self, id: SubscriptionId) -> Result<bool, UnknownSubscription> {
        Ok(self.subscriber(id)?.generation != Some(self.generation))
    }

    /// A snapshot for the first poll and for a subscriber that fell off the
    /// ring's end; None if nothing changed.
    pub fn poll(&mut self, id: SubscriptionId) -> Result<Option<Delta>, UnknownSubscription> {
        let generation = self.generation;
        let subscriber = self.subscribers.get_mut(&id).ok_or(UnknownSubscription(id))?;
        let delta = match subscriber.generation {
            Some(since) if since == generation => return Ok(None),
            Some(since) if since >= self.ring.floor() => ring::changes(&self.doc, &self.ring, since, generation),
            _ => ring::snapshot(&self.doc, generation),
        };
        subscriber.generation = Some(generation);
        Ok(Some(delta))
    }

    pub fn view(&self, id: SubscriptionId) -> Result<&View, UnknownSubscription> {
        Ok(&self.subscriber(id)?.view)
    }

    pub fn view_mut(&mut self, id: SubscriptionId) -> Result<(&Tree, &mut View), UnknownSubscription> {
        let subscriber = self.subscribers.get_mut(&id).ok_or(UnknownSubscription(id))?;
        Ok((&self.doc, &mut subscriber.view))
    }
}
