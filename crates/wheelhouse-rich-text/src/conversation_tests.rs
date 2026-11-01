//! Conversations on the hand-written fixtures.
use std::time::Duration;

use hegel::TestCase;

use super::conversation::Tuning;
use super::generate::{self, Choose};
use super::property_tests::{follow, fresh, same, Draws, Model};
use super::tests::outline;
use super::*;

pub(crate) const CONVERSATION: &[u8] = include_bytes!("fixtures/conversation.jsonl");

/// Opened and backfilled to the start.
pub(crate) fn whole(jsonl: &[u8]) -> RichText {
    let mut rich = RichText::open_conversation(jsonl);
    while rich.backfill(Duration::from_secs(1)) > 0 {}
    rich
}

#[test]
fn the_fixture_ingests_every_kind_of_item() {
    insta::assert_snapshot!("conversation", outline(whole(CONVERSATION).doc()));
}

/// The tree as content and child lists, without versions: what two ways of
/// ingesting the same file must agree on.
fn canon(doc: &Tree) -> Vec<(NodeContent, Vec<Key>)> {
    doc.preorder()
        .into_iter()
        .map(|id| {
            let mut content = doc.content(id);
            content.version = 0;
            (content, doc.children(id).map(|c| doc.key(c)).collect())
        })
        .collect()
}

fn tuning(c: &mut impl Choose) -> Tuning {
    Tuning { tail_items: c.below(6), tail_bytes: [64, 1024, usize::MAX][c.below(3)], step: Some(1 + c.below(3000)) }
}

/// Cut points in `bytes`: anywhere, or only after newlines.
fn cuts(c: &mut impl Choose, bytes: &[u8], lines: bool) -> Vec<usize> {
    let mut cuts: Vec<usize> = (0..c.below(10)).map(|_| c.below(bytes.len() + 1)).collect();
    if lines {
        for cut in &mut cuts {
            *cut = bytes[..*cut].iter().rposition(|&b| b == b'\n').map_or(0, |i| i + 1);
        }
    }
    cuts.push(bytes.len());
    cuts.sort_unstable();
    cuts
}

#[hegel::test(test_cases = 300)]
fn any_split_gives_the_whole_files_tree(tc: TestCase) {
    let mut c = Draws(&tc);
    let items = 1 + c.below(10);
    let events = generate::conversation::events(&mut c, items);
    let bytes = generate::conversation::jsonl(&events);
    let mut reference = whole(&bytes);
    let expected = canon(reference.doc());
    let (variant, lines) = match c.below(3) {
        0 => (bytes, false),
        1 => (bytes, true),
        _ => (generate::conversation::jsonl(&generate::conversation::split_deltas(&mut c, &events)), true),
    };
    let cuts = cuts(&mut c, &variant, lines);
    let tuning = tuning(&mut c);
    let mut rich = RichText::tuned_conversation(&variant[..cuts[0]], RING_CAPACITY, tuning);
    for pair in cuts.windows(2) {
        if c.chance(2) {
            rich.backfill(Duration::ZERO);
        }
        rich.append(&variant[pair[0]..pair[1]]);
    }
    while rich.backfill(Duration::ZERO) > 0 {}
    rich.doc().assert_rows();
    assert_eq!(outline(rich.doc()), outline(reference.doc()));
    assert_eq!(canon(rich.doc()), expected);
    // Copy All reads the items, not the order they came in.
    assert_eq!(rich.copy_all(), reference.copy_all());
}

#[hegel::test(test_cases = 300)]
fn followed_conversations_match_fresh_snapshots(tc: TestCase) {
    let mut c = Draws(&tc);
    let items = 1 + c.below(8);
    let bytes = generate::conversation::jsonl(&generate::conversation::events(&mut c, items));
    let cuts = cuts(&mut c, &bytes, false);
    let capacity = [8, 64, RING_CAPACITY][c.below(3)];
    let tuning = tuning(&mut c);
    let mut rich = RichText::tuned_conversation(&bytes[..cuts[0]], capacity, tuning);
    let every = rich.subscribe();
    let rarely = rich.subscribe();
    let (mut each, mut rare) = (Model::default(), Model::default());
    follow(&mut rich, every, &mut each);
    let mut at = 1;
    loop {
        let before = rich.generation();
        let changed = if at < cuts.len() && c.chance(2) {
            at += 1;
            rich.append(&bytes[cuts[at - 2]..cuts[at - 1]])
        } else if rich.backfill_remaining() > 0 {
            let generation = rich.generation();
            rich.backfill(Duration::ZERO);
            rich.generation() != generation
        } else if at < cuts.len() {
            at += 1;
            rich.append(&bytes[cuts[at - 2]..cuts[at - 1]])
        } else {
            break;
        };
        assert_eq!(rich.generation(), before + u64::from(changed));
        rich.doc().assert_rows();
        follow(&mut rich, every, &mut each);
        same(&each, &fresh(&mut rich));
        if c.chance(3) {
            follow(&mut rich, rarely, &mut rare);
        }
    }
    follow(&mut rich, rarely, &mut rare);
    let snapshot = fresh(&mut rich);
    same(&each, &snapshot);
    same(&rare, &snapshot);
}

fn line(event: &str) -> Vec<u8> {
    format!("{event}\n").into_bytes()
}

fn start(id: &str, role: &str) -> Vec<u8> {
    line(&format!(r#"{{"type":"TextMessageStart","threadId":"t","seq":1,"messageId":"{id}","role":"{role}"}}"#))
}

fn content(id: &str, delta: &str) -> Vec<u8> {
    let delta = serde_json::to_string(delta).unwrap();
    line(&format!(r#"{{"type":"TextMessageContent","threadId":"t","seq":2,"messageId":"{id}","delta":{delta}}}"#))
}

fn end(id: &str) -> Vec<u8> {
    line(&format!(r#"{{"type":"TextMessageEnd","threadId":"t","seq":3,"messageId":"{id}"}}"#))
}

const HELLO: &str = r#"{"type":"Hello","contract":1}"#;

#[test]
fn a_streaming_message_upserts_only_its_tail_and_freezes_the_rest() {
    let mut rich = RichText::open_conversation(&[line(HELLO), start("m", "assistant")].concat());
    let id = rich.subscribe();
    rich.poll(id).unwrap();
    let markdown = generate::render(&generate::large(3, 200));
    let mut previous_frozen: Vec<(Key, u64)> = vec![];
    for piece in markdown.as_bytes().chunks(37) {
        let piece = std::str::from_utf8(piece).map(str::to_owned).unwrap_or_else(|_| String::from_utf8_lossy(piece).into_owned());
        rich.append(&content("m", &piece));
        let delta = rich.poll(id).unwrap();
        let turn = rich.doc().get(Key::named(&["t", "message", "m"])).unwrap();
        let blocks: Vec<NodeId> = rich.doc().children(turn).collect();
        // Every Block but the last is frozen: it keeps its version.
        for (key, version) in &previous_frozen {
            let node = rich.doc().get(*key).expect("a frozen Block stays");
            assert_eq!(rich.doc().version(node), *version, "a frozen Block was replaced");
        }
        if let Some(delta) = delta {
            for op in &delta.ops {
                if let Op::Upsert(c) = op {
                    assert!(!previous_frozen.iter().any(|(k, _)| *k == c.key), "a frozen Block was upserted");
                }
            }
        }
        previous_frozen = blocks[..blocks.len().saturating_sub(1)].iter().map(|&b| (rich.doc().key(b), rich.doc().version(b))).collect();
    }
    // Re-parsing the tail wrote nothing to the pages but frozen Blocks.
    let written = rich.doc().text.written();
    assert!(written < markdown.len(), "{written} bytes written for {} of markdown", markdown.len());
    rich.append(&end("m"));
    let streamed = canon(rich.doc());
    let whole = whole(&[line(HELLO), start("m", "assistant"), content("m", &markdown), end("m")].concat());
    assert_eq!(streamed, canon(whole.doc()));
}

#[test]
fn an_open_fence_appends_its_lines_as_the_parser_would() {
    let mut rich = RichText::open_conversation(&[line(HELLO), start("m", "assistant"), content("m", "Run:\n\n  ~~~sh\n")].concat());
    let turn = Key::named(&["t", "message", "m"]);
    let mut so_far = "Run:\n\n  ~~~sh\n".to_owned();
    for piece in ["  echo ka", "\n   lo\n", "x ``` y\n", "~~", "~\n", "after"] {
        rich.append(&content("m", piece));
        so_far.push_str(piece);
        let parsed = RichText::open(&so_far);
        let doc = rich.doc();
        let blocks: Vec<String> =
            doc.children(doc.get(turn).unwrap()).map(|b| format!("{:?} {:?} {:?}", doc.kind(b), doc.detail(b), doc.text(b))).collect();
        let pd = parsed.doc();
        let expected: Vec<String> =
            pd.children(pd.root().unwrap()).map(|b| format!("{:?} {:?} {:?}", pd.kind(b), pd.detail(b), pd.text(b))).collect();
        assert_eq!(blocks, expected, "after {so_far:?}");
    }
}

#[test]
fn the_tail_is_repaired_and_nothing_is_held_back() {
    let rich = RichText::open_conversation(&[line(HELLO), start("m", "assistant"), content("m", "Some **bold and `co")].concat());
    let turn = rich.doc().get(Key::named(&["t", "message", "m"])).unwrap();
    let paragraph = rich.doc().children(turn).next().unwrap();
    assert_eq!(rich.doc().text(paragraph), "Some bold and co");
    let attrs: Vec<Attr> = rich.doc().spans(paragraph).iter().map(|s| s.attr).collect();
    assert_eq!(attrs, [Attr::Strong, Attr::Code]);
}

#[test]
fn repair_closes_what_is_open_and_leaves_bare_markers() {
    use super::tail::repair;
    assert_eq!(repair("a **b"), "**");
    assert_eq!(repair("a *b **c"), "***");
    assert_eq!(repair("a `b"), "`");
    assert_eq!(repair("a ~~b"), "~~");
    assert_eq!(repair("[link](https://exa"), ")");
    assert_eq!(repair("**done** and _it_"), "");
    // A marker with nothing after it yet, a bullet, a product and snake_case.
    assert_eq!(repair("**"), "");
    assert_eq!(repair("* item"), "");
    assert_eq!(repair("2 * 3"), "");
    assert_eq!(repair("snake_case"), "");
    // Only the last paragraph.
    assert_eq!(repair("a **b\n\nc"), "");
    assert_eq!(repair("`**`"), "");
}

#[test]
fn backfill_inserts_at_the_front_and_the_view_stays_still() {
    let bytes = generate::conversation::jsonl(&generate::conversation::events(&mut generate::Seeded(9), 120));
    let tuning = Tuning { tail_items: 10, tail_bytes: usize::MAX, step: Some(2_000) };
    let mut rich = RichText::tuned_conversation(&bytes, RING_CAPACITY, tuning);
    let tail_len = rich.doc().children(rich.doc().root().unwrap()).count();
    assert!(rich.backfill_remaining() > 0);
    let id = rich.subscribe();
    {
        let (doc, view) = rich.view_mut(id).unwrap();
        view.set_metrics(doc, Metrics { width: 400.0, line_height: 10.0, chars_per_line: 40.0 });
        view.set_viewport(200.0);
        view.scroll_to_end();
        view.scroll_by(doc, -150.0);
    }
    let window = |rich: &RichText| {
        let window = rich.view(id).unwrap().window(rich.doc());
        window.entries.iter().map(|e| (e.key, e.part, e.top - window.top)).collect::<Vec<_>>()
    };
    let before = window(&rich);
    let mut steps = 0;
    while rich.backfill(Duration::ZERO) > 0 {
        steps += 1;
        assert_eq!(window(&rich), before, "backfill step {steps} moved the view");
    }
    assert!(steps > 3);
    let all = rich.doc().children(rich.doc().root().unwrap()).count();
    assert!(all > tail_len, "{all} items after backfill, {tail_len} in the tail");
    assert_eq!(canon(rich.doc()), canon(whole(&bytes).doc()));
}

#[test]
fn keys_come_from_ids_and_last_across_reloads() {
    let a = whole(CONVERSATION);
    let b = whole(CONVERSATION);
    let keys = |rich: &RichText| rich.doc().preorder().into_iter().map(|id| rich.doc().key(id)).collect::<Vec<_>>();
    assert_eq!(keys(&a), keys(&b));
    let turn = a.doc().get(Key::named(&["t1", "message", "m1"])).unwrap();
    assert_eq!(a.doc().kind(turn), Kind::Turn);
    let tool = a.doc().get(Key::named(&["t1", "tool", "c1"])).unwrap();
    assert!(matches!(a.doc().detail(tool), Detail::Section { status: ToolStatus::Failed, tool: ToolKind::Shell, .. }));
}

#[test]
fn another_major_version_shows_a_notice_and_nothing_else() {
    let rich = whole(&[line(r#"{"type":"Hello","contract":2}"#), start("m", "user"), content("m", "hi"), end("m")].concat());
    insta::assert_snapshot!(outline(rich.doc()), @r#"
    Root
      Notice "Conversation contract 2 isn't supported (this reads 1)"
    "#);
}

#[test]
fn a_tool_call_is_pending_until_its_result() {
    let call =
        line(r#"{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"Bash","title":"ls","kind":"shell"}"#);
    let mut rich = RichText::open_conversation(&[line(HELLO), call].concat());
    let status = |rich: &RichText| match rich.doc().detail(rich.doc().get(Key::named(&["t", "tool", "c"])).unwrap()) {
        Detail::Section { status, .. } => *status,
        _ => unreachable!(),
    };
    assert_eq!(status(&rich), ToolStatus::Pending);
    rich.append(&line(r#"{"type":"ToolCallResult","threadId":"t","seq":2,"toolCallId":"c","status":"awaiting_approval","content":""}"#));
    assert_eq!(status(&rich), ToolStatus::AwaitingApproval);
}

/// A node's subtree below it, as [`canon`] has it.
fn canon_under(doc: &Tree, id: NodeId, out: &mut Vec<(NodeContent, Vec<Key>)>) {
    for child in doc.children(id) {
        let mut content = doc.content(child);
        content.version = 0;
        out.push((content, doc.children(child).map(|c| doc.key(c)).collect()));
        canon_under(doc, child, out);
    }
}

// A subagent's thread opened as its own View shows what its Section holds
// in the whole conversation, at its root, and nothing else: however the
// file is cut into a tail and backfill. Generated conversations without a
// subagent have nothing to check.
#[hegel::test(test_cases = 300)]
fn a_threads_view_is_what_its_section_holds(tc: TestCase) {
    let mut c = Draws(&tc);
    let items = 4 + c.below(12);
    let events = generate::conversation::events(&mut c, items);
    let threads: Vec<String> = events
        .iter()
        .filter_map(|e| match e {
            conversation_events::Event::RunStarted(r) if r.parent.is_some() => Some(r.thread_id.clone()),
            _ => None,
        })
        .collect();
    if threads.is_empty() {
        return;
    }
    let thread = &threads[c.below(threads.len())];
    let bytes = generate::conversation::jsonl(&events);
    let reference = whole(&bytes);
    let doc = reference.doc();
    let section = doc.get(Key::named(&[thread, "thread"])).expect("a subagent's Section");
    let mut expected = vec![];
    canon_under(doc, section, &mut expected);
    let mut rich = RichText::tuned_thread(&bytes, RING_CAPACITY, tuning(&mut c), Some(thread));
    while rich.backfill(Duration::ZERO) > 0 {}
    rich.doc().assert_rows();
    let mut shown = vec![];
    canon_under(rich.doc(), rich.doc().root().unwrap(), &mut shown);
    assert_eq!(shown, expected);
}

#[test]
fn copy_actions_copy_a_message_a_tool_output_and_a_command() {
    let rich = whole(CONVERSATION);
    let copy = |thread: &str, kind: &str, id: &str, action| rich.copy_action(Key::named(&[thread, kind, id]), action);
    // A message's markdown, as plain text.
    assert_eq!(
        copy("t1", "message", "m1", CopyAction::Message).as_deref(),
        Some("Why does `ka lo` fail? See [the log][log].\n\n[log]: https://example.com/log")
    );
    // Output as shown: the carriage return's rewrite, without escapes.
    assert_eq!(copy("t1", "tool", "c1", CopyAction::ToolOutput).as_deref(), Some("error: ka lo\n100%\nsee the docs"));
    assert_eq!(copy("t1", "tool", "c1", CopyAction::Command).as_deref(), Some("cargo test"));
    // A diff's patch and a markdown result as they came.
    assert_eq!(copy("t1", "tool", "c2", CopyAction::ToolOutput).as_deref(), Some("--- a/src/lib.rs\n+++ b/src/lib.rs\n-ka\n+lo"));
    assert_eq!(copy("t1", "tool", "c3", CopyAction::ToolOutput).as_deref(), Some("# Notes\n\n- one\n- two"));
    // What doesn't apply copies nothing: an image's output, a call with no
    // command, a pending call's output, a message's output, reasoning.
    assert_eq!(copy("t1", "tool", "c4", CopyAction::ToolOutput), None);
    assert_eq!(copy("t1", "tool", "c2", CopyAction::Command), None);
    assert_eq!(copy("t1", "tool", "c5", CopyAction::ToolOutput), None);
    assert_eq!(copy("t1", "message", "m1", CopyAction::ToolOutput), None);
    assert_eq!(copy("t1", "reasoning", "rs1", CopyAction::Message), None);
    // What can be copied is exactly what copies something.
    for id in rich.doc().preorder() {
        for action in CopyAction::ALL {
            let key = rich.doc().key(id);
            assert_eq!(rich.can_copy(key, action), rich.copy_action(key, action).is_some(), "{key:?} {action:?}");
        }
    }
    // Markdown has no conversation to copy from.
    assert_eq!(RichText::open("# hi").copy_action(Key::ROOT, CopyAction::Message), None);
}

#[test]
fn copy_all_is_the_transcript_without_reasoning() {
    let tuning = Tuning { tail_items: 2, tail_bytes: usize::MAX, step: Some(64) };
    let mut rich = RichText::tuned_conversation(CONVERSATION, RING_CAPACITY, tuning);
    // Copied before backfill reaches the start, it still has all of it.
    assert!(rich.backfill_remaining() > 0);
    insta::assert_snapshot!("transcript", rich.copy_all().unwrap());
    assert_eq!(RichText::open("# hi").copy_all(), None);
}
