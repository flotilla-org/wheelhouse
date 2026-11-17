use hegel::generators as gs;
use hegel::TestCase;

use super::*;

fn command(name: &str, insert: &str, hint: Option<&str>, aliases: &[&str], group: Option<&str>) -> AgentCommand {
    AgentCommand {
        name: name.into(),
        description: format!("{name}, described"),
        hint: hint.map(str::to_owned),
        aliases: aliases.iter().map(|a| a.to_string()).collect(),
        insert: insert.into(),
        group: group.map(str::to_owned),
    }
}

/// Synthetic entries of every shape the contract allows: `/`, `$` and `@`
/// inserts, hints, aliases, groups, and names that share prefixes, letters
/// and case.
fn pool() -> Vec<AgentCommand> {
    vec![
        command("compact", "/compact ", None, &[], None),
        command("review", "/review ", Some("[instructions]"), &["rv"], None),
        command("review-branch", "/review-branch ", Some("<branch>"), &[], None),
        command("init", "/init ", None, &[], None),
        command("Deploy", "/deploy ", None, &["ship"], None),
        command("lint", "$lint ", None, &[], Some("Skills")),
        command("agent-docs", "@agent-docs ", None, &[], Some("Agents")),
        command("agent-review", "@agent-review ", Some("<task>"), &["ar"], Some("Agents")),
    ]
}

/// None (no document), or a drawn subset of the pool in its order.
fn commands(tc: &TestCase) -> Option<Vec<AgentCommand>> {
    if tc.draw(gs::integers::<u8>().max_value(5)) == 0 {
        return None;
    }
    Some(pool().into_iter().filter(|_| tc.draw(gs::booleans())).collect())
}

/// A query: letters the pool's names share (and `/`), or a prefix, an inner
/// run or a scattering of one of its names or aliases, so every ranking
/// class is drawn often.
fn query(tc: &TestCase) -> String {
    let names: Vec<String> = pool().iter().flat_map(|c| names(c).cloned().collect::<Vec<_>>()).collect();
    let name: Vec<char> = tc.draw(gs::sampled_from(&names[..])).chars().collect();
    let from = tc.draw(gs::integers::<usize>().max_value(name.len() - 1));
    let to = tc.draw(gs::integers::<usize>().min_value(from).max_value(name.len()));
    match tc.draw(gs::integers::<u8>().max_value(3)) {
        0 => (0..tc.draw(gs::integers::<usize>().max_value(4)))
            .map(|_| tc.draw(gs::sampled_from(&["a", "e", "i", "r", "v", "D", "/"][..])))
            .collect(),
        1 => name[..to].iter().collect(),
        2 => name[from..to].iter().collect(),
        _ => name.iter().step_by(2).collect(),
    }
}

/// A field: perhaps words before, a sigil or none, a query, perhaps an
/// argument after, and a cursor anywhere in it, most often at the query's
/// end.
fn field(tc: &TestCase) -> (String, usize) {
    let before = tc.draw(gs::sampled_from(&["", "", "look at "][..]));
    let sigil = tc.draw(gs::sampled_from(&["/", "/", "@", "x", ""][..]));
    let query = query(tc);
    let after = tc.draw(gs::sampled_from(&["", " more"][..]));
    let text = format!("{before}{sigil}{query}{after}");
    let end = before.len() + sigil.len() + query.len();
    let cursor = if tc.draw(gs::booleans()) { end } else { tc.draw(gs::integers::<usize>().max_value(text.len())) };
    (text, cursor)
}

/// The oracle's match: the query's letters appear in order, ignoring case.
fn matches(query: &str, name: &str) -> bool {
    let mut name = name.chars().flat_map(char::to_lowercase);
    query.chars().flat_map(char::to_lowercase).all(|q| name.any(|c| c == q))
}

/// The oracle's ranking class: prefix, substring, or letters in order.
fn class(query: &str, name: &str) -> u8 {
    let (query, name) = (query.to_lowercase(), name.to_lowercase());
    if name.starts_with(&query) {
        0
    } else if name.contains(&query) {
        1
    } else {
        2
    }
}

fn names(c: &AgentCommand) -> impl Iterator<Item = &String> {
    std::iter::once(&c.name).chain(&c.aliases)
}

// C7–C9: a `/` popup opens only on a field starting with `/` while the
// cursor is in that first token, an `@` popup on an `@` token. Its rows are
// exactly the entries of its kind whose name or an alias matches the query
// (`@` inserts for `@`, the rest for `/`), ranked prefix before substring
// before scattered letters. No commands document means no `/` popup.
#[hegel::test(test_cases = 300)]
fn rows_are_exactly_the_matching_commands_ranked(tc: TestCase) {
    let commands = commands(&tc);
    let (text, cursor) = field(&tc);
    let mut completion = Completion::default();
    completion.update(commands.as_deref(), false, &text, cursor);
    let word_start = text[..cursor].rfind(' ').map_or(0, |at| at + 1);
    let word = &text[word_start..cursor];
    let expected = match word.chars().next() {
        Some('/') if word_start == 0 => Some(Popup::Command),
        Some('@') => Some(Popup::Mention),
        _ => None,
    };
    let query = word.get(1..).unwrap_or_default();
    let wanted: Vec<&AgentCommand> = commands
        .iter()
        .flatten()
        .filter(|c| c.insert.starts_with('@') == (expected == Some(Popup::Mention)))
        .filter(|c| names(c).any(|n| matches(query, n)))
        .collect();
    let expected = expected.filter(|_| !wanted.is_empty());
    assert_eq!(completion.popup(), expected, "{text:?} at {cursor}");
    let rows = completion.rows();
    assert_eq!(rows.len(), if expected.is_some() { wanted.len() } else { 0 });
    for row in rows {
        assert!(wanted.iter().any(|c| c.insert == row.insert && c.name == row.name), "{row:?}");
    }
    let classes: Vec<u8> = rows
        .iter()
        .map(|row| {
            let c = wanted.iter().find(|c| c.insert == row.insert).unwrap();
            names(c).filter(|n| matches(query, n)).map(|n| class(query, n)).min().unwrap()
        })
        .collect();
    assert!(classes.windows(2).all(|w| w[0] <= w[1]), "ranked: {classes:?}");
}

// Up and Down move the selection through every row, wrapping, and the at
// most eight rows shown always include it.
#[hegel::test(test_cases = 200)]
fn the_selection_moves_through_the_rows_and_stays_shown(tc: TestCase) {
    let mut many: Vec<AgentCommand> = (0..tc.draw(gs::integers::<usize>().min_value(1).max_value(20)))
        .map(|n| command(&format!("cmd{n}"), &format!("/cmd{n} "), None, &[], None))
        .collect();
    many.extend(pool());
    let mut completion = Completion::default();
    completion.update(Some(&many), false, "/", 1);
    let count = completion.rows().len();
    let mut expected = 0;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(40)) {
        let down = tc.draw(gs::booleans());
        assert_eq!(completion.key(if down { Key::Down } else { Key::Up }), (true, None));
        expected = if down { (expected + 1) % count } else { (expected + count - 1) % count };
        let selected = completion.selected().unwrap();
        assert_eq!(selected, expected);
        let (first, shown) = completion.visible();
        assert!(shown.len() <= VISIBLE_ROWS && first <= selected && selected < first + shown.len(), "{first} {selected}");
    }
}

// Tab or Enter replaces the whole token with the selected row's `insert`,
// leaving the rest of the field as typed; a command's hint then shows right
// after it until the user types, and the popup stays closed.
#[hegel::test(test_cases = 300)]
fn accepting_inserts_the_row_and_its_hint_lasts_until_typing(tc: TestCase) {
    let commands = Some(pool());
    let (text, cursor) = field(&tc);
    let mut completion = Completion::default();
    completion.update(commands.as_deref(), false, &text, cursor);
    if completion.popup().is_none() {
        assert_eq!(completion.key(Key::Accept), (false, None), "a closed popup leaves Enter to the field");
        return;
    }
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(3)) {
        completion.key(Key::Down);
    }
    let row = completion.rows()[completion.selected().unwrap()].clone();
    let (taken, edit) = completion.key(Key::Accept);
    let edit = edit.unwrap();
    assert!(taken);
    assert_eq!(edit.text, row.insert);
    let replaced = &text[edit.range.clone()];
    assert!(edit.range.contains(&(cursor - 1)) && !replaced.contains(' ') && replaced.starts_with(['/', '@']), "{replaced:?}");
    let after = edit.apply(&text);
    assert_eq!(format!("{}{}{}", &text[..edit.range.start], row.insert, &text[edit.range.end..]), after);
    let at = edit.range.start + edit.text.len();
    completion.update(commands.as_deref(), false, &after, at);
    let hint = pool().into_iter().find(|c| c.insert == row.insert).and_then(|c| c.hint);
    assert_eq!(completion.hint(), hint.as_deref().map(|h| (at, h)));
    assert_eq!(completion.popup(), None);
    completion.update(commands.as_deref(), false, &format!("{after}x"), at + 1);
    assert_eq!(completion.hint(), None, "typing ends the hint");
}

// A root with no search endpoint gets no file results and is never asked;
// a searchable `@` asks once per query, never for an empty one.
#[hegel::test(test_cases = 200)]
fn only_a_searchable_root_is_asked_and_once_per_query(tc: TestCase) {
    let searchable = tc.draw(gs::booleans());
    let mut completion = Completion::default();
    let mut asked = vec![];
    for _ in 0..tc.draw(gs::integers::<usize>().min_value(1).max_value(6)) {
        let (text, cursor) = field(&tc);
        if let Some(query) = completion.update(Some(&pool()), searchable, &text, cursor) {
            assert_eq!(completion.update(Some(&pool()), searchable, &text, cursor), None, "asked once");
            asked.push(query.clone());
            completion.answer(&query, vec![FileMatch { path: format!("src/{query}"), kind: FileKind::File }]);
        }
        let files = completion.rows().iter().filter(|r| r.kind != RowKind::Command).count();
        if !searchable {
            assert_eq!(files, 0);
        }
        if let Some(Token { popup: Popup::Mention, query, .. }) = token(&text, cursor) {
            assert_eq!(files, usize::from(searchable && !query.is_empty()), "{text:?}");
        }
    }
    assert!(searchable || asked.is_empty());
    assert!(asked.iter().all(|q| !q.is_empty()));
}

// C8 and D5: `@` lists its Agent Commands, then the latest query's files;
// an answer to an earlier query is dropped. Choosing a folder keeps the
// popup open on its contents, and a file inserts plain `@path`.
#[test]
fn an_at_token_lists_its_commands_then_the_latest_files() {
    let mut completion = Completion::default();
    let commands = pool();
    assert_eq!(completion.update(Some(&commands), true, "see @a", 6), Some("a".into()));
    assert_eq!(completion.update(Some(&commands), true, "see @ag", 7), Some("ag".into()));
    completion.answer("a", vec![FileMatch { path: "stale.rs".into(), kind: FileKind::File }]);
    assert!(completion.rows().iter().all(|r| r.kind == RowKind::Command), "a stale answer");
    completion.answer(
        "ag",
        vec![FileMatch { path: "agents".into(), kind: FileKind::Dir }, FileMatch { path: "page.rs".into(), kind: FileKind::File }],
    );
    let rows: Vec<(RowKind, &str)> = completion.rows().iter().map(|r| (r.kind, r.insert.as_str())).collect();
    assert_eq!(
        rows,
        [(RowKind::Command, "@agent-docs "), (RowKind::Command, "@agent-review "), (RowKind::Dir, "@agents"), (RowKind::File, "@page.rs")]
    );
    // The earlier query's answer, arriving late, changes nothing.
    completion.answer("a", vec![FileMatch { path: "stale.rs".into(), kind: FileKind::File }]);
    assert_eq!(completion.rows().len(), 4);
    completion.key(Key::Down);
    completion.key(Key::Down);
    let (_, edit) = completion.key(Key::Accept);
    let text = edit.unwrap().apply("see @ag");
    assert_eq!(text, "see @agents");
    assert_eq!(completion.update(Some(&commands), true, &text, text.len()), Some("agents".into()), "into the folder");
    completion.answer("agents", vec![FileMatch { path: "agents/main.rs".into(), kind: FileKind::File }]);
    // "agent-docs" matches too, scattered, and comes first.
    assert_eq!(completion.rows().last().unwrap().kind, RowKind::File);
    completion.key(Key::Up);
    let (_, edit) = completion.key(Key::Accept);
    let text = edit.unwrap().apply(&text);
    assert_eq!(text, "see @agents/main.rs");
    completion.update(Some(&commands), true, &text, text.len());
    assert_eq!(completion.popup(), None, "a file closes it");
}

// Esc closes the popup and keeps the draft: it stays closed while the cursor
// is in that token, and a new token, or coming back to it, opens one again.
#[test]
fn escape_closes_and_keeps_the_draft() {
    let mut completion = Completion::default();
    let commands = pool();
    completion.update(Some(&commands), false, "/re", 3);
    assert_eq!(completion.popup(), Some(Popup::Command));
    assert_eq!(completion.key(Key::Escape), (true, None));
    assert_eq!(completion.popup(), None);
    completion.update(Some(&commands), false, "/rev", 4);
    assert_eq!(completion.popup(), None, "the same token");
    assert_eq!(completion.key(Key::Escape), (false, None));
    completion.update(Some(&commands), false, "/rev @a", 7);
    assert_eq!(completion.popup(), Some(Popup::Mention));
    // Back in the first token after leaving it, its popup opens again.
    completion.update(Some(&commands), false, "/rev", 4);
    assert_eq!(completion.popup(), Some(Popup::Command));
    // Unknown text stays as typed: no popup is no edit.
    completion.update(Some(&commands), false, "/nothing-like-it", 16);
    assert_eq!((completion.popup(), completion.key(Key::Accept)), (None, (false, None)));
}
