use std::fmt::Debug;
use std::fs;

use hegel::generators as gs;
use hegel::TestCase;
use serde::de::DeserializeOwned;
use serde_json::{json, Value};

use super::*;

const WIRE: &[u8] = include_bytes!("../fixtures/wire.jsonl");

fn events(bytes: &[u8]) -> Vec<Event> {
    read(bytes).collect::<Result<_, _>>().unwrap()
}

#[test]
fn the_wire_example_reads_with_our_additions() {
    let events = events(WIRE);
    assert_eq!(events.len(), 25);
    assert_eq!(events[0], Event::Hello(Hello { contract: CONTRACT, ..Hello::default() }));
    let Event::ToolCallStart(start) = &events[8] else { panic!("{:?}", events[8]) };
    assert_eq!((start.title.as_deref(), start.kind), (Some("cargo test -p wheelhouse-text"), Some(ToolKind::Shell)));
    let Event::ToolCallOutput(output) = &events[11] else { panic!("{:?}", events[11]) };
    assert_eq!((output.delta.as_str(), output.media_type.as_deref()), ("running 3 tests\n", Some(ANSI)));
    let Event::ToolCallResult(result) = &events[12] else { panic!("{:?}", events[12]) };
    assert_eq!(result.status, Some(ToolStatus::Failed));
    assert_eq!(result.media_type.as_deref(), Some(ANSI));
    assert_eq!(result.content, "\x1b[31merror\x1b[0m: 1 test failed\n");
    assert_eq!(result.truncated, None);
    assert_eq!((events[12].thread_id(), events[12].seq()), ("t1", 12));
    let Event::ToolCallEnd(end) = &events[14] else { panic!() };
    assert_eq!((end.title.as_deref(), end.kind), (Some("parser error recovery"), Some(ToolKind::Web)));
    let Event::ToolCallResult(search) = &events[15] else { panic!() };
    assert_eq!(search.media_type.as_deref(), Some(SEARCH_RESULTS));
    let results: Vec<SearchResult> = serde_json::from_value(search.structured.clone().unwrap()).unwrap();
    assert_eq!((results[0].domain.as_str(), results[0].thumbnail.as_deref()), ("example.com", None));
    let Event::RunFinished(RunFinished { outcome: Some(Outcome::Interrupt { interrupts }), .. }) = &events[19] else { panic!() };
    assert_eq!((interrupts[0].id.as_str(), interrupts[0].tool_call_id.as_deref()), ("i1", Some("c3")));
    let Event::RunStarted(RunStarted { input: Some(input), .. }) = &events[20] else { panic!() };
    assert_eq!(input.resume, [ResumeEntry { interrupt_id: "i1".into(), status: ResumeStatus::Cancelled, payload: None }]);
    let Event::TextMessageStart(peer) = &events[21] else { panic!() };
    let origin = peer.origin.as_ref().unwrap();
    assert_eq!((&origin.kind, &origin.fields["name"]), (&OriginKind::Peer, &json!("reviewer")));
    let Event::RunFinished(finished) = &events[24] else { panic!() };
    assert_eq!(finished.outcome, Some(Outcome::Cancelled));
}

#[test]
fn outcomes_read_as_ag_ui_writes_them() {
    let outcome = |line: &str| match parse_line(line.as_bytes()) {
        Ok(Some(Event::RunFinished(e))) => e.outcome,
        other => panic!("{other:?}"),
    };
    assert_eq!(outcome(r#"{"type":"RunFinished","runId":"r"}"#), None);
    assert_eq!(
        outcome(r#"{"type":"RunFinished","runId":"r","outcome":{"type":"success","pendingToolCallIds":["c1"]}}"#),
        Some(Outcome::Success { pending_tool_call_ids: vec!["c1".into()] })
    );
    // AG-UI reads an outcome it doesn't know as success.
    assert_eq!(outcome(r#"{"type":"RunFinished","runId":"r","outcome":{"type":"paused"}}"#), Some(Outcome::Unknown));
}

#[test]
fn origin_kinds_this_version_does_not_know_are_kept() {
    let origin: Origin = serde_json::from_value(json!({"kind": "crew", "vessel": "v1", "turn": 3})).unwrap();
    assert_eq!(origin.kind, OriginKind::Other("crew".into()));
    assert_eq!(serde_json::to_value(&origin).unwrap(), json!({"kind": "crew", "vessel": "v1", "turn": 3}));
    let note: Origin = serde_json::from_value(json!({"kind": "task-notification"})).unwrap();
    assert_eq!(note.kind, OriginKind::TaskNotification);
}

#[test]
fn unknown_events_and_fields_are_skipped() {
    let bytes = br#"{"type":"StepStarted","threadId":"t1","seq":1,"stepName":"x"}
{"type":"TextMessageStart","threadId":"t1","seq":2,"messageId":"m1","role":"user","mood":"calm","nested":{"a":[1]}}

{"type":"ToolCallStart","threadId":"t1","seq":3,"toolCallId":"c1","toolCallName":"X","kind":"teleport"}
{"type":"ToolCallResult","threadId":"t1","seq":4,"toolCallId":"c1","status":"exploded","truncated":{"totalBytes":9,"ref":"log:1","extra":1}}
"#;
    let events = events(bytes);
    assert_eq!(events.len(), 3);
    assert!(matches!(&events[0], Event::TextMessageStart(e) if e.role == Role::User));
    assert!(matches!(&events[1], Event::ToolCallStart(e) if e.kind == Some(ToolKind::Other)));
    let Event::ToolCallResult(result) = &events[2] else { panic!() };
    assert_eq!(result.status, Some(ToolStatus::Unknown));
    assert_eq!(result.truncated, Some(Truncated { total_bytes: 9, reference: "log:1".into() }));
}

#[test]
fn a_bad_line_is_an_error_a_reader_can_skip() {
    let lines: Vec<_> = read(b"{\"type\":\"Hello\",\"contract\":1}\nnot json\n{\"type\":\"TextMessageEnd\"}\n").collect();
    assert!(lines[0].is_ok());
    assert_eq!(lines[1].as_ref().unwrap_err().line, 2);
    // A known event missing a required field.
    assert_eq!(lines[2].as_ref().unwrap_err().line, 3);
}

#[test]
fn events_written_read_back_the_same() {
    let original = events(WIRE);
    let mut out = vec![];
    write(&mut out, &original).unwrap();
    assert_eq!(events(&out), original);
    // Absent options stay absent, so a written stream adds no fields.
    assert!(!to_line(&original[2]).contains("null"));
}

#[test]
fn a_run_start_keeps_its_timestamp_when_it_has_one() {
    let timed = r#"{"type":"RunStarted","threadId":"t1","seq":1,"runId":"r","timestamp":1800000000000}"#;
    let Ok(Some(Event::RunStarted(start))) = parse_line(timed.as_bytes()) else { panic!() };
    assert_eq!(start.timestamp, Some(1_800_000_000_000));
    assert_eq!(to_line(&Event::RunStarted(start)), timed);
    let untimed = r#"{"type":"RunStarted","threadId":"t1","seq":1,"runId":"r"}"#;
    let Ok(Some(Event::RunStarted(start))) = parse_line(untimed.as_bytes()) else { panic!() };
    assert_eq!(start.timestamp, None);
    assert_eq!(to_line(&Event::RunStarted(start)), untimed);
}

fn lines_of(pieces: &[&[u8]]) -> Vec<(u64, Vec<u8>)> {
    let mut lines = Lines::default();
    let mut out = vec![];
    for piece in pieces {
        lines.feed(piece, |at, line| out.push((at, line.to_vec())));
    }
    out
}

#[hegel::test(test_cases = 500)]
#[cfg_attr(windows, ignore = "#654: the wire fixture checks out with CRLF under core.autocrlf=true")]
fn lines_are_the_same_however_the_bytes_are_split(tc: TestCase) {
    let mut bytes = WIRE.to_vec();
    if tc.draw(gs::booleans()) {
        // A last line without its newline still counts.
        bytes.pop();
    }
    let whole = lines_of(&[&bytes]);
    assert_eq!(whole.len(), 25);
    let mut cuts: Vec<usize> =
        (0..tc.draw(gs::integers::<usize>().max_value(12))).map(|_| tc.draw(gs::integers::<usize>().max_value(bytes.len()))).collect();
    cuts.sort_unstable();
    let mut pieces = vec![];
    let mut at = 0;
    for cut in cuts {
        pieces.push(&bytes[at..cut]);
        at = cut;
    }
    pieces.push(&bytes[at..]);
    assert_eq!(lines_of(&pieces), whole);
}

#[test]
fn lines_report_their_offsets() {
    let mut lines = Lines::starting_at(100);
    let mut out = vec![];
    lines.feed(b"{\"a\":1}\r\n\n{\"b\"", |at, line| out.push((at, line.to_vec())));
    lines.feed(b":2}", |at, line| out.push((at, line.to_vec())));
    assert_eq!(out, vec![(100, b"{\"a\":1}\r".to_vec()), (109, vec![]), (110, b"{\"b\":2}".to_vec())]);
    assert_eq!(lines.end(), 117);
    // Taken early, so its newline adds nothing.
    lines.feed(b"\n", |at, line| out.push((at, line.to_vec())));
    assert_eq!(out.len(), 3);
}

#[test]
fn the_fold_turns_the_stream_into_records() {
    let mut fold = Fold::default();
    for event in events(WIRE) {
        fold.push(&event);
    }
    let messages = fold.messages("t1");
    let ids: Vec<(&str, Role)> = messages.iter().map(|m| (m.id.as_str(), m.role)).collect();
    assert_eq!(
        ids,
        [
            ("m1", Role::User),
            ("rs1", Role::Reasoning),
            ("c1", Role::Assistant),
            ("m2", Role::Tool),
            ("c2", Role::Assistant),
            ("m3", Role::Tool),
            ("c3", Role::Assistant),
            ("m4", Role::User),
        ]
    );
    assert_eq!(messages[0].text(), "Fix the **failing** test");
    let call = &messages[2].tool_calls[0];
    assert_eq!((call.function.name.as_str(), call.function.arguments.as_str()), ("Bash", r#"{"command":"cargo test -p wheelhouse-text"}"#));
    assert_eq!(call.kind, Some(ToolKind::Shell));
    assert_eq!((messages[3].tool_call_id.as_deref(), messages[3].status), (Some("c1"), Some(ToolStatus::Failed)));
    // Streamed output isn't kept: the result replaces it.
    assert_eq!(messages[3].text(), "\x1b[31merror\x1b[0m: 1 test failed\n");
    // A title that came after the start.
    let search = &messages[4].tool_calls[0];
    assert_eq!((search.title.as_deref(), search.kind), (Some("parser error recovery"), Some(ToolKind::Web)));
    assert_eq!(messages[7].origin.as_ref().map(|o| &o.kind), Some(&OriginKind::Peer));
    assert!(!fold.is_open("t1", "m1"));
    let snapshot = fold.snapshot("t1");
    assert_eq!(snapshot.seq, 24);
    // The record form reads back as the same records.
    let mut again = Fold::default();
    again.push(&Event::MessagesSnapshot(snapshot));
    assert_eq!(again.messages("t1"), messages);
}

#[test]
fn an_open_message_is_in_the_snapshot_with_its_content_so_far() {
    let mut fold = Fold::default();
    fold.push(&Event::TextMessageStart(TextMessageStart {
        thread_id: "t".into(),
        seq: 1,
        message_id: "m".into(),
        role: Role::Assistant,
        origin: None,
    }));
    fold.push(&Event::TextMessageContent(TextMessageContent {
        thread_id: "t".into(),
        seq: 2,
        message_id: "m".into(),
        delta: "Hal".into(),
    }));
    assert!(fold.is_open("t", "m"));
    assert_eq!(fold.snapshot("t").messages[0].text(), "Hal");
}

#[test]
fn activity_deltas_patch_the_snapshot() {
    let mut fold = Fold::default();
    let content = json!({"todos": [{"text": "a", "done": false}]});
    fold.push(&Event::ActivitySnapshot(ActivitySnapshot {
        message_id: "plan".into(),
        activity_type: "todo".into(),
        content,
        ..ActivitySnapshot::default()
    }));
    let patch = vec![
        json!({"op": "replace", "path": "/todos/0/done", "value": true}),
        json!({"op": "add", "path": "/todos/-", "value": {"text": "b"}}),
        json!({"op": "move", "path": "/x", "from": "/todos"}),
    ];
    fold.push(&Event::ActivityDelta(ActivityDelta { message_id: "plan".into(), patch, ..ActivityDelta::default() }));
    let content: &Value = fold.messages("").first().unwrap().content.as_ref().unwrap();
    assert_eq!(content, &json!({"todos": [{"text": "a", "done": true}, {"text": "b"}]}));
}

#[test]
fn a_patch_path_without_its_slash_is_skipped() {
    let mut fold = Fold::default();
    fold.push(&Event::ActivitySnapshot(ActivitySnapshot {
        message_id: "m".into(),
        content: json!({"x": 1}),
        ..ActivitySnapshot::default()
    }));
    let patch = vec![json!({"op": "add", "path": "x", "value": 2}), json!({"op": "replace", "path": "/x", "value": 3})];
    fold.push(&Event::ActivityDelta(ActivityDelta { message_id: "m".into(), patch, ..ActivityDelta::default() }));
    assert_eq!(fold.messages("")[0].content, Some(json!({"x": 3})));
}

#[test]
fn a_snapshot_mid_stream_replaces_the_thread_and_later_deltas_extend_it() {
    let mut fold = Fold::default();
    for event in events(WIRE) {
        fold.push(&event);
    }
    let message = Message { id: "m9".into(), role: Role::Assistant, content: Some(json!("Half")), ..Message::default() };
    fold.push(&Event::MessagesSnapshot(MessagesSnapshot { thread_id: "t1".into(), seq: 30, messages: vec![message] }));
    fold.push(&Event::TextMessageContent(TextMessageContent {
        thread_id: "t1".into(),
        seq: 31,
        message_id: "m9".into(),
        delta: " done".into(),
    }));
    let messages = fold.messages("t1");
    assert_eq!(messages.len(), 1);
    assert_eq!(messages[0].text(), "Half done");
    // A delta for a message the snapshot dropped goes nowhere.
    fold.push(&Event::TextMessageContent(TextMessageContent {
        thread_id: "t1".into(),
        seq: 32,
        message_id: "m1".into(),
        delta: "x".into(),
    }));
    assert_eq!(fold.messages("t1").len(), 1);
}

#[test]
fn a_result_without_a_message_id_gets_one_from_its_call() {
    let mut fold = Fold::default();
    let result = ToolCallResult { thread_id: "t".into(), tool_call_id: "c7".into(), content: "ok".into(), ..ToolCallResult::default() };
    fold.push(&Event::ToolCallResult(result));
    assert_eq!(fold.messages("t")[0].id, "result:c7");
}

#[test]
fn unknown_events_are_not_written() {
    let mut out = vec![];
    write(&mut out, &[Event::Unknown, Event::Hello(Hello { contract: CONTRACT, ..Hello::default() })]).unwrap();
    assert_eq!(out, b"{\"type\":\"Hello\",\"contract\":2}\n");
}

#[test]
fn the_claude_translators_goldens_read_as_version_two() {
    let goldens = concat!(env!("CARGO_MANIFEST_DIR"), "/../../fixtures/claude-code");
    let mut origins = vec![];
    let mut checklists = vec![];
    let mut tasks = vec![];
    for entry in fs::read_dir(goldens).unwrap() {
        let path = entry.unwrap().path();
        if !path.to_string_lossy().ends_with(".events.jsonl") {
            continue;
        }
        let events = events(&fs::read(&path).unwrap());
        assert_eq!(events[0], Event::Hello(Hello { contract: CONTRACT, ..Hello::default() }), "{path:?}");
        for event in events {
            match event {
                Event::TextMessageStart(start) => origins.extend(start.origin),
                Event::StateSnapshot(document) if document.key.as_deref() == Some(CHECKLIST) => {
                    assert_eq!(document.media_type.as_deref(), Some(CHECKLIST_MEDIA_TYPE));
                    checklists.push(serde_json::from_value::<Vec<ChecklistItem>>(document.snapshot).unwrap());
                }
                Event::StateSnapshot(document) if document.key.as_deref() == Some(TASKS) => {
                    assert_eq!(document.media_type.as_deref(), Some(TASKS_MEDIA_TYPE));
                    tasks.push(serde_json::from_value::<Vec<AgentTask>>(document.snapshot).unwrap());
                }
                _ => {}
            }
        }
    }
    let steps: Vec<ChecklistStatus> = checklists.iter().flatten().map(|item| item.status).collect();
    assert!(!steps.contains(&ChecklistStatus::Unknown));
    let ended: Vec<(TaskKind, TaskStatus)> = tasks.iter().flatten().filter(|t| t.ended_at.is_some()).map(|t| (t.kind, t.status)).collect();
    for status in [TaskStatus::Completed, TaskStatus::Failed, TaskStatus::Cancelled] {
        assert!(ended.contains(&(TaskKind::Shell, status)), "{status:?}");
    }
    assert!(tasks.iter().flatten().any(|t| t.kind == TaskKind::Subagent && t.child_thread_id.is_some()));
    let peer = origins.iter().find(|o| o.kind == OriginKind::Peer).expect("a peer message");
    assert_eq!(peer.fields["name"], json!("reviewer"));
    assert!(origins.iter().any(|o| o.kind == OriginKind::TaskNotification));
    assert!(origins.iter().any(|o| o.kind == OriginKind::Other("harness".into())));
}

#[test]
fn an_interrupt_outcome_without_its_list_still_reads() {
    let line = br#"{"type":"RunFinished","runId":"r","outcome":{"type":"interrupt"}}"#;
    let Ok(Some(Event::RunFinished(e))) = parse_line(line) else { panic!() };
    assert_eq!(e.outcome, Some(Outcome::Interrupt { interrupts: vec![] }));
}

fn text(tc: &TestCase) -> String {
    tc.draw(gs::text().max_size(12))
}

fn maybe_text(tc: &TestCase) -> Option<String> {
    tc.draw(gs::booleans()).then(|| text(tc))
}

fn has_null(value: &Value) -> bool {
    match value {
        Value::Null => true,
        Value::Array(items) => items.iter().any(has_null),
        Value::Object(fields) => fields.values().any(has_null),
        _ => false,
    }
}

fn round_trips<T: Serialize + DeserializeOwned + PartialEq + Debug>(value: T) {
    let wire = serde_json::to_value(&value).unwrap();
    // Absent options stay absent.
    assert!(!has_null(&wire), "{wire}");
    assert_eq!(serde_json::from_value::<T>(wire).unwrap(), value);
}

#[hegel::test(test_cases = 200)]
fn agent_state_documents_read_back_as_written(tc: TestCase) {
    round_trips(ChecklistItem {
        id: text(&tc),
        content: text(&tc),
        status: tc.draw(gs::sampled_from(vec![ChecklistStatus::Pending, ChecklistStatus::InProgress, ChecklistStatus::Completed])),
        active_form: maybe_text(&tc),
    });
    let started_at = tc.draw(gs::integers::<u64>());
    round_trips(AgentTask {
        id: text(&tc),
        kind: tc.draw(gs::sampled_from(vec![TaskKind::Subagent, TaskKind::Shell, TaskKind::Monitor, TaskKind::Other])),
        title: text(&tc),
        status: tc.draw(gs::sampled_from(vec![TaskStatus::Running, TaskStatus::Completed, TaskStatus::Failed, TaskStatus::Cancelled])),
        started_at,
        ended_at: tc.draw(gs::booleans()).then(|| tc.draw(gs::integers::<u64>().min_value(started_at))),
        tool_call_id: text(&tc),
        child_thread_id: maybe_text(&tc),
        awaited: tc.draw(gs::booleans()).then(|| tc.draw(gs::booleans())),
        output: tc.draw(gs::booleans()).then(|| TaskOutput {
            path: maybe_text(&tc),
            local: tc.draw(gs::booleans()),
            url: maybe_text(&tc),
            media_type: maybe_text(&tc),
        }),
    });
    round_trips(AgentCommand {
        name: text(&tc),
        description: text(&tc),
        hint: maybe_text(&tc),
        aliases: tc.draw(gs::vecs(gs::text().max_size(6)).max_size(3)),
        insert: text(&tc),
        group: maybe_text(&tc),
    });
    round_trips(FileMatch { path: text(&tc), kind: tc.draw(gs::sampled_from(vec![FileKind::File, FileKind::Dir])) });
}

#[test]
fn agent_state_documents_read_as_producers_write_them() {
    let checklist: Vec<ChecklistItem> = serde_json::from_value(json!([
        {"id": "1", "content": "Run the tests", "status": "in_progress", "activeForm": "Running the tests", "priority": "high"},
        {"id": "2", "content": "Ship", "status": "blocked"},
    ]))
    .unwrap();
    assert_eq!(
        checklist,
        [
            ChecklistItem {
                id: "1".into(),
                content: "Run the tests".into(),
                status: ChecklistStatus::InProgress,
                active_form: Some("Running the tests".into()),
            },
            ChecklistItem { id: "2".into(), content: "Ship".into(), status: ChecklistStatus::Unknown, active_form: None },
        ]
    );
    let tasks: Vec<AgentTask> = serde_json::from_value(json!([
        {"id": "b1", "kind": "shell", "title": "cargo build", "status": "completed", "startedAt": 1, "endedAt": 2,
         "toolCallId": "c1", "awaited": false, "pid": 7,
         "output": {"path": "/tmp/b1.out", "local": true, "url": "/threads/t/tasks/b1/output", "mediaType": ANSI, "size": 3}},
        {"id": "a1", "kind": "cron", "title": "Explore", "status": "paused", "startedAt": 3, "childThreadId": "t2"},
    ]))
    .unwrap();
    let output = tasks[0].output.as_ref().unwrap();
    assert_eq!(
        (output.path.as_deref(), output.local, output.url.as_deref()),
        (Some("/tmp/b1.out"), true, Some("/threads/t/tasks/b1/output"))
    );
    assert_eq!(
        (tasks[0].kind, tasks[0].status, tasks[0].ended_at, tasks[0].awaited),
        (TaskKind::Shell, TaskStatus::Completed, Some(2), Some(false))
    );
    assert_eq!((tasks[1].kind, tasks[1].status, tasks[1].child_thread_id.as_deref()), (TaskKind::Other, TaskStatus::Unknown, Some("t2")));
    // A producer that omits the call writes none back.
    assert_eq!(serde_json::to_value(&tasks[1]).unwrap()["toolCallId"], Value::Null);
    let commands: Vec<AgentCommand> = serde_json::from_value(json!([
        {"name": "review", "description": "Review changes", "hint": "[base]", "insert": "/review ", "source": "builtin"},
        {"name": "lint", "description": "Lint", "aliases": ["l"], "insert": "$lint ", "group": "Skills"},
    ]))
    .unwrap();
    assert_eq!((commands[0].hint.as_deref(), commands[0].insert.as_str()), (Some("[base]"), "/review "));
    assert_eq!((&commands[1].aliases[..], commands[1].group.as_deref()), (&["l".to_string()][..], Some("Skills")));
    let matches: Vec<FileMatch> = serde_json::from_value(
        json!([{"path": "src/lib.rs", "kind": "file", "score": 9}, {"path": "src", "kind": "dir"}, {"path": "x", "kind": "link"}]),
    )
    .unwrap();
    assert_eq!(matches.iter().map(|m| m.kind).collect::<Vec<_>>(), [FileKind::File, FileKind::Dir, FileKind::Unknown]);
}

#[test]
fn a_state_document_is_typed_by_its_key_and_media_type() {
    let line = br#"{"type":"StateSnapshot","threadId":"t","seq":4,"key":"checklist","mediaType":"application/vnd.flotilla.checklist+json","snapshot":[{"id":"1","content":"a","status":"pending"}]}"#;
    let Ok(Some(Event::StateSnapshot(document))) = parse_line(line) else { panic!() };
    assert_eq!((document.key.as_deref(), document.media_type.as_deref()), (Some(CHECKLIST), Some(CHECKLIST_MEDIA_TYPE)));
    let checklist: Vec<ChecklistItem> = serde_json::from_value(document.snapshot).unwrap();
    assert_eq!(checklist[0].status, ChecklistStatus::Pending);
    assert_eq!(
        (TASKS, TASKS_MEDIA_TYPE, COMMANDS, COMMANDS_MEDIA_TYPE),
        ("tasks", "application/vnd.flotilla.tasks+json", "commands", "application/vnd.flotilla.commands+json")
    );
}
