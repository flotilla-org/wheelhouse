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
    let tuning = Tuning { tail_items: 20, tail_bytes: usize::MAX, step: Some(2_000) };
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

// Pasted as markdown, a notice stays one quote, every line of it, and a
// Divider isn't read as a rule or a heading.
#[test]
fn copy_all_quotes_a_notice_whole_and_keeps_a_divider_out_of_headings() {
    let error = line(r#"{"type":"RunError","threadId":"t","seq":1,"message":"first line\nsecond line"}"#);
    let compaction = line(r#"{"type":"Custom","threadId":"t","seq":2,"name":"compaction","value":{}}"#);
    let all = RichText::open_conversation(&[line(HELLO), error, compaction].concat()).copy_all().unwrap();
    assert_eq!(all, "> first line\n> second line\n\n*— compaction —*");
}

/// What a Tool Run must be, wherever it is: two or more calls, its summary
/// counting them, and maximal, so no call or run sits beside another.
fn assert_tool_runs(doc: &Tree) {
    let call = |id: NodeId| matches!(doc.detail(id), Detail::Section { section: SectionKind::Tool, .. });
    for parent in doc.preorder() {
        let children: Vec<NodeId> = doc.children(parent).collect();
        if doc.kind(parent) != Kind::ToolRun {
            for pair in children.windows(2) {
                let grouped = |id| call(id) || doc.kind(id) == Kind::ToolRun;
                assert!(!(grouped(pair[0]) && grouped(pair[1])), "{:?} and {:?} are one run", doc.key(pair[0]), doc.key(pair[1]));
            }
            continue;
        }
        assert!(children.len() >= 2 && children.iter().all(|&c| call(c)), "{:?} holds only calls, two or more", doc.key(parent));
        let calls: Vec<(ToolKind, ToolStatus)> = children
            .iter()
            .map(|&c| match doc.detail(c) {
                Detail::Section { tool, status, .. } => (*tool, *status),
                _ => unreachable!(),
            })
            .collect();
        let count = |f: &dyn Fn(ToolKind, ToolStatus) -> bool| calls.iter().filter(|&&(k, s)| f(k, s)).count() as u32;
        let Detail::ToolRun(summary) = doc.detail(parent) else { panic!("a Tool Run's detail") };
        assert_eq!(
            [summary.commands, summary.edits, summary.reads, summary.searches, summary.others, summary.failed],
            [
                count(&|k, _| k == ToolKind::Shell),
                count(&|k, _| k == ToolKind::Edit),
                count(&|k, _| k == ToolKind::Read),
                count(&|k, _| k == ToolKind::Search),
                count(&|k, _| matches!(k, ToolKind::Agent | ToolKind::Web | ToolKind::Mcp | ToolKind::Other)),
                count(&|_, s| matches!(s, ToolStatus::Failed | ToolStatus::Interrupted)),
            ]
        );
        assert_eq!(summary.failed > 0, (summary.failed_at as usize) < summary.text.len());
    }
}

#[hegel::test(test_cases = 300)]
fn tool_runs_are_the_maximal_runs_of_calls_streamed_or_whole(tc: TestCase) {
    let mut c = Draws(&tc);
    let items = 1 + c.below(12);
    let events = generate::conversation::events(&mut c, items);
    let bytes = generate::conversation::jsonl(&events);
    let reference = whole(&bytes);
    assert_tool_runs(reference.doc());
    // Every call is still there, in a run or not.
    for event in &events {
        let calls: Vec<(&str, &str)> = match event {
            conversation_events::Event::ToolCallStart(e) => vec![(&e.thread_id, &e.tool_call_id)],
            conversation_events::Event::MessagesSnapshot(e) => {
                e.messages.iter().flat_map(|m| &m.tool_calls).map(|call| (e.thread_id.as_str(), call.id.as_str())).collect()
            }
            _ => vec![],
        };
        for (thread, id) in calls {
            assert!(reference.doc().get(Key::named(&[thread, "tool", id])).is_some(), "{thread} {id} is in the tree");
        }
    }
    // Event by event, as a live conversation streams.
    let mut rich = RichText::open_conversation(&bytes[..0]);
    let mut at = 0;
    for event in &events {
        let line = generate::conversation::jsonl(std::slice::from_ref(event));
        rich.append(&bytes[at..at + line.len()]);
        at += line.len();
        rich.doc().assert_rows();
        assert_tool_runs(rich.doc());
    }
    assert_eq!(canon(rich.doc()), canon(reference.doc()));
}

#[test]
fn a_tool_run_counts_its_calls_by_kind_then_failures() {
    use ToolKind::*;
    use ToolStatus::*;
    let summary = |calls: &[(ToolKind, ToolStatus)]| RunSummary::new(calls.iter().copied()).text;
    let calls = [(Shell, Ok), (Read, Ok), (Edit, Failed), (Shell, Interrupted), (Edit, Ok), (Shell, Pending)];
    assert_eq!(summary(&calls), "Ran 3 commands · edited 2 files · read 1 file · 2 failed");
    let s = RunSummary::new(calls);
    assert_eq!(&s.text[s.failed_at as usize..], "2 failed");
    assert_eq!(summary(&[(Search, Ok), (Agent, Ok), (Web, AwaitingApproval), (Mcp, Ok)]), "Ran 1 search · called 3 other tools");
    assert_eq!(summary(&[(Read, Ok), (Read, Ok)]), "Read 2 files");
    let s = RunSummary::new([(Read, Ok), (Read, Ok)]);
    assert_eq!(s.failed_at as usize, s.text.len());
}

#[test]
fn a_calls_duration_is_from_its_start_to_its_result() {
    let call = line(
        r#"{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"Bash","kind":"shell","timestamp":1800000000000}"#,
    );
    let timed = line(r#"{"type":"ToolCallResult","threadId":"t","seq":2,"toolCallId":"c","content":"","timestamp":1800000002500}"#);
    let untimed = line(r#"{"type":"ToolCallResult","threadId":"t","seq":2,"toolCallId":"c","content":""}"#);
    let duration = |events: &[&[u8]]| {
        let rich = RichText::open_conversation(&[&line(HELLO)[..]].iter().chain(events).copied().collect::<Vec<_>>().concat());
        match rich.doc().detail(rich.doc().get(Key::named(&["t", "tool", "c"])).unwrap()) {
            Detail::Section { duration_ms, .. } => *duration_ms,
            _ => unreachable!(),
        }
    };
    assert_eq!(duration(&[&call, &timed]), Some(2500));
    // A result stamped before its start, by clock skew: no duration.
    let early = line(r#"{"type":"ToolCallResult","threadId":"t","seq":2,"toolCallId":"c","content":"","timestamp":1799999999000}"#);
    assert_eq!(duration(&[&call, &early]), None);
    assert_eq!(duration(&[&call, &untimed]), None);
    assert_eq!(duration(&[&call]), None);
}

#[test]
fn a_calls_title_and_input_follow_its_kind() {
    // The title a header shows, and the call's input Block, if it has one.
    let shown = |name: &str, title: &str, kind: &str, args: &str| {
        let start = format!(
            r#"{{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"{name}","title":"{title}","kind":"{kind}"}}"#
        );
        let args = serde_json::to_string(args).unwrap();
        let args = format!(r#"{{"type":"ToolCallArgs","threadId":"t","seq":2,"toolCallId":"c","delta":{args}}}"#);
        let rich = RichText::open_conversation(&[line(HELLO), line(&start), line(&args)].concat());
        let doc = rich.doc();
        let call = doc.get(Key::named(&["t", "tool", "c"])).unwrap();
        let Detail::Section { title, .. } = doc.detail(call) else { unreachable!() };
        let input = doc.children(call).next().filter(|&c| doc.kind(c) == Kind::Input).map(|c| doc.text(c).to_owned());
        (title.clone(), input)
    };
    let s = |text: &str| Some(text.to_owned());
    // A described command keeps its description; an undescribed one shows
    // its first line. Its input is the whole command.
    assert_eq!(shown("Bash", "Run the tests", "shell", r#"{"command":"cargo test\necho done"}"#).0, "Run the tests");
    assert_eq!(shown("Bash", "", "shell", r#"{"command":"cargo test\necho done"}"#), ("cargo test".into(), s("cargo test\necho done")));
    // An edit or a read shows its path, a search its pattern.
    assert_eq!(shown("Edit", "Fix it", "edit", r#"{"file_path":"src/lib.rs","old_string":"a"}"#).0, "src/lib.rs");
    assert_eq!(shown("Read", "", "read", r#"{"file_path":"notes.md"}"#), ("notes.md".into(), s(r#"{"file_path":"notes.md"}"#)));
    assert_eq!(shown("Grep", "", "search", r#"{"pattern":"ka.*lo","path":"src"}"#).0, "ka.*lo");
    // Anything else without a title: its name and its first argument.
    assert_eq!(shown("Fetch", "", "web", r#"{"url":"https://example.com"}"#).0, "Fetch https://example.com");
    // Input is compact JSON, long strings folded; none for no arguments.
    let long = "x".repeat(100);
    let (_, input) = shown("Write", "", "edit", &format!(r#"{{"file_path":"a.rs","content":"{long}"}}"#));
    assert_eq!(input, s(&format!(r#"{{"content":"{}…","file_path":"a.rs"}}"#, "x".repeat(60))));
    assert_eq!(shown("Todo", "", "other", "{}"), (String::new(), None));
    assert_eq!(shown("Todo", "", "other", ""), (String::new(), None));
}

// A call's input is restaged with each change to the call, but the Tree
// keeps a node whose key and content match, so only a changed input reaches
// a View: a result leaves it alone, and more arguments update it in place.
#[test]
fn a_calls_input_changes_only_when_its_arguments_do() {
    let start = line(r#"{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"Bash","kind":"shell"}"#);
    let args = |seq: u32, delta: &str| {
        let delta = serde_json::to_string(delta).unwrap();
        line(&format!(r#"{{"type":"ToolCallArgs","threadId":"t","seq":{seq},"toolCallId":"c","delta":{delta}}}"#))
    };
    let mut rich = RichText::open_conversation(&[line(HELLO), start, args(2, r#"{"command":"ls"#)].concat());
    let id = rich.subscribe();
    rich.poll(id).unwrap();
    let call = Key::named(&["t", "tool", "c"]);
    let first = rich.doc().children(rich.doc().get(call).unwrap()).next().unwrap();
    // Arguments still streaming in aren't JSON yet: shown as they are.
    assert_eq!((rich.doc().kind(first), rich.doc().text(first)), (Kind::Input, r#"{"command":"ls"#));
    let input = rich.doc().key(first);
    let touches = |delta: Option<Delta>| {
        delta.into_iter().flat_map(|d| d.ops).any(|op| match op {
            Op::Upsert(c) => c.key == input,
            Op::Remove(key) => key == input,
            Op::Children { parent, ops } => parent == input || ops.iter().any(|o| matches!(o, ListOp::Insert { key, .. } if *key == input)),
        })
    };
    rich.append(&args(3, r#" -l"}"#));
    let delta = rich.poll(id).unwrap();
    assert!(delta.as_ref().is_some_and(|d| d.ops.iter().any(|op| matches!(op, Op::Upsert(c) if c.key == input && c.text == "ls -l"))));
    assert!(!delta.unwrap().ops.iter().any(|op| matches!(op, Op::Remove(key) if *key == input)), "updated in place");
    rich.append(&line(r#"{"type":"ToolCallResult","threadId":"t","seq":4,"toolCallId":"c","content":"a\nb\n"}"#));
    assert!(!touches(rich.poll(id).unwrap()), "a result leaves the input alone");
}

#[test]
fn a_search_matchs_location_is_its_path_and_line() {
    use super::conversation::location;
    fn at(line: &str) -> &str {
        &line[..location(line)]
    }
    assert_eq!(at("src/a.rs:3: flaky"), "src/a.rs:3");
    assert_eq!(at("a:1"), "a:1");
    assert_eq!(at("a:1:2"), "a:1");
    assert_eq!(at("a:1-context"), "a:1");
    assert_eq!(at(r"C:\x:3: y"), r"C:\x:3");
    // A path alone, a line that isn't one, and a colon first.
    assert_eq!(at("src/a.rs"), "");
    assert_eq!(at("note: 12 apples"), "");
    assert_eq!(at("foo: 12-bar"), "");
    assert_eq!(at(":3"), "");
}

#[test]
fn a_reads_language_comes_from_a_path_only() {
    let language = |title: &str, args: &str| {
        let start = format!(
            r#"{{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"Read","title":"{title}","kind":"read"}}"#
        );
        let args = serde_json::to_string(args).unwrap();
        let args = format!(r#"{{"type":"ToolCallArgs","threadId":"t","seq":2,"toolCallId":"c","delta":{args}}}"#);
        let result = line(r#"{"type":"ToolCallResult","threadId":"t","seq":3,"toolCallId":"c","content":"x\n"}"#);
        let rich = RichText::open_conversation(&[line(HELLO), line(&start), line(&args), result].concat());
        let call = rich.doc().get(Key::named(&["t", "tool", "c"])).unwrap();
        let code = rich.doc().children(call).find(|&c| rich.doc().kind(c) == Kind::Code).unwrap();
        match rich.doc().detail(code) {
            Detail::Code { language } => language.clone(),
            _ => unreachable!(),
        }
    };
    assert_eq!(language("Read config", r#"{"file_path":"src/lib.rs"}"#), "rs");
    assert_eq!(language("src/tides.md", ""), "md");
    assert_eq!(language("Read v1.2", ""), "");
    assert_eq!(language("notes", r#"{"file_path":"v1.2"}"#), "");
}

// A later snapshot can leave a call with no arguments: its input Block goes,
// and its output stays where it was.
#[test]
fn a_call_whose_arguments_go_loses_its_input() {
    let start = line(r#"{"type":"ToolCallStart","threadId":"t","seq":1,"toolCallId":"c","toolCallName":"Bash","kind":"shell"}"#);
    let args = line(r#"{"type":"ToolCallArgs","threadId":"t","seq":2,"toolCallId":"c","delta":"{\"command\":\"ls\"}"}"#);
    let result = line(r#"{"type":"ToolCallResult","threadId":"t","seq":3,"toolCallId":"c","content":"a\n"}"#);
    let snapshot = line(
        r#"{"type":"MessagesSnapshot","threadId":"t","seq":4,"messages":[{"id":"m","role":"assistant","toolCalls":[{"id":"c","type":"function","function":{"name":"Bash","arguments":""},"kind":"shell"}]}]}"#,
    );
    let mut rich = RichText::open_conversation(&[line(HELLO), start, args, result].concat());
    let kinds = |rich: &RichText| {
        let call = rich.doc().get(Key::named(&["t", "tool", "c"])).unwrap();
        rich.doc().children(call).map(|c| rich.doc().kind(c)).collect::<Vec<_>>()
    };
    assert_eq!(kinds(&rich), [Kind::Input, Kind::Log]);
    rich.append(&snapshot);
    rich.doc().assert_rows();
    assert_eq!(kinds(&rich), [Kind::Log]);
}

#[test]
fn arguments_close_only_when_their_value_does_outside_strings() {
    use super::conversation::Nesting;
    let closed = |pieces: &[&str]| {
        let mut nesting = Nesting::default();
        pieces.iter().for_each(|p| nesting.feed(p));
        nesting.closed()
    };
    let cases: &[(&[&str], bool)] = &[
        (&[r#"{"a":1}"#], true),
        (&[r#"{"a":"#, "1", "}"], true),
        (&[r#"{"a":[{"b":2}"#], false),
        (&[r#"[1,[2]]"#], true),
        // Braces and brackets inside a string don't count.
        (&[r#"{"a":"}]"#], false),
        (&[r#"{"a":"}]"}"#], true),
        // An escaped quote doesn't end a string; an escaped backslash
        // before one does, even split across deltas.
        (&[r#"{"a":"\"}"#], false),
        (&[r#"{"a":"\\"}"#], true),
        (&[r#"{"a":"\"#, r#"\"}"#], true),
        (&[r#"{"a":"\"#, r#""}"#], false),
        // Nothing opened yet, or a bare scalar.
        (&[""], false),
        (&["42"], false),
    ];
    for (pieces, want) in cases {
        assert_eq!(closed(pieces), *want, "{pieces:?}");
    }
}

/// What "↓ N new" counts, written apart from the View's own scan.
fn items(doc: &Tree) -> Vec<Key> {
    let root = doc.root();
    doc.entries()
        .iter()
        .filter(|e| match e.part {
            Part::Header => {
                matches!(doc.kind(e.node), Kind::Turn | Kind::Section) || (doc.parent(e.node) == root && doc.kind(e.node) != Kind::ToolRun)
            }
            Part::Block => doc.parent(e.node) == root,
            Part::Footer => false,
        })
        .map(|e| doc.key(e.node))
        .collect()
}

/// Follow-tail as a conversation's file grows (#617, #541 decision 6).
#[hegel::test(test_cases = 300)]
fn a_view_follows_a_growing_conversation(tc: TestCase) {
    let mut c = Draws(&tc);
    let count = 1 + c.below(12);
    let events = generate::conversation::events(&mut c, count);
    let bytes = generate::conversation::jsonl(&generate::conversation::split_deltas(&mut c, &events));
    let cuts = cuts(&mut c, &bytes, false);
    let mut rich = RichText::open_conversation(&bytes[..cuts[0]]);
    while rich.backfill(Duration::ZERO) > 0 {}
    let id = rich.subscribe();
    {
        let (doc, view) = rich.view_mut(id).unwrap();
        view.set_metrics(doc, Metrics { width: 200.0, line_height: 10.0, chars_per_line: 20.0 });
        view.set_viewport(20.0 + c.below(200) as f64);
        view.scroll_to_end();
    }
    // The items there were when follow broke, and the last of them.
    let mut broke: Option<(Vec<Key>, Option<Key>)> = None;
    for pair in cuts.windows(2) {
        let (doc, view) = rich.view_mut(id).unwrap();
        match c.below(5) {
            0 => view.scroll_by(doc, -(1.0 + c.below(300) as f64)),
            1 => view.scroll_by(doc, 1.0 + c.below(300) as f64),
            2 => view.scroll_to_end(),
            3 => {
                // Expanding a Section in view.
                let window = view.window(doc);
                let shown = |e: &&WindowEntry| e.part == Part::Header && e.top >= window.top && e.top < window.top + window.viewport;
                let sections: Vec<Key> = window
                    .entries
                    .iter()
                    .filter(shown)
                    .map(|e| e.key)
                    .filter(|&k| doc.kind(doc.get(k).unwrap()) == Kind::Section)
                    .collect();
                if !sections.is_empty() {
                    let key = sections[c.below(sections.len())];
                    let (was_following, last) = (view.at_end(), items(doc).last().copied());
                    view.set_collapsed(doc, key, true).unwrap();
                    let window = view.window(doc);
                    let header = window.entries.iter().find(|e| e.key == key && e.part == Part::Header).unwrap();
                    if header.top >= window.top && header.top < window.top + window.viewport {
                        view.set_collapsed(doc, key, false).unwrap();
                        assert!(!view.at_end(), "expanding a Section in view turns follow off");
                        assert_eq!(view.anchor().map(|a| (a.key, a.part)), Some((key, Part::Header)), "and anchors on it");
                        if was_following {
                            broke = Some((items(doc), last));
                        }
                    }
                }
            }
            _ => {}
        }
        if view.at_end() {
            broke = None;
        } else if broke.is_none() {
            broke = Some((items(doc), items(doc).last().copied()));
        }
        let before = view.window(doc);
        let anchor = view.anchor();
        let anchor_top =
            anchor.and_then(|a| before.entries.iter().find(|e| e.key == a.key && e.part == a.part)).map(|e| e.top - before.top);
        rich.append(&bytes[pair[0]..pair[1]]);
        let (doc, view) = rich.view_mut(id).unwrap();
        let window = view.window(doc);
        if before.at_end {
            assert!(window.at_end, "growth never breaks follow");
            assert_eq!(window.top, (window.total - window.viewport).max(0.0), "a following View stays pinned to the end");
            assert_eq!(window.new, 0);
            continue;
        }
        assert!(!window.at_end, "growth never re-arms follow");
        assert_eq!(view.anchor(), anchor, "growth never moves a reader's anchor");
        if let (Some(a), Some(was)) = (anchor, anchor_top) {
            let now = window.entries.iter().find(|e| e.key == a.key && e.part == a.part).map(|e| e.top - window.top);
            if window.top > 0.0 && window.top < (window.total - window.viewport).max(0.0) {
                assert_eq!(now, Some(was), "nor where the anchor shows");
            }
        }
        let (seen, last) = broke.as_ref().unwrap();
        let now = items(doc);
        let after = last.map_or(0, |k| now.iter().position(|&n| n == k).unwrap() + 1);
        assert!(now[..after].iter().all(|k| seen.contains(k)), "a new item came before the last seen");
        assert_eq!(window.new as usize, now.len() - after, "items after the last one seen are new");
    }
    let (doc, view) = rich.view_mut(id).unwrap();
    view.scroll_to_end();
    let window = view.window(doc);
    assert!(window.at_end && window.new == 0, "jumping to the end re-arms follow");
}
