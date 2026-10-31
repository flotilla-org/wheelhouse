//! Rich Text for markdown (#558; decided on #538, #540, #541 and #546).
use std::collections::HashMap;

pub use crate::stream::{ListOp, SubscriptionId, UnknownSubscription};

#[cfg(any(test, feature = "test-support"))]
pub mod generate;
mod ingest;
mod pages;
mod ring;
mod tree;

#[cfg(test)]
mod property_tests;
#[cfg(test)]
mod scale_tests;
#[cfg(test)]
mod tests;

pub use pages::PAGE;
pub use ring::{Delta, Op, RING_CAPACITY};
pub use tree::{
    Alert, Align, Attr, Cell, Children, Color, Detail, Entry, InlineObject, Key, Kind, NodeContent, NodeId, Part, Span, Table, Tree,
    NO_STRING, TABLE_MEDIA_TYPE,
};

#[derive(Debug)]
struct Subscriber {
    /// The generation its last delta brought it to; None before its first.
    generation: Option<u64>,
}

#[derive(Debug)]
pub struct RichText {
    doc: Tree,
    ring: ring::Ring,
    generation: u64,
    subscribers: HashMap<SubscriptionId, Subscriber>,
    next_subscription: u64,
}

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
        };
        rich.reload(markdown);
        rich
    }

    /// Nodes whose key and content are unchanged keep their versions.
    /// Returns whether anything changed.
    pub fn reload(&mut self, markdown: &str) -> bool {
        let staged = ingest::parse(markdown.to_owned());
        let changed = self.doc.reconcile(staged, &mut self.ring, self.generation + 1);
        if changed {
            self.generation += 1;
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
        self.subscribers.insert(id, Subscriber { generation: None });
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
}
