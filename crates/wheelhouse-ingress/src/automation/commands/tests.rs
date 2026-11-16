use std::{sync::atomic::AtomicU32, time::Duration};

use hegel::generators as gs;
use hegel::TestCase;
use serde_json::json;

use super::*;
use crate::automation::{frame_end, tests::build, tests::nodes, Generation};

const W: &str = "00000000-0000-0000-0000-00000000000a";

fn command(name: &str, flags: u32, context: &str, arg: Option<Arg>) -> Command {
    Command {
        name: name.into(),
        display_name: format!("{name}!"),
        description: format!("does {name}"),
        tags: "a, b,,".into(),
        context: context.into(),
        flags,
        arg,
    }
}

fn table() -> Vec<Command> {
    let string = Some(Arg { kind: ArgKind::String, required: true });
    vec![
        command("split_panel", LIST_IN_UI, "panel", None),
        command("close_tab", LIST_IN_UI, "view", None),
        command("terminal", LIST_IN_UI | LIST_IN_TAB, "", None),
        command("open_url", LIST_IN_UI, "panel", string),
        command("select_workspace", AUTOMATION, "workspace", None),
        command("set_current_path", 0, "", None),
        command("exit", LIST_IN_UI, "", None),
        command("goto_line", LIST_IN_UI, "view", Some(Arg { kind: ArgKind::Cursor, required: true })),
        command("new_user", LIST_IN_UI, "", Some(Arg { kind: ArgKind::FilePath, required: false })),
        command("run_command", LIST_IN_UI, "", Some(Arg { kind: ArgKind::Command, required: true })),
    ]
}

const KINDS: [ArgKind; 6] = [ArgKind::FilePath, ArgKind::String, ArgKind::Cursor, ArgKind::Cfg, ArgKind::Address, ArgKind::Command];

// The listing is the table's palette and automation-flagged commands, once
// each and in order, each with its argument and targets as its entry has
// them; nothing else is.
#[hegel::test(test_cases = 300)]
fn the_listing_is_exactly_the_listed_commands_as_the_tables_have_them(tc: TestCase) {
    let names = ["a", "b", "c", "d"];
    let contexts = ["", "panel", "view", "workspace", "other"];
    let flag_bits = [LIST_IN_UI, 1 << 1, LIST_IN_TAB, 1 << 3, 1 << 5, AUTOMATION, 1 << 7];
    let table: Vec<Command> = (0..tc.draw(gs::integers::<usize>().max_value(10)))
        .map(|_| {
            let flags = flag_bits.iter().filter(|_| tc.draw(gs::booleans())).fold(0, |all, bit| all | bit);
            let arg = tc.draw(gs::booleans()).then(|| Arg {
                kind: KINDS[tc.draw(gs::integers::<usize>().max_value(KINDS.len() - 1))],
                required: tc.draw(gs::booleans()),
            });
            let name = names[tc.draw(gs::integers::<usize>().max_value(names.len() - 1))];
            let context = contexts[tc.draw(gs::integers::<usize>().max_value(contexts.len() - 1))];
            command(name, flags, context, arg)
        })
        .collect();
    let listing = listing(&table);
    assert_eq!(listing.schema, SCHEMA);
    let mut expected = Vec::new();
    for (index, c) in table.iter().enumerate() {
        let shadowed = table[..index].iter().any(|earlier| earlier.name == c.name);
        if shadowed || c.flags & (LIST_IN_UI | AUTOMATION) == 0 {
            continue;
        }
        let targets = match c.context.as_str() {
            "view" => Targets::View,
            "panel" => Targets::Panel,
            _ if c.flags & LIST_IN_TAB != 0 => Targets::Panel,
            "workspace" => Targets::Workspace,
            _ => Targets::None,
        };
        expected.push(json!({
            "id": format!("command/{}", c.name),
            "display_name": c.display_name,
            "description": c.description,
            "tags": ["a", "b"],
            "args": c.arg.iter().map(|arg| json!({"name": arg.kind.name(), "required": arg.required})).collect::<Vec<_>>(),
            "targets": serde_json::to_value(targets).unwrap(),
        }));
    }
    assert_eq!(serde_json::to_value(&listing).unwrap()["commands"], json!(expected));
}

// Every target an Automation ID names maps to the window, panel and view
// registers' record and back to the same ID.
#[hegel::test(test_cases = 300)]
fn targets_round_trip_through_their_automation_ids(tc: TestCase) {
    let id = WorkspaceId::from_bytes(std::array::from_fn(|_| tc.draw(gs::integers::<u8>())));
    let owner = if tc.draw(gs::booleans()) { ViewOwner::Workspace(id) } else { ViewOwner::Window(id) };
    let key = tc.draw(gs::sampled_from(vec!["u:1", "primary", "files", "a:b"]));
    let target = match tc.draw(gs::integers::<u8>().max_value(4)) {
        0 => Target::Workspace(id),
        1 => Target::Window(id),
        2 => Target::Panel(PanelAddress { owner, id: tc.draw(gs::sampled_from(vec!["1", "42", "main"])).into() }),
        _ => Target::View(ViewAddress { owner, key: key.into(), previous: tc.draw(gs::booleans()) }),
    };
    assert_eq!(Target::from_automation_id(&target.automation_id()), Some(target));
}

#[test]
fn malformed_targets_name_nothing() {
    for bad in
        ["", "workspace", "workspace/nope", &format!("dashboard/{W}"), &format!("workspace/{W}/panel/"), &format!("workspace/{W}/tab/1")]
    {
        assert_eq!(Target::from_automation_id(bad), None, "{bad}");
    }
}

fn body(value: Value) -> Invoke {
    serde_json::from_value(value).unwrap()
}

fn status(result: Result<(&Command, Option<Given>), Refusal>) -> Result<Option<Given>, StatusCode> {
    result.map(|(_, arg)| arg).map_err(|refusal| refusal.0)
}

// An unknown command is 404 and an unlisted one 403; a missing required
// argument, one the command doesn't take and a malformed one are 422.
#[test]
fn a_plan_checks_the_command_and_its_argument() {
    let table = table();
    let plan = |value| status(plan(&table, &body(value)));
    assert_eq!(plan(json!({"command": "command/nope"})), Err(StatusCode::NOT_FOUND));
    assert_eq!(plan(json!({"command": "split_panel"})), Err(StatusCode::NOT_FOUND));
    assert_eq!(plan(json!({"command": "command/set_current_path"})), Err(StatusCode::FORBIDDEN));
    assert_eq!(plan(json!({"command": "command/split_panel"})), Ok(None));
    assert_eq!(plan(json!({"command": "command/select_workspace"})), Ok(None));
    assert_eq!(plan(json!({"command": "command/open_url"})), Err(StatusCode::UNPROCESSABLE_ENTITY));
    assert_eq!(plan(json!({"command": "command/open_url", "args": {"string": 3}})), Err(StatusCode::UNPROCESSABLE_ENTITY));
    assert_eq!(plan(json!({"command": "command/open_url", "args": {"string": "x", "cfg": "y"}})), Err(StatusCode::UNPROCESSABLE_ENTITY));
    assert_eq!(plan(json!({"command": "command/split_panel", "args": {"string": "x"}})), Err(StatusCode::UNPROCESSABLE_ENTITY));
    assert_eq!(
        plan(json!({"command": "command/open_url", "args": {"string": "https://x"}})),
        Ok(Some((ArgKind::String, ArgValue::Text("https://x".into()))))
    );
    assert_eq!(
        plan(json!({"command": "command/goto_line", "args": {"cursor": {"line": 3}}})),
        Ok(Some((ArgKind::Cursor, ArgValue::Cursor { line: 3, column: 1 })))
    );
    assert_eq!(plan(json!({"command": "command/goto_line", "args": {"cursor": {"line": 0}}})), Err(StatusCode::UNPROCESSABLE_ENTITY));
    // An optional argument may be left out.
    assert_eq!(plan(json!({"command": "command/new_user"})), Ok(None));
    assert_eq!(
        plan(json!({"command": "command/goto_line", "args": {"cursor": {"line": u64::MAX}}})),
        Err(StatusCode::UNPROCESSABLE_ENTITY)
    );
    // A command another runs is held to what invoking it would be.
    let run = |name: &str| plan(json!({"command": "command/run_command", "args": {"command": name}}));
    assert_eq!(run("command/exit"), Ok(Some((ArgKind::Command, ArgValue::Text("exit".into())))));
    assert_eq!(run("command/set_current_path"), Err(StatusCode::FORBIDDEN));
    assert_eq!(run("command/nope"), Err(StatusCode::NOT_FOUND));
    assert_eq!(run("command/open_url"), Err(StatusCode::UNPROCESSABLE_ENTITY));
}

// A named target wins; without one, a command that acts on something takes
// the one View its Caller Context shows: 400 for none, 409 for several. One
// that acts on nothing runs without either.
#[test]
fn a_target_is_named_or_the_caller_contexts_one_view() {
    let view = format!("workspace/{W}/view/u:1");
    let other = format!("workspace/{W}/view/u:2");
    let named = Target::from_automation_id(&format!("workspace/{W}/panel/1"));
    let context = |views: &[&String]| Some(views.iter().map(|v| v.to_string()).collect());
    let status = |result: Result<Option<Target>, Refusal>| result.map_err(|refusal| refusal.0);
    assert_eq!(status(target(named.clone(), Targets::Panel, context(&[&view, &other]))), Ok(named));
    assert_eq!(status(target(None, Targets::Panel, context(&[&view]))), Ok(Target::from_automation_id(&view)));
    assert_eq!(status(target(None, Targets::View, context(&[]))), Err(StatusCode::BAD_REQUEST));
    assert_eq!(status(target(None, Targets::View, None)), Err(StatusCode::BAD_REQUEST));
    assert_eq!(status(target(None, Targets::Panel, context(&[&view, &other]))), Err(StatusCode::CONFLICT));
    assert_eq!(status(target(None, Targets::None, context(&[&view, &other]))), Ok(None));
    assert_eq!(status(target(None, Targets::None, None)), Ok(None));
    // What a target can stand for.
    let workspace = Target::Workspace(W.parse().unwrap());
    assert!(workspace.serves(Targets::None) && workspace.serves(Targets::Workspace));
    assert!(!workspace.serves(Targets::Panel) && !workspace.serves(Targets::View));
    let view = Target::from_automation_id(&view).unwrap();
    assert!(view.serves(Targets::Panel) && view.serves(Targets::View));
}

/// A UI that runs what it is given, as `(ticket, invocation)`, and refuses
/// a target it doesn't have.
#[derive(Default)]
struct Ui {
    invoked: Vec<(u64, Invocation)>,
    ran: Vec<u64>,
    refuse: Option<Refusal>,
}

impl Host for Ui {
    fn invoke(&mut self, ticket: u64, invocation: &Invocation) -> Result<(), Refusal> {
        if invocation.target.as_ref().is_some_and(|target| target.automation_id().ends_with("/panel/9")) {
            return Err(Refusal::new(StatusCode::UNPROCESSABLE_ENTITY, "no panel 9"));
        }
        self.invoked.push((ticket, invocation.clone()));
        Ok(())
    }

    fn settle(&mut self, ticket: u64) -> Result<(), Refusal> {
        self.ran.push(ticket);
        self.refuse.take().map_or(Ok(()), Err)
    }
}

/// An endpoint on a real socket whose UI ends a frame every 10ms, running
/// what is invoked, under the `mode` setting.
#[cfg(unix)]
struct Endpoint {
    path: std::path::PathBuf,
    stop: Arc<std::sync::atomic::AtomicBool>,
    frames: Option<std::thread::JoinHandle<()>>,
    ui: Arc<std::sync::Mutex<Ui>>,
    mode: Arc<AtomicU32>,
}

#[cfg(unix)]
impl Endpoint {
    fn start() -> Self {
        use std::os::unix::fs::DirBuilderExt;
        extern "C" fn wake() {}
        let dir = crate::socket_test_dir();
        std::fs::DirBuilder::new().mode(0o700).create(&dir).unwrap();
        let path = dir.join("socket");
        let listener = std::os::unix::net::UnixListener::bind(&path).unwrap();
        listener.set_nonblocking(true).unwrap();
        let automation = Arc::new(Automation::new(wake));
        automation.commands.set(table()).unwrap();
        let routes = crate::automation::router(automation.clone());
        std::thread::spawn(move || {
            let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
            runtime.block_on(async move { axum::serve(tokio::net::UnixListener::from_std(listener).unwrap(), routes).await.unwrap() });
        });
        let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let ui = Arc::new(std::sync::Mutex::new(Ui::default()));
        let mode = Arc::new(AtomicU32::new(AUTOMATION_FULL));
        let frames = {
            let (stop, ui, mode) = (stop.clone(), ui.clone(), mode.clone());
            std::thread::spawn(move || {
                let (mut generation, mut running) = (Generation::default(), Running::default());
                let mut tree = nodes();
                while !stop.load(SeqCst) {
                    // Each command that ran changes the tree.
                    let ran = ui.lock().unwrap().invoked.len();
                    tree[1].name = Some(format!("ran {ran}"));
                    let mode = mode.load(SeqCst);
                    frame_end(&automation, &mut generation, mode, running.any(), |screens| Some(build(tree.clone(), screens)));
                    running.frame_end(&automation, mode, generation.generation, &mut *ui.lock().unwrap());
                    std::thread::sleep(Duration::from_millis(10));
                }
            })
        };
        Endpoint { path, stop, frames: Some(frames), ui, mode }
    }

    fn request(&self, method: &str, target: &str, body: &str) -> (u16, String) {
        use std::io::{Read, Write};
        let mut stream = std::os::unix::net::UnixStream::connect(&self.path).unwrap();
        let length = body.len();
        write!(stream, "{method} {target} HTTP/1.0\r\nContent-Type: application/json\r\nContent-Length: {length}\r\n\r\n{body}").unwrap();
        stream.set_read_timeout(Some(Duration::from_secs(10))).unwrap();
        let mut response = String::new();
        stream.read_to_string(&mut response).unwrap();
        let (head, body) = response.split_once("\r\n\r\n").unwrap();
        (head[9..12].parse().unwrap(), body.to_owned())
    }

    /// Whether an event stream from `cursor` sends nothing at first.
    fn resumes_quietly(&self, cursor: &str) -> bool {
        use std::io::{BufRead, Write};
        let mut stream = std::os::unix::net::UnixStream::connect(&self.path).unwrap();
        write!(stream, "GET /v1/observed/events?since={cursor} HTTP/1.0\r\n\r\n").unwrap();
        stream.set_read_timeout(Some(Duration::from_millis(300))).unwrap();
        let mut reader = std::io::BufReader::new(stream);
        let mut line = String::new();
        while reader.read_line(&mut line).is_ok_and(|n| n > 0) && line != "\r\n" {
            line.clear();
        }
        line.clear();
        reader.read_line(&mut line).is_err()
    }

    fn invoke(&self, body: Value) -> (u16, String) {
        self.request("POST", "/v1/commands/invoke", &body.to_string())
    }
}

#[cfg(unix)]
impl Drop for Endpoint {
    fn drop(&mut self) {
        self.stop.store(true, SeqCst);
        if let Some(frames) = self.frames.take() {
            let _ = frames.join();
        }
        if let Some(dir) = self.path.parent() {
            let _ = std::fs::remove_dir_all(dir);
        }
    }
}

// An invocation runs in the frame after it is taken, and is answered with
// the cursor of the tree at that frame's end, which shows what it did; the
// UI's refusals and those of the setting reach the client.
#[cfg(unix)]
#[test]
fn an_invocation_answers_with_the_cursor_of_the_frame_that_ran_it() {
    let endpoint = Endpoint::start();
    let (status, listing) = endpoint.request("GET", "/v1/commands", "");
    assert_eq!(status, 200);
    let listing: Listing = serde_json::from_str(&listing).unwrap();
    let ids: Vec<&str> = listing.commands.iter().map(|c| c.id.as_str()).collect();
    assert!(ids.contains(&"command/select_workspace") && !ids.contains(&"command/set_current_path"), "{ids:?}");
    let panel = format!("workspace/{W}/panel/1");
    let (status, ran) = endpoint.invoke(json!({"command": "command/split_panel", "target": panel}));
    assert_eq!(status, 200, "{ran}");
    let cursor = serde_json::from_str::<Value>(&ran).unwrap()["cursor"].as_str().unwrap().to_owned();
    let (instance, generation) = cursor.split_once(':').unwrap();
    assert_eq!(instance, crate::address::instance_uuid().to_string());
    // The tree at that cursor shows that it ran: a stream resuming there
    // has nothing to send.
    let (_, semantic) = endpoint.request("GET", "/v1/observed/semantic", "");
    let semantic: Value = serde_json::from_str(&semantic).unwrap();
    assert_eq!((semantic["generation"].as_u64(), &semantic["nodes"][1]["name"]), (generation.parse().ok(), &json!("ran 1")));
    assert!(endpoint.resumes_quietly(&cursor), "the cursor came before what the command did");
    {
        let ui = endpoint.ui.lock().unwrap();
        let (ticket, invocation) = &ui.invoked[0];
        assert_eq!((invocation.command.as_str(), invocation.target.as_ref().map(Target::automation_id)), ("split_panel", Some(panel)));
        assert_eq!(ui.ran, [*ticket]);
    }
    // The Caller Context names the target: one View, or 409 for several.
    let (status, _) = endpoint.invoke(json!({"command": "command/close_tab", "session": "s1"}));
    assert_eq!(status, 409);
    let (status, _) = endpoint.invoke(json!({"command": "command/close_tab", "session": "nobody"}));
    assert_eq!(status, 400);
    // The UI's refusals, before running and after.
    let (status, reason) = endpoint.invoke(json!({"command": "command/split_panel", "target": format!("workspace/{W}/panel/9")}));
    assert_eq!((status, reason.as_str()), (422, "no panel 9"));
    endpoint.ui.lock().unwrap().refuse = Some(Refusal::new(StatusCode::UNPROCESSABLE_ENTITY, "refused"));
    assert_eq!(endpoint.invoke(json!({"command": "command/exit"})), (422, "refused".into()));
    // A target of the wrong kind, malformed JSON and an unknown field.
    assert_eq!(endpoint.invoke(json!({"command": "command/close_tab", "target": format!("workspace/{W}")})).0, 422);
    assert_eq!(endpoint.request("POST", "/v1/commands/invoke", "{").0, 400);
    assert_eq!(endpoint.invoke(json!({"command": "command/exit", "targets": "x"})).0, 422);
    // observe answers 403, with why; off 404, as the reads do.
    endpoint.mode.store(crate::AUTOMATION_OBSERVE, SeqCst);
    std::thread::sleep(Duration::from_millis(50));
    let (status, reason) = endpoint.invoke(json!({"command": "command/exit"}));
    assert_eq!(status, 403);
    assert!(reason.contains("observe"), "{reason}");
    assert_eq!(endpoint.request("GET", "/v1/commands", "").0, 403);
    endpoint.mode.store(AUTOMATION_OFF, SeqCst);
    std::thread::sleep(Duration::from_millis(50));
    assert_eq!(endpoint.invoke(json!({"command": "command/exit"})).0, 404);
    assert_eq!(endpoint.request("GET", "/v1/commands", "").0, 404);
    assert_eq!(endpoint.ui.lock().unwrap().invoked.len(), 2);
}
