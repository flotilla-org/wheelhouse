//! Find, in budgeted slices over every Block, collapsed or not (#546).
use regex::{Regex, RegexBuilder};

use super::select::Point;
use super::tree::{Key, Part, Tree};
use super::view::View;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct FindOptions {
    pub regex: bool,
    pub case_sensitive: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Match {
    pub key: Key,
    pub start: u32,
    pub end: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FindStatus {
    pub count: usize,
    /// `count` is all of them, unless `capped`.
    pub done: bool,
    /// The scan stopped at MATCH_LIMIT matches: show "N+".
    pub capped: bool,
    pub current: Option<usize>,
}

#[derive(Debug)]
pub struct Find {
    regex: Regex,
    next: usize,
    matches: Vec<Match>,
    done: bool,
    capped: bool,
    current: Option<usize>,
}

/// A query that matches nearly everything (`.` as a regex) would otherwise
/// hold a match per character.
pub const MATCH_LIMIT: usize = 100_000;

/// Bytes of text scanned per step.
pub const FIND_BUDGET: usize = 256 * 1024;

impl Find {
    pub fn new(query: &str, options: FindOptions) -> Result<Find, String> {
        let pattern = if options.regex { query.to_owned() } else { regex::escape(query) };
        let regex = RegexBuilder::new(&pattern).case_insensitive(!options.case_sensitive).build().map_err(|e| e.to_string())?;
        Ok(Find { regex, next: 0, matches: vec![], done: false, capped: false, current: None })
    }

    pub(crate) fn restart(&mut self) {
        self.next = 0;
        self.matches.clear();
        self.done = false;
        self.capped = false;
        self.current = None;
    }

    pub fn status(&self) -> FindStatus {
        FindStatus { count: self.matches.len(), done: self.done, capped: self.capped, current: self.current }
    }

    pub fn matches(&self) -> &[Match] {
        &self.matches
    }

    /// At least one Block.
    pub(crate) fn step(&mut self, doc: &Tree, budget: usize) -> FindStatus {
        let entries = doc.entries();
        let mut scanned = 0;
        while self.next < entries.len() && !self.capped && (scanned == 0 || scanned < budget) {
            let e = entries[self.next];
            self.next += 1;
            if e.part != Part::Block {
                continue;
            }
            let text = doc.text(e.node);
            scanned += text.len().max(1);
            let key = doc.key(e.node);
            for m in self.regex.find_iter(text).filter(|m| !m.is_empty()) {
                if self.matches.len() == MATCH_LIMIT {
                    self.capped = true;
                    break;
                }
                self.matches.push(Match { key, start: m.start() as u32, end: m.end() as u32 });
            }
        }
        self.done = self.capped || self.next >= entries.len();
        self.status()
    }
}

impl View {
    pub fn find(&mut self, query: &str, options: FindOptions) -> Result<(), String> {
        self.find = Some(Find::new(query, options)?);
        Ok(())
    }

    pub fn find_step(&mut self, doc: &Tree, budget: usize) -> Option<FindStatus> {
        Some(self.find.as_mut()?.step(doc, budget))
    }

    pub fn find_state(&self) -> Option<&Find> {
        self.find.as_ref()
    }

    pub fn end_find(&mut self) {
        self.find = None;
    }

    pub fn find_next(&mut self, doc: &Tree, forward: bool) -> Option<Match> {
        let find = self.find.as_mut()?;
        let count = find.matches.len();
        if count == 0 {
            return None;
        }
        let current = match (find.current, forward) {
            (None, true) => 0,
            (None, false) => count - 1,
            (Some(i), true) => (i + 1) % count,
            (Some(i), false) => (i + count - 1) % count,
        };
        find.current = Some(current);
        let m = find.matches[current];
        self.select(doc, Point { key: m.key, byte: m.start }, Point { key: m.key, byte: m.end }).ok()?;
        self.reveal(doc, m.key, m.start).ok()?;
        Some(m)
    }
}
