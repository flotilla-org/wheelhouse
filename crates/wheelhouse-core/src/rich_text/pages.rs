//! A page never moves once written, so a byte range into one stays valid
//! for as long as the pages live.
use std::borrow::Cow;

/// A string longer than this gets a page of its own.
pub const PAGE: usize = 64 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct TextRange {
    pub page: u32,
    pub start: u32,
    pub len: u32,
}

/// Each string sits whole in one page.
#[derive(Debug, Default)]
pub struct Pages {
    pages: Vec<String>,
    /// Bytes written, live or not.
    written: usize,
}

impl Pages {
    pub fn push(&mut self, s: &str) -> TextRange {
        if s.is_empty() {
            return TextRange::default();
        }
        self.written += s.len();
        let fits = self.pages.last().is_some_and(|page| page.capacity() - page.len() >= s.len());
        if !fits {
            // A page's capacity is fixed when it is made: it never grows, so
            // what it holds never moves.
            self.pages.push(String::with_capacity(s.len().max(PAGE)));
        }
        let page = self.pages.len() - 1;
        let text = &mut self.pages[page];
        let start = text.len();
        text.push_str(s);
        let offset = |n: usize| u32::try_from(n).expect("text under 4 GiB");
        TextRange { page: offset(page), start: offset(start), len: offset(s.len()) }
    }

    pub fn get(&self, range: TextRange) -> &str {
        if range.len == 0 {
            return "";
        }
        &self.pages[range.page as usize][range.start as usize..(range.start + range.len) as usize]
    }

    pub fn written(&self) -> usize {
        self.written
    }
}

/// The raw markdown, split over pages at character boundaries. A slice may
/// cross pages.
#[derive(Debug, Default)]
pub struct Source {
    /// Each page's first byte offset.
    starts: Vec<usize>,
    pages: Vec<Box<str>>,
    len: usize,
}

impl Source {
    pub fn new(text: &str) -> Self {
        let mut source = Source::default();
        let mut at = 0;
        while at < text.len() {
            let mut end = (at + PAGE).min(text.len());
            while !text.is_char_boundary(end) {
                end -= 1;
            }
            source.starts.push(at);
            source.pages.push(text[at..end].into());
            at = end;
        }
        source.len = text.len();
        source
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Clamped to the source.
    pub fn slice(&self, start: usize, end: usize) -> Cow<'_, str> {
        let end = end.min(self.len);
        if start >= end {
            return "".into();
        }
        let first = self.starts.partition_point(|&s| s <= start) - 1;
        let last = self.starts.partition_point(|&s| s < end) - 1;
        if first == last {
            let base = self.starts[first];
            return self.pages[first][start - base..end - base].into();
        }
        let mut out = String::with_capacity(end - start);
        for page in first..=last {
            let base = self.starts[page];
            let text = &self.pages[page];
            out.push_str(&text[start.max(base) - base..end.min(base + text.len()) - base]);
        }
        out.into()
    }

    pub fn line_start(&self, at: usize) -> usize {
        let at = at.min(self.len);
        if self.pages.is_empty() {
            return 0;
        }
        let mut page = self.starts.partition_point(|&s| s <= at).saturating_sub(1);
        let mut end = at;
        loop {
            let base = self.starts[page];
            let bytes = self.pages[page].as_bytes();
            let upto = (end - base).min(bytes.len());
            if let Some(i) = bytes[..upto].iter().rposition(|&b| b == b'\n') {
                return base + i + 1;
            }
            if page == 0 {
                return 0;
            }
            page -= 1;
            end = base;
        }
    }

    pub fn whole(&self) -> String {
        self.slice(0, self.len).into_owned()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pages_never_move_and_long_strings_get_their_own() {
        let mut pages = Pages::default();
        let a = pages.push("hello");
        let first = pages.pages[0].as_ptr();
        let long = "x".repeat(PAGE + 10);
        let b = pages.push(&long);
        for _ in 0..2000 {
            pages.push("some more text to fill pages");
        }
        assert_eq!(pages.pages[0].as_ptr(), first);
        assert_eq!(pages.get(a), "hello");
        assert_eq!(pages.get(b), long);
    }

    #[test]
    fn source_slices_cross_pages() {
        let text: String = (0..30_000).map(|i| char::from(b'a' + (i % 26) as u8)).chain("é".repeat(40_000).chars()).collect();
        let source = Source::new(&text);
        assert!(source.pages.len() > 1);
        assert_eq!(source.whole(), text);
        assert_eq!(source.slice(60_000, 70_000), &text[60_000..70_000]);
        let lines = Source::new(&format!("{}\n{}", "a".repeat(70_000), "b".repeat(70_000)));
        assert_eq!(lines.line_start(140_000), 70_001);
        assert_eq!(lines.line_start(70_000), 0);
    }
}
