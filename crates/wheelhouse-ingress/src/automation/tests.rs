use super::*;
use hegel::generators as gs;
use hegel::TestCase;

/// A JSON value of the shapes the trees carry: objects, arrays, text,
/// booleans, integers (some beyond 2^53) and floats.
fn draw_value(tc: &TestCase, depth: u32) -> Value {
    let leaf = depth == 0;
    match tc.draw(gs::integers::<u8>().max_value(if leaf { 5 } else { 7 })) {
        0 => Value::Null,
        1 => tc.draw(gs::booleans()).into(),
        2 => tc.draw(gs::integers::<u64>()).into(),
        3 => tc.draw(gs::integers::<i64>()).into(),
        4 => tc.draw(gs::floats::<f64>().allow_nan(false).allow_infinity(false)).into(),
        5 => tc.draw(gs::text().max_size(8)).into(),
        6 => (0..tc.draw(gs::integers::<usize>().max_value(3))).map(|_| draw_value(tc, depth - 1)).collect(),
        _ => (0..tc.draw(gs::integers::<usize>().max_value(3)))
            .map(|_| (tc.draw(gs::text().max_size(6)), draw_value(tc, depth - 1)))
            .collect::<Map<_, _>>()
            .into(),
    }
}

/// `value` read back from CBOR, checked against ADR 0063's profile:
/// definite lengths, no tags, text map keys, shortest integer heads,
/// map keys in the bytewise order of their encodings.
fn read_profile(bytes: &[u8]) -> Value {
    fn check(bytes: &[u8], at: &mut usize) -> Value {
        let initial = bytes[*at];
        *at += 1;
        let (major, info) = (initial >> 5, initial & 0x1f);
        assert_ne!(major, 6, "a tag");
        assert_ne!(info, 31, "an indefinite length");
        let argument = |at: &mut usize| -> u64 {
            let n = match info {
                0..=23 => return u64::from(info),
                24 => 1,
                25 => 2,
                26 => 4,
                27 => 8,
                _ => panic!("reserved additional information {info}"),
            };
            let value = bytes[*at..*at + n].iter().fold(0u64, |acc, b| acc << 8 | u64::from(*b));
            *at += n;
            if major != 7 {
                let shortest = match value {
                    0..=23 => 0,
                    24..=0xff => 1,
                    0x100..=0xffff => 2,
                    0x1_0000..=0xffff_ffff => 4,
                    _ => 8,
                };
                assert_eq!(n, shortest, "a longer head than needed for {value}");
            }
            value
        };
        match major {
            0 => argument(at).into(),
            1 => (-1 - argument(at) as i128).try_into().map(|n: i64| n.into()).expect("in i64 range"),
            3 => {
                let len = argument(at) as usize;
                let text = std::str::from_utf8(&bytes[*at..*at + len]).expect("UTF-8 text").to_owned();
                *at += len;
                text.into()
            }
            4 => (0..argument(at)).map(|_| check(bytes, at)).collect(),
            5 => {
                let mut map = Map::new();
                let mut previous: Option<&[u8]> = None;
                for _ in 0..argument(at) {
                    let start = *at;
                    assert_eq!(bytes[start] >> 5, 3, "a map key that isn't text");
                    let Value::String(key) = check(bytes, at) else { unreachable!() };
                    let encoded = &bytes[start..*at];
                    assert!(previous.is_none_or(|previous| previous < encoded), "map keys out of order");
                    previous = Some(encoded);
                    map.insert(key, check(bytes, at));
                }
                map.into()
            }
            7 => match info {
                20 => false.into(),
                21 => true.into(),
                22 => Value::Null,
                25..=27 => {
                    let value: ciborium::Value = ciborium::from_reader(&bytes[*at - 1..]).expect("a float");
                    *at += [2, 4, 8][usize::from(info - 25)];
                    let x = value.as_float().expect("a float");
                    // The shortest width that keeps it.
                    let mut shortest = Vec::new();
                    float(x, &mut shortest);
                    assert_eq!(shortest.len() - 1, [2, 4, 8][usize::from(info - 25)], "{x} not in its shortest width");
                    serde_json::Number::from_f64(x).map_or(Value::Null, Value::Number)
                }
                _ => panic!("simple value {info}"),
            },
            _ => panic!("major type {major}"),
        }
    }
    let mut at = 0;
    let value = check(bytes, &mut at);
    assert_eq!(at, bytes.len(), "trailing bytes");
    value
}

/// What JSON carries for `value`, its large integers read back from
/// `{"u64": "..."}`.
fn read_json(bytes: &[u8]) -> Value {
    fn unwrap(value: Value) -> Value {
        match value {
            Value::Object(map) if map.len() == 1 && map.get("u64").is_some_and(Value::is_string) => {
                let text = map["u64"].as_str().unwrap();
                text.parse::<u64>().map(Value::from).unwrap_or_else(|_| text.parse::<i64>().unwrap().into())
            }
            Value::Array(items) => items.into_iter().map(unwrap).collect(),
            Value::Object(map) => map.into_iter().map(|(k, v)| (k, unwrap(v))).collect::<Map<_, _>>().into(),
            other => other,
        }
    }
    unwrap(serde_json::from_slice(bytes).unwrap())
}

/// Equal, but for a float's last bit: serde_json's default reader isn't
/// correctly rounded, though the text it reads is the float's shortest.
fn same(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Number(x), Value::Number(y)) if x.is_f64() || y.is_f64() => {
            let (x, y) = (x.as_f64().unwrap(), y.as_f64().unwrap());
            x == y || (x.to_bits() as i64 - y.to_bits() as i64).abs() <= 1
        }
        (Value::Array(x), Value::Array(y)) => x.len() == y.len() && x.iter().zip(y).all(|(x, y)| same(x, y)),
        (Value::Object(x), Value::Object(y)) => x.len() == y.len() && x.iter().all(|(k, v)| y.get(k).is_some_and(|w| same(v, w))),
        _ => a == b,
    }
}

#[hegel::test(test_cases = 300)]
fn json_and_cbor_encode_the_same_value_and_cbor_keeps_the_profile(tc: TestCase) {
    let value = draw_value(&tc, 3);
    let cbor = cbor(&value);
    assert_eq!(read_profile(&cbor), value);
    let json = serde_json::to_vec(&json_safe(&value)).unwrap();
    // Every integer JSON carries as a number is one readers keep exactly.
    fn safe(value: &Value) -> bool {
        match value {
            Value::Number(n) => match (n.as_u64(), n.as_i64()) {
                (Some(n), _) => n <= SAFE,
                (_, Some(n)) => n.unsigned_abs() <= SAFE,
                _ => true,
            },
            Value::Array(items) => items.iter().all(safe),
            Value::Object(map) => map.values().all(safe),
            _ => true,
        }
    }
    assert!(safe(&serde_json::from_slice(&json).unwrap()));
    // Objects that happen to look like {"u64": text} aren't drawn with
    // numeric text often enough to matter; skip the rare ambiguous case.
    let ambiguous = |v: &Value| v.to_string().contains("\"u64\"");
    if !ambiguous(&value) {
        assert!(same(&read_json(&json), &value), "{value}");
    }
}

#[test]
fn large_integers_render_as_u64_text_in_json() {
    let value = json!({"small": 1u64 << 53, "large": (1u64 << 53) + 1, "max": u64::MAX, "negative": -(1i64 << 60)});
    let json: Value = serde_json::from_slice(&serde_json::to_vec(&json_safe(&value)).unwrap()).unwrap();
    assert_eq!(json["small"], 1u64 << 53);
    assert_eq!(json["large"], json!({"u64": "9007199254740993"}));
    assert_eq!(json["max"], json!({"u64": "18446744073709551615"}));
    assert_eq!(json["negative"], json!({"u64": "-1152921504606846976"}));
    // CBOR carries them as integers.
    assert_eq!(read_profile(&cbor(&value)), value);
}

#[test]
fn floats_take_their_shortest_width() {
    for (x, width) in [(0.0, 2), (1.5, 2), (65504.0, 2), (5.960464477539063e-8, 2), (0.1, 8), (1.0e10, 4), (100000.0, 4)] {
        let mut out = Vec::new();
        float(x, &mut out);
        assert_eq!(out.len() - 1, width, "{x}");
        let back: ciborium::Value = ciborium::from_reader(out.as_slice()).unwrap();
        assert_eq!(back.as_float(), Some(x));
    }
}

fn node(id: &str, kind: &str, children: Vec<Value>) -> Value {
    let mut node = json!({"id": id, "kind": kind, "durable": true});
    if !children.is_empty() {
        node["children"] = children.into();
    }
    node
}

fn terminal(id: &str, session: &str, text: &str) -> Value {
    let mut view = node(id, "view", vec![]);
    view["props"] = json!({"content": "terminal", "terminal": {"session": session, "text": [text], "cursor": {"row": 0, "col": 0}}});
    view
}

/// Two workspaces: one with a split of a terminal and a sessions View, the
/// other with a terminal showing the same session.
fn tree() -> Vec<Value> {
    let a = "workspace/00000000-0000-0000-0000-00000000000a";
    let b = "workspace/00000000-0000-0000-0000-00000000000b";
    let mut first = node(
        a,
        "workspace",
        vec![node(
            &format!("{a}/panel/1"),
            "panel",
            vec![
                node(&format!("{a}/panel/2"), "panel", vec![terminal(&format!("{a}/view/u:1"), "s1", "$ ls")]),
                node(&format!("{a}/panel/3"), "panel", vec![node(&format!("{a}/view/u:2"), "view", vec![])]),
            ],
        )],
    );
    first["props"] = json!({"subject": {"provider": "p", "kind": "vessel", "id": "v"}});
    vec![first, node(b, "workspace", vec![terminal(&format!("{b}/view/u:1"), "s1", "$ top")])]
}

fn request(route: Route, query: &str) -> Request {
    Request::parse(route, Some(query), None).unwrap()
}

fn body(reply: &Reply) -> Value {
    assert_eq!(reply.status, 200);
    serde_json::from_slice(&reply.body).unwrap()
}

#[test]
fn root_returns_exactly_that_subtree() {
    let tree = tree();
    let all: Vec<&Value> = {
        fn walk<'a>(node: &'a Value, out: &mut Vec<&'a Value>) {
            out.push(node);
            for child in node.get("children").and_then(Value::as_array).into_iter().flatten() {
                walk(child, out);
            }
        }
        let mut out = Vec::new();
        tree.iter().for_each(|node| walk(node, &mut out));
        out
    };
    for wanted in all {
        let id = wanted["id"].as_str().unwrap();
        let reply = respond(&request(Route::Semantic, &format!("root={id}&include=terminal_text")), &tree, 3, MAX_RESPONSE);
        let value = body(&reply);
        assert_eq!(value["schema"], 1);
        assert_eq!(value["generation"], 3);
        assert_eq!(value["nodes"], json!([wanted]));
    }
    // Percent-encoded, as a client would send it.
    let encoded = "root=workspace%2F00000000-0000-0000-0000-00000000000b";
    assert_eq!(body(&respond(&request(Route::Semantic, encoded), &tree, 1, MAX_RESPONSE))["nodes"][0]["id"], tree[1]["id"]);
    assert_eq!(respond(&request(Route::Semantic, "root=workspace/none"), &tree, 1, MAX_RESPONSE).status, 404);
    // Without a root, every workspace.
    assert_eq!(body(&respond(&request(Route::Semantic, ""), &tree, 1, MAX_RESPONSE))["nodes"].as_array().unwrap().len(), 2);
}

#[test]
fn terminal_text_only_when_asked() {
    let tree = tree();
    let without = body(&respond(&request(Route::Semantic, ""), &tree, 1, MAX_RESPONSE)).to_string();
    assert!(!without.contains("$ ls") && !without.contains("cursor"));
    let with = body(&respond(&request(Route::Semantic, "include=terminal_text"), &tree, 1, MAX_RESPONSE)).to_string();
    assert!(with.contains("$ ls") && with.contains("cursor"));
}

#[test]
fn caller_context_lists_the_views_showing_a_session() {
    let tree = tree();
    let value = body(&respond(&request(Route::Caller, "session=s1"), &tree, 1, MAX_RESPONSE));
    assert_eq!(value["session"], "s1");
    assert_eq!(
        value["views"],
        json!([
            {"id": "workspace/00000000-0000-0000-0000-00000000000a/view/u:1", "workspace": "workspace/00000000-0000-0000-0000-00000000000a",
             "subject": {"provider": "p", "kind": "vessel", "id": "v"}},
            {"id": "workspace/00000000-0000-0000-0000-00000000000b/view/u:1", "workspace": "workspace/00000000-0000-0000-0000-00000000000b"},
        ])
    );
    // No view shows it: an empty list, not an error.
    assert_eq!(body(&respond(&request(Route::Caller, "session=other"), &tree, 1, MAX_RESPONSE))["views"], json!([]));
    // A root narrows it.
    let narrowed =
        respond(&request(Route::Caller, "session=s1&root=workspace/00000000-0000-0000-0000-00000000000b"), &tree, 1, MAX_RESPONSE);
    assert_eq!(body(&narrowed)["views"].as_array().unwrap().len(), 1);
    assert!(Request::parse(Route::Caller, Some(""), None).is_err());
}

#[test]
fn the_size_cap_drops_terminal_text_first() {
    let mut tree = tree();
    let big = "x".repeat(4000);
    tree[0]["children"][0]["children"][0]["children"][0]["props"]["terminal"]["text"] = json!([big]);
    let asked = request(Route::Semantic, "include=terminal_text");
    let whole = respond(&asked, &tree, 1, MAX_RESPONSE);
    assert!(whole.body.len() > 4000 && body(&whole).get("truncated").is_none());
    // Over the cap: the text goes, the nodes stay.
    let capped = body(&respond(&asked, &tree, 1, 3000));
    assert_eq!(capped["truncated"], true);
    assert!(!capped.to_string().contains("xxxx") && !capped.to_string().contains("cursor"));
    assert_eq!(capped["nodes"].as_array().unwrap().len(), 2);
    assert_eq!(capped["nodes"][0]["children"][0]["children"][0]["children"][0]["props"]["terminal"]["session"], "s1");
    // Still over: whole workspaces from the end.
    let tiny = body(&respond(&asked, &tree, 1, 700));
    assert_eq!(tiny["truncated"], true);
    assert!(tiny["nodes"].as_array().unwrap().len() < 2);
}

#[hegel::test(test_cases = 200)]
fn the_generation_only_increases_and_moves_only_on_change(tc: TestCase) {
    let shapes = [tree(), tree()[..1].to_vec(), Vec::new()];
    let mut generation = Generation::default();
    let mut last: Option<Vec<Value>> = None;
    let mut seen = 0;
    for _ in 0..tc.draw(gs::integers::<usize>().max_value(12)) {
        let mut next = shapes[tc.draw(gs::integers::<usize>().max_value(2))].clone();
        // Screen text changes alone don't count.
        if tc.draw(gs::booleans()) {
            for node in &mut next {
                drop_terminal_text(node);
            }
        }
        let now = generation.observe(&next);
        let mut plain = next.clone();
        plain.iter_mut().for_each(|node| {
            drop_terminal_text(node);
        });
        let changed = last.as_ref() != Some(&plain);
        assert_eq!(now, if changed { seen + 1 } else { seen });
        seen = now;
        last = Some(plain);
    }
}

// A gap with no reader doesn't move the generation by itself: the tree is
// compared with the last one observed, which is what the last read saw.
#[test]
fn an_idle_gap_alone_does_not_move_the_generation() {
    let mut generation = Generation::default();
    let start = Instant::now();
    generation.read(start);
    assert_eq!(generation.observe(&tree()), 1);
    assert!(!generation.active(false, start + RECENT * 3));
    assert_eq!(generation.observe(&tree()), 1);
}

#[test]
fn reading_keeps_it_comparing_for_a_while() {
    let mut generation = Generation::default();
    let start = Instant::now();
    assert!(!generation.active(false, start));
    assert!(generation.active(true, start));
    generation.read(start);
    assert!(generation.active(false, start + RECENT / 2));
    assert!(!generation.active(false, start + RECENT));
}

#[test]
fn queries_parse() {
    let parsed =
        Request::parse(Route::Semantic, Some("since=4&wait=120000&include=a,terminal_text&root=w%2Fx"), Some("application/cbor")).unwrap();
    assert_eq!(parsed.since, Some(4));
    assert_eq!(parsed.wait, MAX_WAIT);
    assert!(parsed.terminal_text);
    assert_eq!(parsed.root.as_deref(), Some("w/x"));
    assert_eq!(parsed.encoding, Encoding::Cbor);
    assert_eq!(Request::parse(Route::Semantic, None, Some("application/json")).unwrap().encoding, Encoding::Json);
    assert!(Request::parse(Route::Semantic, Some("since=x"), None).is_err());
    // A wait with nothing to wait past is a mistake.
    assert!(Request::parse(Route::Semantic, Some("wait=100"), None).is_err());
    // A `+` is itself; the last of a repeated parameter wins.
    let parsed = Request::parse(Route::Caller, Some("session=a+b%2Bc&root=x&root=y"), None).unwrap();
    assert_eq!((parsed.session.as_deref(), parsed.root.as_deref()), (Some("a+b+c"), Some("y")));
    assert!(Request::parse(Route::Semantic, Some("root=%zz"), None).is_err());
}
