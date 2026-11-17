//! Task stop, task output and root search against a stand-in producer on a
//! Unix socket, with synthetic data only (the repo is public).
#![cfg(unix)]

use std::io::Write;
use std::path::PathBuf;
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use axum::extract::{Path, Query, State};
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use conversation_events::{FileKind, FileMatch, TaskOutput};
use serde::Deserialize;
use tokio::net::UnixListener;
use wheelhouse_agent::{Endpoint, Output, OutputRead, PostStatus, Route, Search, SEARCH_DEBOUNCE, TAIL_EVERY};

#[derive(Default)]
struct Producer {
    stopped: Mutex<Vec<(String, String)>>,
    output: Mutex<String>,
    queries: Mutex<Vec<String>>,
}

async fn stop(State(p): State<Arc<Producer>>, Path((thread, task)): Path<(String, String)>) -> Response {
    if task != "k1" {
        return (StatusCode::NOT_FOUND, "no task").into_response();
    }
    p.stopped.lock().unwrap().push((thread, task));
    Json(serde_json::json!({ "stopped": true })).into_response()
}

async fn output(State(p): State<Arc<Producer>>, Path((_, task)): Path<(String, String)>) -> Response {
    if task != "k1" {
        return (StatusCode::NOT_FOUND, "task k2 serves no output").into_response();
    }
    ([("content-type", "text/x-ansi")], p.output.lock().unwrap().clone()).into_response()
}

#[derive(Deserialize)]
struct Asked {
    q: String,
    limit: usize,
}

async fn files(State(p): State<Arc<Producer>>, Query(asked): Query<Asked>) -> Json<Vec<FileMatch>> {
    p.queries.lock().unwrap().push(asked.q.clone());
    // A query this slow is overtaken by the next.
    if asked.q == "slow" {
        tokio::time::sleep(Duration::from_millis(400)).await;
    }
    let matches = [
        FileMatch { path: format!("src/{}", asked.q), kind: FileKind::Dir },
        FileMatch { path: format!("src/{}.rs", asked.q), kind: FileKind::File },
    ];
    Json(matches.into_iter().take(asked.limit).collect())
}

fn serve(name: &str, producer: Arc<Producer>) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("wh-tasks-{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let socket = dir.join("producer.sock");
    let app = Router::new()
        .route("/threads/{id}/tasks/{task}/stop", post(stop))
        .route("/threads/{id}/tasks/{task}/output", get(output))
        .route("/threads/{id}/files", get(files))
        .with_state(producer);
    let (ready, listening) = mpsc::channel();
    let path = socket.clone();
    thread::spawn(move || {
        let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        runtime.block_on(async move {
            let listener = UnixListener::bind(&path).unwrap();
            ready.send(()).unwrap();
            axum::serve(listener, app).await.unwrap();
        });
    });
    listening.recv().unwrap();
    socket
}

fn until<T>(what: &str, mut f: impl FnMut() -> Option<T>) -> T {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(value) = f() {
            return value;
        }
        assert!(Instant::now() < deadline, "no {what}");
        thread::sleep(Duration::from_millis(5));
    }
}

fn settled(post: &wheelhouse_agent::Post) -> PostStatus {
    until("answer", || Some(post.status()).filter(|s| *s != PostStatus::Sending))
}

/// Everything an output read until `done` holds of it, as one read.
fn read_until(output: &Output, seen: &mut OutputRead, done: impl Fn(&OutputRead) -> bool) {
    until("output", || {
        let read = output.take();
        if read.reset {
            *seen = OutputRead { reset: true, ..OutputRead::default() };
        }
        seen.text.push_str(&read.text);
        seen.end = read.end;
        done(seen).then_some(())
    })
}

fn no_wake() -> wheelhouse_agent::Wake {
    Arc::new(|| {})
}

// Stopping a task posts to its own route, and the producer's refusal of a
// task it doesn't have comes back as the post's failure.
#[test]
fn a_task_stops_through_its_producer() {
    let producer = Arc::new(Producer::default());
    let endpoint = Endpoint::open(&Route::Local(serve("stop", producer.clone()))).unwrap();
    assert_eq!(settled(&endpoint.stop_task("t/1", "k1")), PostStatus::Accepted);
    assert_eq!(*producer.stopped.lock().unwrap(), [("t/1".to_owned(), "k1".to_owned())], "the ids travel whole");
    assert_eq!(settled(&endpoint.stop_task("t/1", "k2")), PostStatus::Failed("no task".into()));
    // A URL would drop these segments and stop something else.
    for id in ["..", ".", ""] {
        assert!(matches!(settled(&endpoint.stop_task("t1", id)), PostStatus::Failed(_)), "{id:?}");
    }
    assert_eq!(producer.stopped.lock().unwrap().len(), 1);
}

// A task's output from its url follows what the producer appends while it
// runs, starts over when the producer's copy shrinks, and ends once the
// task has and the rest is read. One the producer refuses ends failed.
#[test]
fn output_from_a_url_follows_the_task_until_it_ends() {
    let producer = Arc::new(Producer::default());
    let endpoint = Endpoint::open(&Route::Local(serve("url", producer.clone()))).unwrap();
    *producer.output.lock().unwrap() = "one\n".into();
    let at = |task: &str| TaskOutput { url: Some(format!("/threads/t1/tasks/{task}/output")), ..TaskOutput::default() };
    let output = Output::open(Some(&endpoint), &at("k1"), true, no_wake()).unwrap();
    let mut seen = OutputRead::default();
    read_until(&output, &mut seen, |r| r.text == "one\n");
    producer.output.lock().unwrap().push_str("two\n");
    read_until(&output, &mut seen, |r| r.text == "one\ntwo\n");
    *producer.output.lock().unwrap() = "trimmed".into();
    read_until(&output, &mut seen, |r| r.reset && r.text == "trimmed");
    assert_eq!(seen.end, None, "still running");
    producer.output.lock().unwrap().push_str(" and done");
    output.finish();
    read_until(&output, &mut seen, |r| r.end.is_some());
    assert_eq!((seen.text.as_str(), seen.end.clone()), ("trimmed and done", Some(Ok(()))));
    assert_eq!(output.take().end, Some(Ok(())), "an end is reported to every later look");

    // A 404 may mean nothing is served yet while the task runs; once it
    // has ended, it is the answer.
    let refused = Output::open(Some(&endpoint), &at("k2"), true, no_wake()).unwrap();
    thread::sleep(TAIL_EVERY * 3);
    assert_eq!(refused.take().end, None, "still waiting");
    refused.finish();
    let mut seen = OutputRead::default();
    read_until(&refused, &mut seen, |r| r.end.is_some());
    assert_eq!(seen.end, Some(Err("task k2 serves no output".into())));
    // A url the producer names never leaves its endpoint.
    for elsewhere in ["http://elsewhere.test/x", "//elsewhere.test/x"] {
        let away = TaskOutput { url: Some(elsewhere.into()), ..TaskOutput::default() };
        assert!(Output::open(Some(&endpoint), &away, true, no_wake()).is_err(), "{elsewhere}");
        assert!(endpoint.search(elsewhere, no_wake()).is_err(), "{elsewhere}");
    }
    assert!(Output::open(None, &at("k1"), true, no_wake()).is_err(), "a url needs the endpoint");
}

// A local file is read directly, not through the producer: tailed while the
// task runs, a character split across writes arriving whole, a truncation
// starting over, and a file that never came ending failed.
#[test]
fn local_output_tails_its_file_directly() {
    let dir = std::env::temp_dir().join(format!("wh-tail-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("task.log");
    let local = TaskOutput { path: Some(path.to_str().unwrap().into()), local: true, url: Some("/unused".into()), media_type: None };
    let output = Output::open(None, &local, true, no_wake()).unwrap();
    let mut file = std::fs::File::create(&path).unwrap();
    file.write_all(b"caf\xc3").unwrap();
    let mut seen = OutputRead::default();
    read_until(&output, &mut seen, |r| r.text == "caf");
    file.write_all(b"\xa9 ok").unwrap();
    read_until(&output, &mut seen, |r| r.text == "café ok");
    drop(file);
    std::fs::write(&path, "new").unwrap();
    read_until(&output, &mut seen, |r| r.reset && r.text == "new");
    output.finish();
    read_until(&output, &mut seen, |r| r.end.is_some());
    assert_eq!(seen.end, Some(Ok(())));

    let missing = TaskOutput { path: Some(dir.join("never.log").to_str().unwrap().into()), local: true, ..TaskOutput::default() };
    let ended = Output::open(None, &missing, false, no_wake()).unwrap();
    let mut seen = OutputRead::default();
    read_until(&ended, &mut seen, |r| r.end.is_some());
    assert!(matches!(seen.end, Some(Err(_))));
    // Only a regular file is read: a directory, as a FIFO or a device, fails
    // rather than blocking.
    let folder = TaskOutput { path: Some(dir.to_str().unwrap().into()), local: true, ..TaskOutput::default() };
    let not_a_file = Output::open(None, &folder, true, no_wake()).unwrap();
    let mut seen = OutputRead::default();
    read_until(&not_a_file, &mut seen, |r| r.end.is_some());
    assert!(matches!(seen.end, Some(Err(_))));
    let elsewhere = TaskOutput { path: Some("/far/away.log".into()), ..TaskOutput::default() };
    assert!(Output::open(None, &elsewhere, true, no_wake()).is_err(), "a path on another machine, with no url");
    let _ = std::fs::remove_dir_all(&dir);
}

fn answer(search: &Search) -> wheelhouse_agent::SearchAnswer {
    until("an answer", || search.take())
}

// Typing asks once the query stands for the debounce: the producer sees
// only the last of a burst, with its limit. A slow answer overtaken by a
// later query is dropped, and only the latest is ever answered.
#[test]
fn search_debounces_and_the_latest_query_wins() {
    let producer = Arc::new(Producer::default());
    let endpoint = Endpoint::open(&Route::Local(serve("search", producer.clone()))).unwrap();
    let search = endpoint.search("/threads/t1/files", no_wake()).unwrap();
    for query in ["m", "ma", "mai"] {
        search.query(query, 1);
    }
    let first = answer(&search);
    assert_eq!(first.query, "mai");
    assert_eq!(first.matches, Ok(vec![FileMatch { path: "src/mai".into(), kind: FileKind::Dir }]));
    assert_eq!(*producer.queries.lock().unwrap(), ["mai"]);

    search.query("slow", 10);
    until("the slow query asked", || producer.queries.lock().unwrap().contains(&"slow".to_owned()).then_some(()));
    search.query("fast", 10);
    assert_eq!(answer(&search).query, "fast");
    thread::sleep(Duration::from_millis(500) + SEARCH_DEBOUNCE);
    assert_eq!(search.take(), None, "the slow answer was dropped");
}
