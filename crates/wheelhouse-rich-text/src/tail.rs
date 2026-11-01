//! A streaming message's unfinished tail (#540, decisions 3 and 4).

/// What closes the tail's unmatched inline markup, remend-style: code
/// spans, emphasis, strikethrough and a link's destination. Only the last
/// paragraph is looked at, never fenced code, and a marker with nothing
/// after it yet is left alone, so `**` doesn't become a rule.
pub(crate) fn repair(tail: &str) -> String {
    let mut start = 0;
    let mut fence: Option<(char, usize)> = None;
    let mut at = 0;
    for raw in tail.split_inclusive('\n') {
        let line = raw.trim_end_matches(['\n', '\r']);
        let next = at + raw.len();
        match fence {
            Some((marker, len)) => {
                let indent = line.len() - line.trim_start_matches(' ').len();
                let rest = line[indent..].trim_end_matches([' ', '\t']);
                if indent <= 3 && rest.len() >= len && rest.chars().all(|c| c == marker) {
                    fence = None;
                    start = next;
                }
            }
            None if line.trim().is_empty() => start = next,
            None => {
                if let Some((marker, len, _, _)) = opener(line) {
                    fence = Some((marker, len));
                }
            }
        }
        at = next;
    }
    if fence.is_some() {
        return String::new();
    }
    let text = &tail[start..];
    let chars: Vec<char> = text.chars().collect();
    let mut open: Vec<&'static str> = vec![];
    let mut code: Option<usize> = None;
    let mut link_destination = false;
    let mut i = 0;
    let content_after = |from: usize| chars[from..].iter().any(|c| !c.is_whitespace());
    while i < chars.len() {
        let c = chars[i];
        let run = chars[i..].iter().take_while(|&&d| d == c).count();
        if c == '`' {
            match code {
                Some(n) if n == run => code = None,
                None => code = Some(run),
                _ => {}
            }
            i += run;
            continue;
        }
        if code.is_some() {
            i += 1;
            continue;
        }
        let line_start = i == 0 || chars[i - 1] == '\n';
        let before = if i == 0 { ' ' } else { chars[i - 1] };
        let after = chars.get(i + run).copied().unwrap_or(' ');
        match c {
            '\\' => {
                i += 2;
                continue;
            }
            '*' | '_' if run <= 3 => {
                let bullet = c == '*' && run == 1 && line_start && after == ' ';
                let spaced = before.is_whitespace() && after.is_whitespace();
                let intraword = c == '_' && before.is_alphanumeric() && after.is_alphanumeric();
                if !bullet && !spaced && !intraword {
                    let (one, two) = if c == '*' { ("*", "**") } else { ("_", "__") };
                    let markers: &[&'static str] = match run {
                        1 => &[one],
                        2 => &[two],
                        _ => &[two, one],
                    };
                    for &marker in markers {
                        if let Some(at) = open.iter().rposition(|&m| m == marker) {
                            open.remove(at);
                        } else if content_after(i + run) {
                            open.push(marker);
                        }
                    }
                }
            }
            '~' if run == 2 => {
                if let Some(at) = open.iter().rposition(|&m| m == "~~") {
                    open.remove(at);
                } else if content_after(i + run) {
                    open.push("~~");
                }
            }
            ']' if chars.get(i + 1) == Some(&'(') => {
                link_destination = true;
                i += 2;
                continue;
            }
            ')' if link_destination => link_destination = false,
            _ => {}
        }
        i += run;
    }
    let mut out = String::new();
    if let Some(n) = code {
        out.push_str(&"`".repeat(n));
    }
    if link_destination {
        out.push(')');
    }
    for marker in open.iter().rev() {
        out.push_str(marker);
    }
    out
}

/// An open fenced code Block's fence.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Fence {
    marker: char,
    len: usize,
    indent: usize,
    /// Its info string's first word.
    pub language: String,
    /// Where its content starts, in the message's markdown.
    pub content: usize,
    /// Where the next look for a closing fence starts: the last line that
    /// was incomplete.
    pub scan: usize,
}

fn opener(line: &str) -> Option<(char, usize, usize, String)> {
    let indent = line.len() - line.trim_start_matches(' ').len();
    if indent > 3 {
        return None;
    }
    let rest = &line[indent..];
    let marker = rest.chars().next().filter(|&c| c == '`' || c == '~')?;
    let len = rest.chars().take_while(|&c| c == marker).count();
    if len < 3 || (marker == '`' && rest[len..].contains('`')) {
        return None;
    }
    let language = rest[len..].split_whitespace().next().unwrap_or("").to_owned();
    Some((marker, len, indent, language))
}

impl Fence {
    /// The fence of a fenced code Block at `start` that `markdown` leaves
    /// open; None if its opening line isn't complete yet.
    pub fn open_at(markdown: &str, start: usize) -> Option<Fence> {
        let block = &markdown[start..];
        let newline = block.find('\n')?;
        let (marker, len, indent, language) = opener(block[..newline].trim_end_matches('\r'))?;
        let mut fence = Fence { marker, len, indent, language, content: start + newline + 1, scan: start + newline + 1 };
        if fence.closes(markdown) {
            None
        } else {
            Some(fence)
        }
    }

    /// Whether a line from `scan` on closes it, the last line even before
    /// its newline. Moves `scan` to the last line.
    pub fn closes(&mut self, markdown: &str) -> bool {
        let mut at = self.scan;
        for raw in markdown[self.scan..].split_inclusive('\n') {
            let line = raw.trim_end_matches(['\n', '\r']);
            let indent = line.len() - line.trim_start_matches(' ').len();
            let rest = line[indent..].trim_end_matches([' ', '\t']);
            if indent <= 3 && rest.len() >= self.len && rest.chars().all(|c| c == self.marker) {
                return true;
            }
            if raw.ends_with('\n') {
                at += raw.len();
            }
        }
        self.scan = at;
        false
    }

    /// The code Block's text as the parser would give it: each line less up
    /// to the fence's indent, without the last newline.
    pub fn text(&self, markdown: &str) -> String {
        let mut out = String::with_capacity(markdown.len() - self.content);
        for line in markdown[self.content..].split_inclusive('\n') {
            let strip = line.len() - line.trim_start_matches(' ').len();
            out.push_str(&line[strip.min(self.indent)..]);
        }
        if out.ends_with('\n') {
            out.pop();
        }
        out
    }
}
