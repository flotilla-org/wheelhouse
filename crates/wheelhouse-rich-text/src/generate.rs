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
