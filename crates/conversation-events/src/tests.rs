use hegel::generators as gs;
use hegel::TestCase;
use serde_json::{json, Value};

use super::*;

const WIRE: &[u8] = include_bytes!("../fixtures/wire.jsonl");

fn events(bytes: &[u8]) -> Vec<Event> {
    read(bytes).collect::<Result<_, _>>().unwrap()
}

#[test]
fn the_wire_example_reads_with_our_additions() {
    let events = events(WIRE);
    assert_eq!(events.len(), 14);
    assert_eq!(events[0], Event::Hello(Hello { contract: CONTRACT }));
    let Event::ToolCallStart(start) = &events[8] else { panic!("{:?}", events[8]) };
    assert_eq!((start.title.as_deref(), start.kind), (Some("cargo test -p wheelhouse-text"), Some(ToolKind::Shell)));
    let Event::ToolCallResult(result) = &events[11] else { panic!("{:?}", events[11]) };
    assert_eq!(result.status, Some(ToolStatus::Failed));
    assert_eq!(result.media_type.as_deref(), Some(ANSI));
    assert_eq!(result.content, "\x1b[31merror\x1b[0m: 1 test failed\n");
    assert_eq!(result.truncated, None);
    assert_eq!((events[11].thread_id(), events[11].seq()), ("t1", 11));
    let Event::RunFinished(finished) = &events[13] else { panic!() };
    assert_eq!(finished.outcome.as_deref(), Some(COMPLETED));
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

fn lines_of(pieces: &[&[u8]]) -> Vec<(u64, Vec<u8>)> {
    let mut lines = Lines::default();
    let mut out = vec![];
    for piece in pieces {
        lines.feed(piece, |at, line| out.push((at, line.to_vec())));
    }
    out
}

#[hegel::test(test_cases = 500)]
fn lines_are_the_same_however_the_bytes_are_split(tc: TestCase) {
    let mut bytes = WIRE.to_vec();
    if tc.draw(gs::booleans()) {
        // A last line without its newline still counts.
        bytes.pop();
    }
    let whole = lines_of(&[&bytes]);
    assert_eq!(whole.len(), 14);
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
    assert_eq!(ids, [("m1", Role::User), ("rs1", Role::Reasoning), ("c1", Role::Assistant), ("m2", Role::Tool)]);
    assert_eq!(messages[0].text(), "Fix the **failing** test");
    let call = &messages[2].tool_calls[0];
    assert_eq!((call.function.name.as_str(), call.function.arguments.as_str()), ("Bash", r#"{"command":"cargo test -p wheelhouse-text"}"#));
    assert_eq!(call.kind, Some(ToolKind::Shell));
    assert_eq!((messages[3].tool_call_id.as_deref(), messages[3].status), (Some("c1"), Some(ToolStatus::Failed)));
    assert!(!fold.is_open("t1", "m1"));
    let snapshot = fold.snapshot("t1");
    assert_eq!(snapshot.seq, 13);
    // The record form reads back as the same records.
    let mut again = Fold::default();
    again.push(&Event::MessagesSnapshot(snapshot));
    assert_eq!(again.messages("t1"), messages);
}

#[test]
fn an_open_message_is_in_the_snapshot_with_its_content_so_far() {
    let mut fold = Fold::default();
    fold.push(&Event::TextMessageStart(TextMessageStart { thread_id: "t".into(), seq: 1, message_id: "m".into(), role: Role::Assistant }));
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
