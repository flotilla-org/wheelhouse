//! Tool output as log Blocks (#540, decision 7): ANSI styles carried across
//! chunks as literal-colour spans, OSC 8 links, and a carriage return that
//! rewrites only the incomplete line. Any other escape is dropped.
use std::collections::HashMap;

use super::tree::{Attr, Color, Span, NO_STRING};

/// A log Block closes at about this many lines or bytes (#538).
pub const CHUNK_LINES: usize = 64;
pub const CHUNK_BYTES: usize = 16 * 1024;
/// A longer line is broken here.
pub const LINE_CAP: usize = 4096;
/// An OSC string longer than this is dropped whole.
const OSC_CAP: usize = 8 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
struct Pen {
    fg: Option<Color>,
    bg: Option<Color>,
    bold: bool,
    italic: bool,
    underline: bool,
    strike: bool,
    inverse: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Default)]
struct Style {
    pen: Pen,
    link: Option<String>,
}

impl Style {
    fn attrs(&self, link: impl FnOnce(&str) -> u32) -> Vec<Attr> {
        let pen = self.pen;
        let (fg, bg) = if pen.inverse {
            (Some(pen.bg.unwrap_or(Color::Background)), Some(pen.fg.unwrap_or(Color::Foreground)))
        } else {
            (pen.fg, pen.bg)
        };
        let mut attrs = vec![];
        if let Some(fg) = fg {
            attrs.push(Attr::Color(fg));
        }
        if let Some(bg) = bg {
            attrs.push(Attr::Background(bg));
        }
        if pen.bold {
            attrs.push(Attr::Strong);
        }
        if pen.italic {
            attrs.push(Attr::Emphasis);
        }
        if pen.underline {
            attrs.push(Attr::Underline);
        }
        if pen.strike {
            attrs.push(Attr::Strikethrough);
        }
        if let Some(target) = &self.link {
            attrs.push(Attr::Link { target: link(target), reference: NO_STRING });
        }
        attrs
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub(crate) struct Chunk {
    pub text: String,
    pub spans: Vec<Span>,
    pub strings: Vec<String>,
    lines: usize,
}

impl Chunk {
    fn push_line(&mut self, cells: &[(char, u32)], styles: &[Style]) {
        if self.lines > 0 {
            self.text.push('\n');
        }
        self.lines += 1;
        // Each attribute's span runs as far as the cells that have it.
        let mut open: Vec<(Attr, u32)> = vec![];
        let mut at = 0;
        while at < cells.len() {
            let style = cells[at].1;
            let start = self.text.len() as u32;
            while at < cells.len() && cells[at].1 == style {
                self.text.push(cells[at].0);
                at += 1;
            }
            let strings = &mut self.strings;
            let attrs = styles[style as usize].attrs(|target| match strings.iter().position(|s| s == target) {
                Some(i) => i as u32,
                None => {
                    strings.push(target.to_owned());
                    (strings.len() - 1) as u32
                }
            });
            open.retain(|&(attr, from)| {
                let keep = attrs.contains(&attr);
                if !keep {
                    self.spans.push(Span { start: from, len: start - from, attr });
                }
                keep
            });
            for attr in attrs {
                if !open.iter().any(|&(a, _)| a == attr) {
                    open.push((attr, start));
                }
            }
        }
        let end = self.text.len() as u32;
        for (attr, from) in open {
            self.spans.push(Span { start: from, len: end - from, attr });
        }
    }

    fn full(&self) -> bool {
        self.lines >= CHUNK_LINES || self.text.len() >= CHUNK_BYTES
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
enum State {
    #[default]
    Ground,
    Escape,
    /// `ESC` then intermediates, as in a charset designation.
    EscapeIntermediate,
    Csi(String),
    Osc(String),
    OscEscape(String),
    /// DCS, SOS, PM or APC, until its terminator.
    Ignored,
    IgnoredEscape,
}

/// Fed text in any pieces, it gives the same Blocks.
#[derive(Debug, Default)]
pub(crate) struct Log {
    closed: Vec<Chunk>,
    open: Chunk,
    line: Vec<(char, u32)>,
    cursor: usize,
    state: State,
    style: Style,
    styles: Vec<Style>,
    ids: HashMap<Style, u32>,
}

impl Log {
    pub fn feed(&mut self, text: &str) {
        for c in text.chars() {
            self.char(c);
        }
    }

    /// The closed chunks, then the open one with the incomplete line.
    pub fn chunks(&self) -> Vec<Chunk> {
        let mut out = self.closed.clone();
        let mut open = self.open.clone();
        if !self.line.is_empty() {
            open.push_line(&self.line, &self.styles);
        }
        if open.lines > 0 {
            out.push(open);
        }
        out
    }

    fn style_id(&mut self) -> u32 {
        if let Some(&id) = self.ids.get(&self.style) {
            return id;
        }
        self.styles.push(self.style.clone());
        let id = (self.styles.len() - 1) as u32;
        self.ids.insert(self.style.clone(), id);
        id
    }

    fn put(&mut self, c: char) {
        if self.cursor >= LINE_CAP {
            self.end_line();
        }
        let style = self.style_id();
        if self.cursor < self.line.len() {
            self.line[self.cursor] = (c, style);
        } else {
            self.line.push((c, style));
        }
        self.cursor += 1;
    }

    fn end_line(&mut self) {
        self.open.push_line(&self.line, &self.styles);
        self.line.clear();
        self.cursor = 0;
        if self.open.full() {
            self.closed.push(std::mem::take(&mut self.open));
        }
    }

    fn control(&mut self, c: char) {
        match c {
            '\n' => self.end_line(),
            '\r' => self.cursor = 0,
            '\t' => self.put('\t'),
            _ => {}
        }
    }

    fn char(&mut self, c: char) {
        let state = std::mem::take(&mut self.state);
        self.state = match state {
            State::Ground => match c {
                '\x1b' => State::Escape,
                '\u{9b}' => State::Csi(String::new()),
                '\u{9d}' => State::Osc(String::new()),
                '\u{90}' | '\u{98}' | '\u{9e}' | '\u{9f}' => State::Ignored,
                c if (c as u32) < 0x20 => {
                    self.control(c);
                    State::Ground
                }
                '\x7f'..='\u{9f}' => State::Ground,
                c => {
                    self.put(c);
                    State::Ground
                }
            },
            State::Escape | State::EscapeIntermediate => match c {
                '[' if state == State::Escape => State::Csi(String::new()),
                ']' if state == State::Escape => State::Osc(String::new()),
                'P' | 'X' | '^' | '_' if state == State::Escape => State::Ignored,
                '\x1b' => State::Escape,
                ' '..='/' => State::EscapeIntermediate,
                _ => State::Ground,
            },
            State::Csi(mut params) => match c {
                '0'..='?' | ' '..='/' => {
                    if params.len() < 256 {
                        params.push(c);
                    }
                    State::Csi(params)
                }
                '@'..='~' => {
                    if c == 'm' {
                        self.sgr(&params);
                    }
                    State::Ground
                }
                '\x1b' => State::Escape,
                c if (c as u32) < 0x20 => {
                    self.control(c);
                    State::Csi(params)
                }
                _ => State::Ground,
            },
            State::Osc(mut body) => match c {
                '\x07' | '\u{9c}' => {
                    self.osc(&body);
                    State::Ground
                }
                '\x1b' => State::OscEscape(body),
                _ => {
                    if body.len() < OSC_CAP {
                        body.push(c);
                        State::Osc(body)
                    } else {
                        State::Ignored
                    }
                }
            },
            State::OscEscape(body) => {
                if c == '\\' {
                    self.osc(&body);
                    State::Ground
                } else {
                    // An unterminated OSC is dropped; this ESC starts anew.
                    self.state = State::Escape;
                    return self.char(c);
                }
            }
            State::Ignored => match c {
                '\x1b' => State::IgnoredEscape,
                '\x07' | '\u{9c}' => State::Ground,
                _ => State::Ignored,
            },
            State::IgnoredEscape => match c {
                '\\' => State::Ground,
                '\x1b' => State::IgnoredEscape,
                _ => State::Ignored,
            },
        };
    }

    /// OSC 8 opens a link, or closes it with an empty URI. Tool output is
    /// untrusted, so only web and mail links become links; the text of any
    /// other stays plain.
    fn osc(&mut self, body: &str) {
        let mut parts = body.splitn(3, ';');
        if parts.next() != Some("8") {
            return;
        }
        let (Some(_params), Some(uri)) = (parts.next(), parts.next()) else { return };
        let scheme = uri.split_once(':').map(|(s, _)| s.to_ascii_lowercase());
        let safe = matches!(scheme.as_deref(), Some("http" | "https" | "mailto"));
        self.style.link = safe.then(|| uri.to_owned());
    }

    fn sgr(&mut self, params: &str) {
        if params.starts_with(['<', '=', '>', '?']) {
            return;
        }
        let groups: Vec<Vec<Option<u32>>> =
            params.split(';').map(|group| group.split(':').map(|n| n.parse::<u32>().ok()).collect()).collect();
        let pen = &mut self.style.pen;
        let mut i = 0;
        while i < groups.len() {
            let group = &groups[i];
            let code = group[0].unwrap_or(0);
            i += 1;
            match code {
                0 => *pen = Pen::default(),
                1 => pen.bold = true,
                3 => pen.italic = true,
                4 => pen.underline = group.get(1).copied().flatten() != Some(0),
                7 => pen.inverse = true,
                9 => pen.strike = true,
                21 => pen.underline = true,
                22 => pen.bold = false,
                23 => pen.italic = false,
                24 => pen.underline = false,
                27 => pen.inverse = false,
                29 => pen.strike = false,
                30..=37 => pen.fg = Some(Color::Palette((code - 30) as u8)),
                39 => pen.fg = None,
                40..=47 => pen.bg = Some(Color::Palette((code - 40) as u8)),
                49 => pen.bg = None,
                90..=97 => pen.fg = Some(Color::Palette((code - 90 + 8) as u8)),
                100..=107 => pen.bg = Some(Color::Palette((code - 100 + 8) as u8)),
                38 | 48 => {
                    let color = if group.len() > 1 {
                        extended(&group[1..])
                    } else {
                        // The semicolon form takes the following parameters.
                        let rest: Vec<Option<u32>> = groups[i..].iter().map(|g| g[0]).collect();
                        let (color, used) = extended_semicolons(&rest);
                        i += used;
                        color
                    };
                    if let Some(color) = color {
                        if code == 38 {
                            pen.fg = Some(color);
                        } else {
                            pen.bg = Some(color);
                        }
                    }
                }
                _ => {}
            }
        }
    }
}

fn byte(n: Option<u32>) -> Option<u8> {
    n.and_then(|n| u8::try_from(n).ok())
}

/// `5:n` or `2:r:g:b`, or `2:space:r:g:b` with a colour space.
fn extended(sub: &[Option<u32>]) -> Option<Color> {
    match sub.first().copied().flatten()? {
        5 => Some(Color::Palette(byte(*sub.get(1)?)?)),
        2 => {
            let rgb = if sub.len() >= 5 { &sub[2..5] } else { sub.get(1..4)? };
            Some(Color::Rgb(byte(rgb[0])?, byte(rgb[1])?, byte(rgb[2])?))
        }
        _ => None,
    }
}

/// `5;n` or `2;r;g;b`, and how many parameters it used.
fn extended_semicolons(rest: &[Option<u32>]) -> (Option<Color>, usize) {
    match rest.first().copied().flatten() {
        Some(5) => (rest.get(1).and_then(|&n| byte(n)).map(Color::Palette), 2.min(rest.len())),
        Some(2) => {
            let rgb = rest.get(1..4).and_then(|c| Some(Color::Rgb(byte(c[0])?, byte(c[1])?, byte(c[2])?)));
            (rgb, 4.min(rest.len()))
        }
        _ => (None, 1.min(rest.len())),
    }
}

#[cfg(test)]
mod tests {
    use hegel::generators as gs;
    use hegel::TestCase;

    use super::*;

    /// A span's start and length in its line, and its attribute.
    type LineSpan = (u32, u32, String);

    /// Each line's text and its spans, links resolved, as a frontend sees them.
    fn lines(log: &Log) -> Vec<(String, Vec<LineSpan>)> {
        let mut out = vec![];
        for chunk in log.chunks() {
            let mut at = 0u32;
            for line in chunk.text.split('\n') {
                let end = at + line.len() as u32;
                let mut spans: Vec<LineSpan> = chunk
                    .spans
                    .iter()
                    .filter(|s| s.start >= at && s.end() <= end && (s.len > 0 || s.start < end))
                    .map(|s| {
                        let attr = match s.attr {
                            Attr::Link { target, .. } => format!("Link({})", chunk.strings[target as usize]),
                            other => format!("{other:?}"),
                        };
                        (s.start - at, s.len, attr)
                    })
                    .collect();
                spans.sort();
                out.push((line.to_owned(), spans));
                at = end + 1;
            }
        }
        out
    }

    fn fed(pieces: &[&str]) -> Log {
        let mut log = Log::default();
        for piece in pieces {
            log.feed(piece);
        }
        log
    }

    const ALPHABET: &[&str] = &[
        "\x1b",
        "[",
        "]",
        ";",
        ":",
        "m",
        "0",
        "1",
        "2",
        "3",
        "5",
        "8",
        "9",
        "38",
        "48",
        "4",
        "7",
        "\x07",
        "\\",
        "\r",
        "\n",
        "\t",
        "a",
        "b",
        "é",
        "漢",
        "\u{9b}",
        "\u{9d}",
        "\u{9c}",
        "\u{90}",
        "\x08",
        "\x7f",
        "P",
        "?",
        "h",
        "https://x.y",
        " ",
    ];

    fn noise(tc: &TestCase) -> String {
        let n = tc.draw(gs::integers::<usize>().max_value(80));
        (0..n).map(|_| ALPHABET[tc.draw(gs::integers::<usize>().max_value(ALPHABET.len() - 1))]).collect()
    }

    #[hegel::test(test_cases = 1000)]
    fn no_escape_reaches_the_text_and_splits_change_nothing(tc: TestCase) {
        let input = noise(&tc);
        let whole = fed(&[&input]);
        for chunk in whole.chunks() {
            assert!(
                !chunk.text.chars().any(|c| c == '\x1b' || (c.is_control() && c != '\n' && c != '\t')),
                "{:?} from {input:?}",
                chunk.text
            );
            for span in &chunk.spans {
                assert!(span.end() as usize <= chunk.text.len() && chunk.text.is_char_boundary(span.start as usize));
            }
        }
        let mut cuts: Vec<usize> = (0..tc.draw(gs::integers::<usize>().max_value(6)))
            .map(|_| tc.draw(gs::integers::<usize>().max_value(input.len())))
            .filter(|&i| input.is_char_boundary(i))
            .collect();
        cuts.sort_unstable();
        let mut pieces = vec![];
        let mut at = 0;
        for cut in cuts {
            pieces.push(&input[at..cut]);
            at = cut;
        }
        pieces.push(&input[at..]);
        assert_eq!(fed(&pieces).chunks(), whole.chunks(), "{input:?} in {pieces:?}");
    }

    #[hegel::test(test_cases = 1000)]
    fn a_carriage_return_rewrites_only_the_last_line(tc: TestCase) {
        let before = noise(&tc);
        let after: String = noise(&tc).replace('\n', "");
        let old = lines(&fed(&[&before]));
        let new = lines(&fed(&[&before, "\r", &after]));
        // An OSC or CSI left open by `before` may swallow the rest.
        let kept = old.len().saturating_sub(1);
        assert!(new.len() >= kept, "{before:?} then {after:?}");
        assert_eq!(new[..kept], old[..kept], "{before:?} then {after:?}");
    }

    #[test]
    fn styles_carry_across_lines_and_chunks() {
        let mut log = Log::default();
        log.feed("\x1b[31mred\n");
        for _ in 0..CHUNK_LINES {
            log.feed("still\n");
        }
        log.feed("\x1b[0mplain");
        let chunks = log.chunks();
        assert_eq!(chunks.len(), 2);
        assert_eq!(chunks[1].spans, vec![Span { start: 0, len: 5, attr: Attr::Color(Color::Palette(1)) }]);
        assert_eq!(chunks[1].text, "still\nplain");
    }

    #[test]
    fn sgr_colours_in_every_form() {
        let log = fed(&["\x1b[38;5;208ma\x1b[38:2::1:2:3mb\x1b[48;2;4;5;6mc\x1b[7md\x1b[39;49;27;95me\x1b[1;3;4;9mf\x1b[22;23;24;29mg"]);
        let line = &lines(&log)[0];
        assert_eq!(line.0, "abcdefg");
        let at = |i: u32| {
            let mut attrs: Vec<String> = line.1.iter().filter(|s| s.0 <= i && i < s.0 + s.1).map(|s| s.2.clone()).collect();
            attrs.sort();
            attrs
        };
        assert_eq!(at(0), ["Color(Palette(208))"]);
        assert_eq!(at(1), ["Color(Rgb(1, 2, 3))"]);
        assert_eq!(at(2), ["Background(Rgb(4, 5, 6))", "Color(Rgb(1, 2, 3))"]);
        // Inverse swaps them.
        assert_eq!(at(3), ["Background(Rgb(1, 2, 3))", "Color(Rgb(4, 5, 6))"]);
        assert_eq!(at(4), ["Color(Palette(13))"]);
        assert_eq!(at(5).len(), 5);
        assert_eq!(at(6), ["Color(Palette(13))"]);
        // Inverse on the defaults names the theme's.
        let line = &lines(&fed(&["\x1b[7mx"]))[0];
        assert_eq!(line.1.iter().map(|s| s.2.as_str()).collect::<Vec<_>>(), ["Background(Foreground)", "Color(Background)"]);
    }

    #[test]
    fn links_and_dropped_escapes() {
        let log = fed(&["\x1b]8;id=1;https://example.com\x1b\\site\x1b]8;;\x1b\\ \x1b]0;title\x07\x1b[2J\x1bPdata\x1b\\done"]);
        assert_eq!(lines(&log), vec![("site done".to_owned(), vec![(0, 4, "Link(https://example.com)".to_owned())])]);
    }

    #[test]
    fn only_web_and_mail_links_become_links() {
        let link = |uri: &str| lines(&fed(&[&format!("\x1b]8;;{uri}\x07x\x1b]8;;\x07")]))[0].1.clone();
        assert_eq!(link("https://example.com").len(), 1);
        assert_eq!(link("MAILTO:a@example.com").len(), 1);
        for uri in ["javascript:alert(1)", "file:///etc/passwd", "vscode://x", "no-scheme"] {
            assert_eq!(link(uri), vec![], "{uri}");
        }
    }

    #[test]
    fn an_overlong_osc_is_dropped_whole_and_its_link_with_it() {
        let long = format!("a\x1b]8;;https://example.com/{}\x07b\x1b]8;;\x07c", "x".repeat(OSC_CAP));
        assert_eq!(lines(&fed(&[&long])), vec![("abc".to_owned(), vec![])]);
    }

    #[test]
    fn carriage_returns_overwrite_and_long_lines_break() {
        assert_eq!(lines(&fed(&["10%\r50%\r100%\nnext\r\n"]))[..2].iter().map(|l| l.0.as_str()).collect::<Vec<_>>(), ["100%", "next"]);
        assert_eq!(lines(&fed(&["abcdef\rxy"]))[0].0, "xycdef");
        let long = "x".repeat(LINE_CAP + 10);
        let lines = lines(&fed(&[&long]));
        assert_eq!((lines[0].0.len(), lines[1].0.len()), (LINE_CAP, 10));
    }
}
