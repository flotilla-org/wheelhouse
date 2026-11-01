//! JSON Lines: one event per line, appended in order.
use std::fmt;
use std::io::{self, Write};

use super::Event;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LineError {
    /// From 1.
    pub line: usize,
    pub message: String,
}

impl fmt::Display for LineError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "line {}: {}", self.line, self.message)
    }
}

impl std::error::Error for LineError {}

/// None for a blank line and for an event type this version doesn't know.
/// A line that isn't JSON, or a known event with a field of the wrong type,
/// is an error a reader may skip.
pub fn parse_line(line: &[u8]) -> Result<Option<Event>, String> {
    let line = trim(line);
    if line.is_empty() {
        return Ok(None);
    }
    match serde_json::from_slice::<Event>(line) {
        Ok(Event::Unknown) => Ok(None),
        Ok(event) => Ok(Some(event)),
        Err(e) => Err(e.to_string()),
    }
}

fn trim(line: &[u8]) -> &[u8] {
    let start = line.iter().position(|b| !b.is_ascii_whitespace()).unwrap_or(line.len());
    let end = line.iter().rposition(|b| !b.is_ascii_whitespace()).map_or(start, |i| i + 1);
    &line[start..end]
}

/// Every known event in a contract file, and an error for each bad line.
pub fn read(bytes: &[u8]) -> impl Iterator<Item = Result<Event, LineError>> + '_ {
    bytes.split(|&b| b == b'\n').enumerate().filter_map(|(i, line)| match parse_line(line) {
        Ok(event) => event.map(Ok),
        Err(message) => Some(Err(LineError { line: i + 1, message })),
    })
}

/// One line, without its newline.
pub fn to_line(event: &Event) -> String {
    serde_json::to_string(event).expect("events serialize")
}

pub fn write<'a>(out: &mut impl Write, events: impl IntoIterator<Item = &'a Event>) -> io::Result<()> {
    for event in events {
        out.write_all(to_line(event).as_bytes())?;
        out.write_all(b"\n")?;
    }
    Ok(())
}

/// Splits bytes appended in any pieces into lines, each with the offset of
/// its first byte. A last line still without its newline waits for it, unless
/// it is already a whole JSON object: a file's last line may lack its
/// newline, and no proper prefix of an object is one, so taking it early
/// gives the same lines as waiting.
#[derive(Debug, Default)]
pub struct Lines {
    pending: Vec<u8>,
    /// Where `pending` starts.
    offset: u64,
    /// `pending` was taken already as a whole object.
    taken: bool,
}

impl Lines {
    /// For bytes that follow `offset` bytes already read.
    pub fn starting_at(offset: u64) -> Lines {
        Lines { offset, ..Lines::default() }
    }

    /// The offset of the next byte to be fed.
    pub fn end(&self) -> u64 {
        self.offset + self.pending.len() as u64
    }

    pub fn feed(&mut self, mut bytes: &[u8], mut line: impl FnMut(u64, &[u8])) {
        while let Some(newline) = bytes.iter().position(|&b| b == b'\n') {
            let at = self.offset;
            let len = self.pending.len() + newline + 1;
            if !self.taken {
                if self.pending.is_empty() {
                    line(at, &bytes[..newline]);
                } else {
                    self.pending.extend_from_slice(&bytes[..newline]);
                    line(at, &self.pending);
                }
            }
            self.pending.clear();
            self.taken = false;
            self.offset += len as u64;
            bytes = &bytes[newline + 1..];
        }
        self.pending.extend_from_slice(bytes);
        if !self.taken && trim(&self.pending).ends_with(b"}") && serde_json::from_slice::<serde::de::IgnoredAny>(&self.pending).is_ok() {
            line(self.offset, &self.pending);
            self.taken = true;
        }
    }
}
