use std::ops::Range;

use conversation_events::{AgentCommand, FileKind, FileMatch};

/// The most rows a popup shows at once; it scrolls past that (#701 D5).
pub const VISIBLE_ROWS: usize = 8;
/// The most files a search is asked for.
pub const FILE_LIMIT: u32 = 50;

/// Which popup is open (wheelhouse#694 C7–C9).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Popup {
    /// `/` as the field's first character: the Agent Commands.
    Command,
    /// An `@` token: the `@` Agent Commands, then the root's files.
    Mention,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RowKind {
    Command,
    File,
    Dir,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Row {
    pub kind: RowKind,
    /// A command's name, or a file's path.
    pub name: String,
    pub description: String,
    pub group: Option<String>,
    /// Exactly what accepting it inserts: a command's `insert`, `@path`.
    pub insert: String,
    hint: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Key {
    Up,
    Down,
    /// Tab or Enter.
    Accept,
    Escape,
}

/// Replace `range` of the field's text (byte offsets) with `text`; the
/// cursor goes after it. It is made for the text last passed to
/// [`Completion::update`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Edit {
    pub range: Range<usize>,
    pub text: String,
}

impl Edit {
    /// None for a text the range doesn't fit, as one changed since.
    pub fn apply(&self, text: &str) -> Option<String> {
        Some(format!("{}{}{}", text.get(..self.range.start)?, self.text, text.get(self.range.end..)?))
    }
}

/// The token the cursor is in, if it opens a popup.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Token {
    popup: Popup,
    /// The whole token, sigil included, past the cursor to its end.
    range: Range<usize>,
    /// What is typed after the sigil, up to the cursor.
    query: String,
}

fn token(text: &str, cursor: usize) -> Option<Token> {
    if cursor > text.len() || !text.is_char_boundary(cursor) {
        return None;
    }
    let start = text[..cursor].rfind(char::is_whitespace).map_or(0, |at| at + text[at..].chars().next().map_or(1, char::len_utf8));
    let end = text[cursor..].find(char::is_whitespace).map_or(text.len(), |at| cursor + at);
    let word = &text[start..cursor];
    let popup = match word.chars().next()? {
        '/' if start == 0 => Popup::Command,
        '@' => Popup::Mention,
        _ => return None,
    };
    Some(Token { popup, range: start..end, query: word[1..].to_owned() })
}

/// How well `query` matches `candidate`, lower first: a prefix (ties keep
/// the producer's order), then a substring nearer the start, then letters
/// in order spread less widely.
fn score(query: &str, candidate: &str) -> Option<(u8, usize)> {
    let (query, candidate) = (query.to_lowercase(), candidate.to_lowercase());
    if candidate.starts_with(&query) {
        return Some((0, 0));
    }
    if let Some(at) = candidate.find(&query) {
        return Some((1, at));
    }
    let mut chars = candidate.char_indices();
    let mut first = None;
    let mut last = 0;
    for wanted in query.chars() {
        let (at, _) = chars.find(|(_, c)| *c == wanted)?;
        first.get_or_insert(at);
        last = at;
    }
    Some((2, last - first.unwrap_or(0)))
}

fn command_score(query: &str, command: &AgentCommand) -> Option<(u8, usize)> {
    std::iter::once(&command.name).chain(&command.aliases).filter_map(|name| score(query, name)).min()
}

/// What the popup shows; compared to tell whether a call changed it.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct Shown {
    popup: Option<Popup>,
    rows: Vec<Row>,
    selected: usize,
    first: usize,
    hint: Option<(usize, String)>,
}

/// The Composer's `/` and `@` completion, as a pure model (wheelhouse#694
/// C7–C9, #701 D5). The host feeds it the field and the thread's data, asks
/// a root's search when it says, and applies the edits it returns. Text is
/// always sent as typed: nothing here touches what is sent.
#[derive(Debug, Default)]
pub struct Completion {
    text: String,
    commands: Option<Vec<AgentCommand>>,
    token: Option<Token>,
    shown: Shown,
    /// A popup closed by Esc or an insert stays closed while the cursor is
    /// in that token.
    dismissed: Option<(Popup, usize)>,
    /// The root searched, and the query its search was last asked.
    root: Option<String>,
    asked: Option<String>,
    files: Option<(String, Vec<FileMatch>)>,
    /// A command's argument hint, after its insert, until the text changes:
    /// the text it was made for, where it shows and what it says.
    hint: Option<(String, usize, String)>,
    revision: u64,
}

impl Completion {
    /// The field now: its text and cursor (a byte offset), the thread's
    /// `commands` document if it has one, and the root searched, if any.
    /// Returns a query to ask the root's search for, when one is newly
    /// wanted: another root is asked again.
    pub fn update(&mut self, commands: Option<&[AgentCommand]>, root: Option<&str>, text: &str, cursor: usize) -> Option<String> {
        if self.root.as_deref() != root {
            self.root = root.map(str::to_owned);
            (self.asked, self.files) = (None, None);
        }
        if self.hint.as_ref().is_some_and(|(made_for, ..)| made_for != text) {
            self.hint = None;
        }
        self.text = text.to_owned();
        let token = token(text, cursor);
        let same = |a: &Option<Token>, b: &Option<Token>| {
            a.as_ref().map(|t| (t.popup, t.range.start)) == b.as_ref().map(|t| (t.popup, t.range.start))
        };
        if !same(&token, &self.token) {
            self.dismissed = None;
        }
        let query_changed = token.as_ref().map(|t| &t.query) != self.token.as_ref().map(|t| &t.query) || !same(&token, &self.token);
        self.token = token;
        let mut search = None;
        match &self.token {
            Some(Token { popup: Popup::Mention, query, .. }) if self.root.is_some() && !query.is_empty() => {
                if self.asked.as_ref() != Some(query) {
                    self.asked = Some(query.clone());
                    search = Some(query.clone());
                }
            }
            _ => self.asked = None,
        }
        if self.commands.as_deref() != commands {
            self.commands = commands.map(<[AgentCommand]>::to_vec);
        }
        self.show(query_changed);
        search
    }

    /// The root's search answered `query`; an answer to any other query
    /// than the one asked last is stale and dropped.
    pub fn answer(&mut self, query: &str, files: Vec<FileMatch>) {
        if self.asked.as_deref() == Some(query) {
            self.files = Some((query.to_owned(), files));
            self.show(false);
        }
    }

    fn show(&mut self, reset: bool) {
        let open = self.token.as_ref().filter(|t| self.dismissed != Some((t.popup, t.range.start)));
        let rows = open.map(|token| self.matching(token)).unwrap_or_default();
        let popup = open.map(|t| t.popup).filter(|_| !rows.is_empty());
        let (mut selected, mut first) = (self.shown.selected, self.shown.first);
        if reset || popup != self.shown.popup {
            (selected, first) = (0, 0);
        }
        selected = selected.min(rows.len().saturating_sub(1));
        first = first.min(selected).max((selected + 1).saturating_sub(VISIBLE_ROWS));
        let hint = self.hint.as_ref().map(|(_, at, hint)| (*at, hint.clone()));
        let shown = Shown { popup, rows, selected, first, hint };
        if shown != self.shown {
            self.revision += 1;
        }
        self.shown = shown;
    }

    fn matching(&self, token: &Token) -> Vec<Row> {
        let commands = self.commands.as_deref().unwrap_or_default();
        let wants = |command: &AgentCommand| command.insert.starts_with('@') == (token.popup == Popup::Mention);
        let mut scored: Vec<_> = commands
            .iter()
            .enumerate()
            .filter(|(_, command)| wants(command))
            .filter_map(|(at, command)| Some((command_score(&token.query, command)?, at, command)))
            .collect();
        scored.sort_by_key(|(score, at, _)| (*score, *at));
        let mut rows: Vec<Row> = scored
            .into_iter()
            .map(|(_, _, c)| Row {
                kind: RowKind::Command,
                name: c.name.clone(),
                description: c.description.clone(),
                group: c.group.clone(),
                insert: c.insert.clone(),
                hint: c.hint.clone(),
            })
            .collect();
        if let Some((_, files)) = self.files.as_ref().filter(|(query, _)| token.popup == Popup::Mention && *query == token.query) {
            rows.extend(files.iter().map(|file| Row {
                kind: if file.kind == FileKind::Dir { RowKind::Dir } else { RowKind::File },
                name: file.path.clone(),
                description: String::new(),
                group: None,
                insert: format!("@{}", file.path),
                hint: None,
            }));
        }
        rows
    }

    /// A key while the field has focus: whether the popup took it, and the
    /// edit Tab or Enter makes. Keys the popup doesn't take (any, while it
    /// is closed) are the field's own: Enter sends.
    pub fn key(&mut self, key: Key) -> (bool, Option<Edit>) {
        let (Some(popup), Some(token)) = (self.shown.popup, self.token.clone()) else {
            return (false, None);
        };
        // Keys move the selection, close the popup or set a hint; the rows
        // go only with the popup.
        let look = |shown: &Shown| (shown.popup, shown.selected, shown.first, shown.hint.clone());
        let before = look(&self.shown);
        let count = self.shown.rows.len();
        let mut edit = None;
        match key {
            Key::Up => self.shown.selected = (self.shown.selected + count - 1) % count,
            Key::Down => self.shown.selected = (self.shown.selected + 1) % count,
            Key::Escape => self.close(popup, &token),
            Key::Accept => {
                let row = self.shown.rows[self.shown.selected].clone();
                let made = Edit { range: token.range.clone(), text: row.insert.clone() };
                let text = made.apply(&self.text).expect("made for this text");
                // A folder is gone into: its files are what comes next.
                if row.kind != RowKind::Dir {
                    self.close(popup, &token);
                }
                self.hint = row.hint.map(|hint| (text.clone(), token.range.start + row.insert.len(), hint));
                self.shown.hint = self.hint.as_ref().map(|(_, at, hint)| (*at, hint.clone()));
                edit = Some(made);
            }
        }
        let selected = self.shown.selected;
        self.shown.first = self.shown.first.min(selected).max((selected + 1).saturating_sub(VISIBLE_ROWS));
        if look(&self.shown) != before {
            self.revision += 1;
        }
        (true, edit)
    }

    fn close(&mut self, popup: Popup, token: &Token) {
        self.dismissed = Some((popup, token.range.start));
        self.shown.popup = None;
        self.shown.rows.clear();
        (self.shown.selected, self.shown.first) = (0, 0);
    }

    pub fn popup(&self) -> Option<Popup> {
        self.shown.popup
    }

    /// Every row, in order; empty while no popup is open.
    pub fn rows(&self) -> &[Row] {
        &self.shown.rows
    }

    /// The index of the first row shown, and the rows shown from it.
    pub fn visible(&self) -> (usize, &[Row]) {
        let first = self.shown.first;
        (first, &self.shown.rows[first..self.shown.rows.len().min(first + VISIBLE_ROWS)])
    }

    /// The selected row's index into [`Completion::rows`].
    pub fn selected(&self) -> Option<usize> {
        self.shown.popup.map(|_| self.shown.selected)
    }

    /// Ghost text after an inserted command, and the byte offset it shows
    /// at, until the user types.
    pub fn hint(&self) -> Option<(usize, &str)> {
        self.shown.hint.as_ref().map(|(at, hint)| (*at, hint.as_str()))
    }

    /// Moves whenever what the popup or hint shows changes.
    pub fn revision(&self) -> u64 {
        self.revision
    }
}

#[cfg(test)]
mod tests;
