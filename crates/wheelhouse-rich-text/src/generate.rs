//! Synthetic markdown: the repo is public. A document is a list of
//! top-level blocks, so an edit can replace, insert or remove one. Words are
//! lowercase letters, so a cut never lands on markdown punctuation by chance.

/// A seeded generator, or a property test's draws.
pub trait Choose {
    /// 0 if `n` is 0.
    fn below(&mut self, n: usize) -> usize;

    fn chance(&mut self, one_in: usize) -> bool {
        self.below(one_in) == 0
    }
}

/// splitmix64.
pub struct Seeded(pub u64);

impl Choose for Seeded {
    fn below(&mut self, n: usize) -> usize {
        if n == 0 {
            return 0;
        }
        self.0 = self.0.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        ((z ^ (z >> 31)) % n as u64) as usize
    }
}

const SYLLABLES: &[&str] = &["ka", "lo", "mi", "ne", "ru", "ta", "vo", "zen", "pi", "sa", "do", "fe"];

pub fn word(c: &mut impl Choose) -> String {
    let n = 1 + c.below(3);
    (0..n).map(|_| SYLLABLES[c.below(SYLLABLES.len())]).collect()
}

fn words(c: &mut impl Choose, min: usize, max: usize) -> String {
    let n = min + c.below(max - min + 1);
    (0..n).map(|_| word(c)).collect::<Vec<_>>().join(" ")
}

pub fn inline(c: &mut impl Choose, min: usize, max: usize) -> String {
    let n = min + c.below(max - min + 1);
    let mut parts = vec![];
    for _ in 0..n {
        parts.push(match c.below(14) {
            0 => format!("*{}*", words(c, 1, 3)),
            1 => format!("**{}**", words(c, 1, 3)),
            2 => format!("`{}`", words(c, 1, 2)),
            3 => format!("[{}](https://example.com/{})", words(c, 1, 2), word(c)),
            4 => format!("~~{}~~", words(c, 1, 2)),
            5 => format!("${}^{}$", word(c), 1 + c.below(3)),
            6 => format!("[[{}]]", word(c)),
            7 => format!("[[{}|{}]]", word(c), words(c, 1, 2)),
            8 => format!("[^{}]", 1 + c.below(3)),
            9 => format!("*{} **{}** {}*", word(c), word(c), word(c)),
            _ => word(c),
        });
    }
    parts.join(" ")
}

fn paragraph(c: &mut impl Choose, indent: &str) -> String {
    let lines = 1 + c.below(3);
    (0..lines).map(|i| format!("{}{}", if i == 0 { "" } else { indent }, inline(c, 2, 8))).collect::<Vec<_>>().join("\n")
}

fn code(c: &mut impl Choose, indent: &str) -> String {
    let languages = ["rust", "python", "", "sh"];
    let language = languages[c.below(languages.len())];
    let lines: Vec<String> = (0..1 + c.below(4))
        .map(|_| match c.below(3) {
            0 => format!("let {} = {};", word(c), c.below(100)),
            1 => format!("    {}({});", word(c), word(c)),
            _ => format!("fn {}() {{}}", word(c)),
        })
        .collect();
    let body: String = lines.iter().map(|l| format!("{indent}{l}\n")).collect();
    format!("```{language}\n{body}{indent}```")
}

fn list(c: &mut impl Choose, depth: usize, indent: &str) -> String {
    let ordered = c.chance(3);
    let tasks = !ordered && c.chance(4);
    let loose = c.chance(4);
    let start = 1 + c.below(9);
    let items = 1 + c.below(4);
    let mut out = vec![];
    for i in 0..items {
        let marker = if ordered { format!("{}. ", start + i) } else { "- ".to_owned() };
        let inner = format!("{indent}{}", " ".repeat(marker.len()));
        let task = if tasks {
            if c.chance(2) {
                "[x] "
            } else {
                "[ ] "
            }
        } else {
            ""
        };
        let mut item = format!("{indent}{marker}{task}{}", paragraph(c, &inner));
        if depth < 2 && c.chance(4) {
            item.push_str(if loose { "\n\n" } else { "\n" });
            item.push_str(&list(c, depth + 1, &inner));
        } else if loose && c.chance(3) {
            item.push_str("\n\n");
            item.push_str(&inner);
            item.push_str(&paragraph(c, &inner));
        }
        out.push(item);
    }
    out.join(if loose { "\n\n" } else { "\n" })
}

fn quote(c: &mut impl Choose) -> String {
    let alerts = ["NOTE", "TIP", "IMPORTANT", "WARNING", "CAUTION"];
    let mut lines = vec![];
    if c.chance(3) {
        lines.push(format!("> [!{}]", alerts[c.below(alerts.len())]));
    }
    let paragraphs = 1 + c.below(2);
    for p in 0..paragraphs {
        if p > 0 {
            lines.push(">".to_owned());
        }
        for line in paragraph(c, "").lines() {
            lines.push(format!("> {line}"));
        }
    }
    lines.join("\n")
}

fn cell(c: &mut impl Choose) -> String {
    match c.below(4) {
        0 => format!("*{}*", word(c)),
        1 => format!("`{}`", word(c)),
        _ => words(c, 1, 2),
    }
}

fn row(c: &mut impl Choose, columns: usize) -> String {
    let cells: Vec<String> = (0..columns).map(|_| cell(c)).collect();
    format!("| {} |", cells.join(" | "))
}

fn table(c: &mut impl Choose) -> String {
    let columns = 2 + c.below(2);
    let rows = 1 + c.below(3);
    let aligns = [":--", "--:", ":-:", "---"];
    let mut out = vec![row(c, columns)];
    let delimiter: Vec<&str> = (0..columns).map(|_| aligns[c.below(aligns.len())]).collect();
    out.push(format!("|{}|", delimiter.join("|")));
    for _ in 0..rows {
        out.push(row(c, columns));
    }
    out.join("\n")
}

pub fn block(c: &mut impl Choose) -> String {
    match c.below(20) {
        0..=6 => paragraph(c, ""),
        7 | 8 => format!("{} {}", "#".repeat(1 + c.below(6)), inline(c, 1, 5)),
        9 | 10 => code(c, ""),
        11..=13 => list(c, 0, ""),
        14 | 15 => quote(c),
        16 => "---".to_owned(),
        17 => table(c),
        18 => format!("$$\n{}^{} + {}\n$$", word(c), 1 + c.below(3), word(c)),
        _ => format!("[^{}]: {}", 1 + c.below(3), inline(c, 1, 5)),
    }
}

/// Perhaps with front matter.
pub fn document(c: &mut impl Choose, blocks: usize) -> Vec<String> {
    let mut out = vec![];
    if c.chance(4) {
        out.push(format!("---\ntitle: {}\ntags: [{}]\n---", words(c, 1, 3), word(c)));
    }
    out.extend((0..blocks).map(|_| block(c)));
    out
}

pub fn render(blocks: &[String]) -> String {
    let mut out = blocks.join("\n\n");
    out.push('\n');
    out
}

/// At least `lines` lines.
pub fn large(seed: u64, lines: usize) -> Vec<String> {
    let mut c = Seeded(seed);
    let mut out = document(&mut c, 0);
    // Blank lines between blocks; none after the last.
    let mut count = 0;
    while count <= lines {
        let block = block(&mut c);
        count += block.lines().count() + 1;
        out.push(block);
    }
    out
}

pub fn edit(c: &mut impl Choose, blocks: &mut Vec<String>) {
    let front = blocks.first().is_some_and(|b| b.starts_with("---\n"));
    let first = usize::from(front);
    let len = blocks.len();
    match c.below(6) {
        0 if len > first => {
            let at = first + c.below(len - first);
            blocks[at] = block(c);
        }
        1 if len > first => {
            let at = first + c.below(len - first);
            blocks.remove(at);
        }
        2 if len > first + 1 => {
            let from = first + c.below(len - first);
            let to = first + c.below(len - first);
            let b = blocks.remove(from);
            blocks.insert(to, b);
        }
        3 if len > first => {
            let at = first + c.below(len - first);
            let words: Vec<&str> = blocks[at].split(' ').collect();
            let i = c.below(words.len());
            let changed: String = words
                .iter()
                .enumerate()
                .map(|(j, w)| if i == j { format!("{w}{}", word(c)) } else { (*w).to_owned() })
                .collect::<Vec<_>>()
                .join(" ");
            blocks[at] = changed;
        }
        _ => {
            let at = first + c.below(len - first + 1);
            blocks.insert(at, block(c));
        }
    }
}

/// Synthetic conversations in the contract's stream form (#548).
pub mod conversation {
    use std::collections::HashMap;

    use conversation_events::{
        Custom, Event, FunctionCall, Hello, Message, MessagesSnapshot, Parent, ReasoningMessageContent, ReasoningMessageEnd,
        ReasoningMessageStart, Role, RunError, RunFinished, RunStarted, TextMessageContent, TextMessageEnd, TextMessageStart, ToolCall,
        ToolCallArgs, ToolCallEnd, ToolCallResult, ToolCallStart, ToolKind, ToolStatus, Truncated, CONTRACT,
    };
    use serde_json::json;

    use super::{block, inline, word, words, Choose, Seeded};

    #[derive(Default)]
    struct Writer {
        events: Vec<Event>,
        seq: HashMap<String, u64>,
        ids: u64,
        /// The last text message, for a snapshot to repeat.
        last: Option<(String, String, Role, String)>,
    }

    impl Writer {
        fn seq(&mut self, thread: &str) -> u64 {
            let seq = self.seq.entry(thread.to_owned()).or_default();
            *seq += 1;
            *seq
        }

        fn id(&mut self, prefix: &str) -> String {
            self.ids += 1;
            format!("{prefix}{}", self.ids)
        }

        fn message(&mut self, c: &mut impl Choose, thread: &str, role: Role, text: &str) {
            let id = self.id("m");
            let t = thread.to_owned();
            let seq = self.seq(thread);
            self.events.push(Event::TextMessageStart(TextMessageStart { thread_id: t.clone(), seq, message_id: id.clone(), role }));
            for delta in pieces(c, text) {
                let seq = self.seq(thread);
                self.events.push(Event::TextMessageContent(TextMessageContent {
                    thread_id: t.clone(),
                    seq,
                    message_id: id.clone(),
                    delta,
                }));
            }
            let seq = self.seq(thread);
            self.events.push(Event::TextMessageEnd(TextMessageEnd { thread_id: t.clone(), seq, message_id: id.clone() }));
            self.last = Some((t, id, role, text.to_owned()));
        }

        fn reasoning(&mut self, c: &mut impl Choose, thread: &str) {
            let id = self.id("rs");
            let t = thread.to_owned();
            let seq = self.seq(thread);
            self.events.push(Event::ReasoningMessageStart(ReasoningMessageStart {
                thread_id: t.clone(),
                seq,
                message_id: id.clone(),
                role: None,
            }));
            let text = format!("Summary: {}", inline(c, 2, 6));
            for delta in pieces(c, &text) {
                let seq = self.seq(thread);
                self.events.push(Event::ReasoningMessageContent(ReasoningMessageContent {
                    thread_id: t.clone(),
                    seq,
                    message_id: id.clone(),
                    delta,
                }));
            }
            let seq = self.seq(thread);
            self.events.push(Event::ReasoningMessageEnd(ReasoningMessageEnd { thread_id: t, seq, message_id: id }));
        }

        /// A started call and its id.
        fn call(&mut self, thread: &str, name: &str, kind: ToolKind, title: String) -> String {
            let id = self.id("c");
            let t = thread.to_owned();
            let seq = self.seq(thread);
            self.events.push(Event::ToolCallStart(ToolCallStart {
                thread_id: t.clone(),
                seq,
                tool_call_id: id.clone(),
                tool_call_name: name.into(),
                parent_message_id: None,
                title: Some(title.clone()),
                kind: Some(kind),
            }));
            let seq = self.seq(thread);
            let delta = json!({ "input": title }).to_string();
            self.events.push(Event::ToolCallArgs(ToolCallArgs { thread_id: t.clone(), seq, tool_call_id: id.clone(), delta }));
            let seq = self.seq(thread);
            self.events.push(Event::ToolCallEnd(ToolCallEnd { thread_id: t, seq, tool_call_id: id.clone() }));
            id
        }

        fn result(
            &mut self,
            thread: &str,
            call: &str,
            media_type: &str,
            content: String,
            status: ToolStatus,
            truncated: Option<Truncated>,
        ) {
            let message_id = self.id("r");
            let seq = self.seq(thread);
            let structured = (media_type == "image/png").then(|| json!({ "alt": "a synthetic picture" }));
            self.events.push(Event::ToolCallResult(ToolCallResult {
                thread_id: thread.into(),
                seq,
                message_id,
                tool_call_id: call.into(),
                content,
                role: None,
                status: Some(status),
                media_type: Some(media_type.into()),
                structured,
                truncated,
            }));
        }

        fn tool(&mut self, c: &mut impl Choose, thread: &str, scale: usize) {
            let kinds = [ToolKind::Shell, ToolKind::Edit, ToolKind::Read, ToolKind::Search, ToolKind::Web, ToolKind::Mcp, ToolKind::Other];
            let kind = kinds[c.below(kinds.len())];
            let call = self.call(thread, &word(c), kind, words(c, 1, 3));
            let statuses = [ToolStatus::Ok, ToolStatus::Ok, ToolStatus::Failed, ToolStatus::Interrupted, ToolStatus::AwaitingApproval];
            let status = statuses[c.below(statuses.len())];
            let truncated =
                c.chance(6).then(|| Truncated { total_bytes: 70_000 + c.below(1000) as u64, reference: format!("log:{}", c.below(99)) });
            let (media, content) = match c.below(8) {
                0..=2 => ("text/x-ansi", ansi(c, scale)),
                3 => ("text/plain", (0..1 + c.below(4 * scale)).map(|_| format!("{}\n", words(c, 1, 8))).collect()),
                4 => ("text/markdown", super::render(&(0..1 + c.below(3)).map(|_| block(c)).collect::<Vec<_>>())),
                5 => ("text/x-diff", format!("--- a/{0}\n+++ b/{0}\n-{1}\n+{2}\n", word(c), word(c), word(c))),
                6 => ("image/png", String::new()),
                _ => ("application/octet-stream", words(c, 1, 4)),
            };
            self.result(thread, &call, media, content, status, truncated);
        }

        fn subagent(&mut self, c: &mut impl Choose, thread: &str, scale: usize) {
            let call = self.call(thread, "Task", ToolKind::Agent, words(c, 2, 4));
            let child = self.id("t");
            let (seq, run_id) = (self.seq(&child), self.id("run"));
            self.events.push(Event::RunStarted(RunStarted {
                thread_id: child.clone(),
                seq,
                run_id,
                parent_run_id: None,
                parent: Some(Parent { thread_id: thread.into(), tool_call_id: call.clone() }),
            }));
            let text = inline(c, 2, 6);
            self.message(c, &child, Role::User, &text);
            for _ in 0..1 + c.below(3) {
                self.item(c, &child, scale, false);
            }
            let seq = self.seq(&child);
            let outcome = if c.chance(4) { "interrupted" } else { "completed" };
            self.events.push(Event::RunFinished(RunFinished {
                thread_id: child,
                seq,
                outcome: Some(outcome.into()),
                ..RunFinished::default()
            }));
            self.result(thread, &call, "text/markdown", inline(c, 2, 6), ToolStatus::Ok, None);
        }

        fn snapshot(&mut self, c: &mut impl Choose, thread: &str) {
            let mut messages = vec![];
            // A late joiner's snapshot repeats items it already has.
            if let Some((t, id, role, text)) = self.last.clone().filter(|(t, ..)| t == thread) {
                let _ = t;
                messages.push(Message { id, role, content: Some(json!(format!("{text} {}", word(c)))), ..Message::default() });
            }
            let (user, assistant, call, result) = (self.id("m"), self.id("m"), self.id("c"), self.id("r"));
            messages.push(Message { id: user, role: Role::User, content: Some(json!(inline(c, 1, 4))), ..Message::default() });
            messages.push(Message {
                id: assistant,
                role: Role::Assistant,
                content: Some(json!(inline(c, 1, 6))),
                tool_calls: vec![ToolCall {
                    id: call.clone(),
                    call_type: "function".into(),
                    function: FunctionCall { name: "Bash".into(), arguments: "{}".into() },
                    title: Some(words(c, 1, 2)),
                    kind: Some(ToolKind::Shell),
                }],
                ..Message::default()
            });
            messages.push(Message {
                id: result,
                role: Role::Tool,
                content: Some(json!(format!("{}\n", words(c, 1, 5)))),
                tool_call_id: Some(call),
                status: Some(ToolStatus::Ok),
                media_type: Some("text/plain".into()),
                ..Message::default()
            });
            let seq = self.seq(thread);
            self.events.push(Event::MessagesSnapshot(MessagesSnapshot { thread_id: thread.into(), seq, messages }));
        }

        fn item(&mut self, c: &mut impl Choose, thread: &str, scale: usize, top: bool) {
            match c.below(24) {
                0..=4 => {
                    let text = inline(c, 2, 10);
                    self.message(c, thread, Role::User, &text)
                }
                5..=10 => {
                    let blocks: Vec<String> = (0..1 + c.below(2 * scale)).map(|_| block(c)).collect();
                    self.message(c, thread, Role::Assistant, &super::render(&blocks));
                }
                11 | 12 => self.reasoning(c, thread),
                13..=19 => self.tool(c, thread, scale),
                20 if top => self.subagent(c, thread, scale),
                21 => {
                    let seq = self.seq(thread);
                    self.events.push(Event::Custom(Custom { thread_id: thread.into(), seq, name: "compaction".into(), value: json!({}) }));
                }
                22 => {
                    let seq = self.seq(thread);
                    self.events.push(Event::RunError(RunError { thread_id: thread.into(), seq, message: words(c, 2, 5), code: None }));
                }
                23 if top => self.snapshot(c, thread),
                _ => {
                    let text = words(c, 2, 6);
                    self.message(c, thread, Role::System, &text)
                }
            }
        }
    }

    /// Text in up to four deltas, cut at character boundaries.
    fn pieces(c: &mut impl Choose, text: &str) -> Vec<String> {
        let mut cuts: Vec<usize> = (0..c.below(4)).map(|_| c.below(text.len() + 1)).filter(|&i| text.is_char_boundary(i)).collect();
        cuts.sort_unstable();
        let mut out = vec![];
        let mut at = 0;
        for cut in cuts {
            out.push(text[at..cut].to_owned());
            at = cut;
        }
        out.push(text[at..].to_owned());
        out
    }

    /// Coloured lines, a progress bar's carriage returns, and a link.
    fn ansi(c: &mut impl Choose, scale: usize) -> String {
        let mut out = String::new();
        for _ in 0..1 + c.below(6 * scale) {
            match c.below(6) {
                0 => out.push_str(&format!("\x1b[1;31merror\x1b[0m: {}\n", words(c, 1, 6))),
                1 => out.push_str(&format!("\x1b[38;5;{}m{}\x1b[39m {}\n", c.below(256), word(c), words(c, 1, 4))),
                2 => out.push_str(&format!("\x1b[48;2;{};{};{}m{}\x1b[49m\n", c.below(256), c.below(256), c.below(256), word(c))),
                3 => out.push_str(&format!("10%\r50%\r100% {}\n", word(c))),
                4 => out.push_str(&format!("see \x1b]8;;https://example.com/{0}\x07{0}\x1b]8;;\x07\n", word(c))),
                _ => out.push_str(&format!("{}\n", words(c, 1, 10))),
            }
        }
        out
    }

    /// A conversation of about `items` top-level items, every message ended.
    pub fn events(c: &mut impl Choose, items: usize) -> Vec<Event> {
        let mut w = Writer::default();
        w.events.push(Event::Hello(Hello { contract: CONTRACT }));
        let seq = w.seq("t0");
        w.events.push(Event::RunStarted(RunStarted { thread_id: "t0".into(), seq, run_id: "run0".into(), ..RunStarted::default() }));
        for _ in 0..items {
            w.item(c, "t0", 1, true);
        }
        w.events
    }

    pub fn jsonl(events: &[Event]) -> Vec<u8> {
        let mut out = vec![];
        conversation_events::write(&mut out, events).expect("a Vec takes writes");
        out
    }

    /// The same conversation with its deltas cut into more events.
    pub fn split_deltas(c: &mut impl Choose, events: &[Event]) -> Vec<Event> {
        let mut out = vec![];
        for event in events {
            match event {
                Event::TextMessageContent(e) => out.extend(
                    pieces(c, &e.delta).into_iter().map(|delta| Event::TextMessageContent(TextMessageContent { delta, ..e.clone() })),
                ),
                Event::ReasoningMessageContent(e) => out.extend(
                    pieces(c, &e.delta)
                        .into_iter()
                        .map(|delta| Event::ReasoningMessageContent(ReasoningMessageContent { delta, ..e.clone() })),
                ),
                Event::ToolCallArgs(e) => {
                    out.extend(pieces(c, &e.delta).into_iter().map(|delta| Event::ToolCallArgs(ToolCallArgs { delta, ..e.clone() })))
                }
                e => out.push(e.clone()),
            }
        }
        out
    }

    /// A transcript of at least `bytes` bytes, for scale.
    pub fn large(seed: u64, bytes: usize) -> Vec<u8> {
        let mut c = Seeded(seed);
        let mut w = Writer::default();
        w.events.push(Event::Hello(Hello { contract: CONTRACT }));
        let mut out = vec![];
        let mut written = 0;
        while out.len() < bytes {
            w.item(&mut c, "t0", 6, true);
            conversation_events::write(&mut out, &w.events[written..]).expect("a Vec takes writes");
            written = w.events.len();
        }
        out
    }
}
