//! Wheelhouse's HTTP ingress. UI state stays on the C host's main thread.
use std::ffi::c_void;

#[cfg(any(unix, windows))]
mod connector;
#[cfg(unix)]
mod quit;
#[cfg(any(unix, windows))]
mod recording;

pub type Wake = extern "C" fn();
#[repr(C)]
pub struct Text {
    pub data: *const u8,
    pub len: usize,
}
#[repr(C)]
pub struct Workdir {
    pub workspace_id: u64,
    pub view_id: u64,
    pub entity_kind: Text,
    pub entity_id: Text,
    pub cwd: Text,
    pub live_cwd: Text,
}
pub type Emit = unsafe extern "C" fn(*mut c_void, *const Workdir);
pub type Observe = unsafe extern "C" fn(*mut c_void, Emit, *mut c_void) -> u32;
pub type Apply = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> u32;
/// Drop every fact the endpoint's provider published, before a snapshot
/// replaces them: 1 when done, 2 when some window kept its facts (the request
/// is unavailable), 0 when the endpoint has no provider of its own (a snapshot
/// there is refused).
pub type Retract = unsafe extern "C" fn(*mut c_void) -> u32;

#[cfg(any(unix, windows))]
mod transport {
    use super::*;
    use axum::{
        body::Bytes,
        extract::{DefaultBodyLimit, State},
        http::{HeaderMap, StatusCode},
        response::{IntoResponse, Response},
        routing::{get, post},
        Router,
    };
    use std::{path::PathBuf, sync::mpsc, thread, time::Duration};
    use tokio::sync::oneshot;

    pub enum Pending {
        /// One request's patches, applied in one drain so no frame shows part
        /// of them; `replace` retracts the provider's facts first.
        Patch {
            bodies: Vec<Bytes>,
            replace: bool,
            reply: oneshot::Sender<u32>,
        },
        Workdirs {
            reply: oneshot::Sender<Option<std::sync::Arc<serde_json::Value>>>,
        },
    }
    pub struct Ingress {
        pub queue: mpsc::Receiver<Pending>,
        shutdown: Option<oneshot::Sender<()>>,
        thread: Option<thread::JoinHandle<()>>,
    }
    #[derive(Clone)]
    struct Shared {
        queue: mpsc::SyncSender<Pending>,
        wake: Wake,
    }

    // Request outcomes from the UI thread's drain.
    pub const APPLIED: u32 = 1;
    pub const REJECTED: u32 = 0;
    pub const UNAVAILABLE: u32 = 2;
    pub const UNSUPPORTED: u32 = 3;

    async fn patch(State(state): State<Shared>, headers: HeaderMap, body: Bytes) -> StatusCode {
        catalog(state, headers, body, false).await
    }

    async fn snapshot(State(state): State<Shared>, headers: HeaderMap, body: Bytes) -> StatusCode {
        catalog(state, headers, body, true).await
    }

    // A replacing snapshot carries the provider's whole catalog. Select its
    // larger bound before recording so capture and delivery enforce the same
    // limit; ordinary patches and the other routes retain their 1 MiB bound.
    async fn limit_body(mut request: axum::extract::Request, next: axum::middleware::Next) -> Response {
        let limit = if request.uri().path() == "/v1/metadata/snapshot" { 16 * 1024 * 1024 } else { 1024 * 1024 };
        DefaultBodyLimit::max(limit).apply(&mut request);
        next.run(request).await
    }

    // A metadata patch, or an array of them applied together. A snapshot is
    // an array that replaces everything the endpoint's provider published.
    async fn catalog(state: Shared, headers: HeaderMap, body: Bytes, replace: bool) -> StatusCode {
        if headers.get("content-type").and_then(|v| v.to_str().ok()).and_then(|v| v.split(';').next()).map(str::trim)
            != Some("application/json")
        {
            return StatusCode::UNSUPPORTED_MEDIA_TYPE;
        }
        let Ok(value) = serde_json::from_slice::<serde_json::Value>(&body) else {
            return StatusCode::BAD_REQUEST;
        };
        let is_patch = |v: &serde_json::Value| v.get("type").and_then(|t| t.as_str()) == Some("metadata-patch");
        let patches = match value {
            serde_json::Value::Array(items) if items.iter().all(is_patch) => items,
            single if !replace && is_patch(&single) => vec![single],
            _ => return StatusCode::BAD_REQUEST,
        };
        // A single patch keeps its original bytes.
        let bodies = if patches.len() == 1 && !replace && body.first() == Some(&b'{') {
            vec![body]
        } else {
            patches.iter().map(|patch| Bytes::from(serde_json::to_vec(patch).expect("JSON value serializes"))).collect()
        };
        let (reply, response) = oneshot::channel();
        if state.queue.try_send(Pending::Patch { bodies, replace, reply }).is_err() {
            return StatusCode::SERVICE_UNAVAILABLE;
        }
        (state.wake)();
        match tokio::time::timeout(Duration::from_secs(5), response).await {
            Ok(Ok(APPLIED)) => StatusCode::NO_CONTENT,
            Ok(Ok(REJECTED)) => StatusCode::UNPROCESSABLE_ENTITY,
            Ok(Ok(UNSUPPORTED)) => StatusCode::NOT_FOUND,
            _ => StatusCode::SERVICE_UNAVAILABLE,
        }
    }

    async fn workdirs(State(state): State<Shared>) -> Response {
        let (reply, response) = oneshot::channel();
        if state.queue.try_send(Pending::Workdirs { reply }).is_err() {
            return StatusCode::SERVICE_UNAVAILABLE.into_response();
        }
        (state.wake)();
        match tokio::time::timeout(Duration::from_secs(5), response).await {
            Ok(Ok(Some(value))) => {
                ([("cache-control", "no-store"), ("content-type", "application/json")], value.to_string()).into_response()
            }
            _ => StatusCode::SERVICE_UNAVAILABLE.into_response(),
        }
    }

    #[cfg(unix)]
    use std::os::unix::fs::{MetadataExt, PermissionsExt};

    #[cfg(windows)]
    mod windows;

    #[cfg(unix)]
    struct SocketGuard {
        path: PathBuf,
        dev: u64,
        ino: u64,
    }
    #[cfg(unix)]
    impl Drop for SocketGuard {
        fn drop(&mut self) {
            if std::fs::symlink_metadata(&self.path).is_ok_and(|m| m.dev() == self.dev && m.ino() == self.ino) {
                let _ = std::fs::remove_file(&self.path);
            }
        }
    }
    impl Ingress {
        pub fn start(path: PathBuf, wake: Wake, recording: Option<(PathBuf, u64, usize)>) -> Result<Self, String> {
            #[cfg(unix)]
            let (listener, guard) = {
                let parent = path.parent().ok_or("socket needs a private parent directory")?;
                let meta = std::fs::metadata(parent).map_err(|e| e.to_string())?;
                if !meta.is_dir() || meta.mode() & 0o077 != 0 || meta.uid() != unsafe { libc::geteuid() } {
                    return Err("socket parent must be owned by this user and private (mode 0700)".into());
                }
                let listener = std::os::unix::net::UnixListener::bind(&path).map_err(|e| e.to_string())?;
                let meta = std::fs::symlink_metadata(&path).map_err(|e| e.to_string())?;
                let guard = SocketGuard { path: path.clone(), dev: meta.dev(), ino: meta.ino() };
                std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).map_err(|e| e.to_string())?;
                listener.set_nonblocking(true).map_err(|e| e.to_string())?;
                (listener, guard)
            };
            let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().map_err(|e| e.to_string())?;
            #[cfg(windows)]
            let listener = {
                let _entered = runtime.enter();
                windows::Listener::bind(&path).map_err(|e| e.to_string())?
            };
            // Reads and writes deliberately share one bounded admission queue:
            // a burst of either may make the other receive 503 and retry. This
            // preserves request ordering and bounds total pending UI work.
            let (queue, receiver) = mpsc::sync_channel(64);
            let (shutdown, stop) = oneshot::channel();
            let thread = thread::Builder::new()
                .name("wheelhouse-ingress".into())
                .spawn(move || {
                    #[cfg(unix)]
                    let _guard = guard;
                    runtime.block_on(async move {
                        #[cfg(unix)]
                        let listener = tokio::net::UnixListener::from_std(listener).expect("registered Unix listener");
                        let mut app = Router::new()
                            .route("/v1/health", get(|| async { StatusCode::NO_CONTENT }))
                            .route("/v1/metadata/patch", post(patch))
                            .route("/v1/metadata/snapshot", post(snapshot))
                            .route("/v1/observed/workdirs", get(workdirs))
                            .with_state(Shared { queue, wake });
                        if let Some((path, bytes, files)) = recording {
                            let recorder = crate::recording::Recorder::start(path, bytes, files);
                            app = app.layer(axum::middleware::from_fn_with_state(recorder, crate::recording::capture));
                        }
                        let app = app.layer(axum::middleware::from_fn(limit_body));
                        let server = axum::serve(listener, app);
                        let ticks = async {
                            loop {
                                tokio::time::sleep(Duration::from_millis(250)).await;
                                wake();
                            }
                        };
                        tokio::select! { _ = server => {}, _ = stop => {}, _ = ticks => {} }
                    });
                })
                .map_err(|e| e.to_string())?;
            Ok(Self { queue: receiver, shutdown: Some(shutdown), thread: Some(thread) })
        }
    }
    impl Drop for Ingress {
        fn drop(&mut self) {
            if let Some(stop) = self.shutdown.take() {
                let _ = stop.send(());
            }
            if let Some(thread) = self.thread.take() {
                let _ = thread.join();
            }
        }
    }
    #[cfg(test)]
    mod tests {
        use super::*;
        #[tokio::test]
        async fn a_patch_reaches_the_host_as_sent_before_http_acknowledgement() {
            extern "C" fn wake() {}
            let request: &'static [u8] = br#"{ "type":"metadata-patch", "source_id":"provider", "target":{"kind":"entity","value":{"kind":"vessel","id":"a"}}, "set":{"label":{"value":{"type":"text","value":"A"}}}, "unset":["removed"] }"#;
            let (queue, receiver) = mpsc::sync_channel(1);
            let host = thread::spawn(move || {
                let Pending::Patch { bodies, replace, reply } = receiver.recv().unwrap() else { panic!("expected patch") };
                assert!(!replace);
                assert_eq!(bodies, [Bytes::from_static(request)]);
                reply.send(1).unwrap();
            });
            let mut headers = HeaderMap::new();
            headers.insert("content-type", "application/json".parse().unwrap());
            let response = patch(State(Shared { queue, wake }), headers, Bytes::from_static(request)).await;
            assert_eq!(response, StatusCode::NO_CONTENT);
            host.join().unwrap();
        }

        // What a request's handler queues, with its HTTP status once the
        // host replies `outcome`.
        async fn queued(body: &'static [u8], replace: bool, outcome: u32) -> (Option<(Vec<Bytes>, bool)>, StatusCode) {
            extern "C" fn wake() {}
            let (queue, receiver) = mpsc::sync_channel(1);
            let host = thread::spawn(move || {
                let Ok(Pending::Patch { bodies, replace, reply }) = receiver.recv() else {
                    return None;
                };
                reply.send(outcome).unwrap();
                Some((bodies, replace))
            });
            let mut headers = HeaderMap::new();
            headers.insert("content-type", "application/json".parse().unwrap());
            let status = catalog(Shared { queue, wake }, headers, Bytes::from_static(body), replace).await;
            (host.join().unwrap(), status)
        }
        #[tokio::test]
        async fn an_array_is_one_request_and_a_snapshot_must_be_one() {
            let a = r#"{"type":"metadata-patch","target":{"kind":"entity","value":{"kind":"vessel","id":"a"}},"set":{},"unset":[]}"#;
            let array = format!("[{a},{a}]");
            let array: &'static [u8] = Box::leak(array.into_bytes().into_boxed_slice());
            let (request, status) = queued(array, false, APPLIED).await;
            assert_eq!(status, StatusCode::NO_CONTENT);
            let (bodies, replace) = request.unwrap();
            assert_eq!((bodies.len(), replace), (2, false));
            assert_eq!(serde_json::from_slice::<serde_json::Value>(&bodies[1]).unwrap()["target"]["value"]["id"], "a");
            let (request, status) = queued(array, true, APPLIED).await;
            assert_eq!((status, request.map(|(b, r)| (b.len(), r))), (StatusCode::NO_CONTENT, Some((2, true))));
            // A snapshot of nothing retracts everything.
            let (request, status) = queued(b"[]", true, APPLIED).await;
            assert_eq!((status, request.map(|(b, _)| b.len())), (StatusCode::NO_CONTENT, Some(0)));
            assert_eq!(queued(array, true, UNSUPPORTED).await.1, StatusCode::NOT_FOUND);
            assert_eq!(queued(array, false, REJECTED).await.1, StatusCode::UNPROCESSABLE_ENTITY);
            // Every element must be a patch, and a snapshot must be an array.
            let mixed: &'static [u8] = Box::leak(format!("[{a},{{\"type\":\"other\"}}]").into_bytes().into_boxed_slice());
            assert_eq!(queued(mixed, false, APPLIED).await, (None, StatusCode::BAD_REQUEST));
            let single: &'static [u8] = Box::leak(a.as_bytes().to_vec().into_boxed_slice());
            assert_eq!(queued(single, true, APPLIED).await, (None, StatusCode::BAD_REQUEST));
        }
        #[test]
        fn a_request_applies_in_one_drain_and_a_snapshot_retracts_first() {
            #[derive(Default)]
            struct Calls(Vec<&'static str>, u32);
            unsafe extern "C" fn record_apply(context: *mut c_void, _: *const u8, _: usize) -> u32 {
                (*(context as *mut Calls)).0.push("apply");
                APPLIED
            }
            unsafe extern "C" fn record_retract(context: *mut c_void) -> u32 {
                let calls = &mut *(context as *mut Calls);
                calls.0.push("retract");
                calls.1
            }
            for (replace, retract, retracted, outcome, calls) in [
                (false, None, 0, APPLIED, &["apply", "apply"][..]),
                (true, Some(record_retract as Retract), 1, APPLIED, &["retract", "apply", "apply"][..]),
                (true, Some(record_retract as Retract), 0, UNSUPPORTED, &["retract"][..]),
                (true, Some(record_retract as Retract), UNAVAILABLE, UNAVAILABLE, &["retract", "apply", "apply"][..]),
                (true, None, 1, UNSUPPORTED, &[][..]),
            ] {
                let (queue, receiver) = mpsc::sync_channel(4);
                let mut server = Ingress { queue: receiver, shutdown: None, thread: None };
                let (reply, ack) = oneshot::channel();
                queue.send(Pending::Patch { bodies: vec![Bytes::new(), Bytes::new()], replace, reply }).unwrap();
                let mut host = Calls(Vec::new(), retracted);
                unsafe { wheelhouse_ingress_poll_observed(&mut server, record_apply, retract, None, &mut host as *mut _ as *mut c_void) };
                assert_eq!(ack.blocking_recv().unwrap(), outcome);
                assert_eq!(host.0, calls);
            }
        }
        #[derive(Default)]
        struct Host {
            reads: u32,
            generation: u64,
            fail: bool,
        }
        unsafe extern "C" fn apply(context: *mut c_void, _: *const u8, _: usize) -> u32 {
            (*(context as *mut Host)).generation += 1;
            1
        }
        unsafe extern "C" fn observe(context: *mut c_void, emit: Emit, output: *mut c_void) -> u32 {
            let host = &mut *(context as *mut Host);
            host.reads += 1;
            if host.fail {
                return 0;
            }
            let empty = || Text { data: std::ptr::null(), len: 0 };
            emit(
                output,
                &Workdir {
                    workspace_id: host.generation,
                    view_id: 9,
                    entity_kind: empty(),
                    entity_id: empty(),
                    live_cwd: empty(),
                    cwd: Text { data: b"/repo".as_ptr(), len: 5 },
                },
            );
            1
        }
        fn read(queue: &mpsc::SyncSender<Pending>) -> oneshot::Receiver<Option<std::sync::Arc<serde_json::Value>>> {
            let (reply, response) = oneshot::channel();
            assert!(queue.send(Pending::Workdirs { reply }).is_ok());
            response
        }
        #[test]
        fn failed_observation_is_shared_only_within_the_drain() {
            let (queue, receiver) = mpsc::sync_channel(64);
            let mut server = Ingress { queue: receiver, shutdown: None, thread: None };
            let mut host = Host { fail: true, ..Host::default() };
            let reads: Vec<_> = (0..4).map(|_| read(&queue)).collect();
            unsafe {
                wheelhouse_ingress_poll_observed(&mut server, apply, None, Some(observe), &mut host as *mut _ as *mut c_void);
            }
            assert_eq!(host.reads, 1);
            for response in reads {
                assert!(response.blocking_recv().unwrap().is_none());
            }
            host.fail = false;
            let response = read(&queue);
            unsafe {
                wheelhouse_ingress_poll_observed(&mut server, apply, None, Some(observe), &mut host as *mut _ as *mut c_void);
            }
            assert!(response.blocking_recv().unwrap().is_some());
            assert_eq!(host.reads, 2);
        }
        #[test]
        fn burst_reads_share_snapshot_but_patches_invalidate_it() {
            let (queue, receiver) = mpsc::sync_channel(64);
            let mut server = Ingress { queue: receiver, shutdown: None, thread: None };
            let mut host = Host::default();
            let mut reads = Vec::new();
            drop(read(&queue)); // Cancelled reads do not call the observer.
            for _ in 0..4 {
                reads.push(read(&queue));
            }
            let (reply, ack) = oneshot::channel();
            assert!(queue.send(Pending::Patch { bodies: vec![Bytes::new()], replace: false, reply }).is_ok());
            for _ in 0..4 {
                reads.push(read(&queue));
            }
            unsafe {
                wheelhouse_ingress_poll_observed(&mut server, apply, None, Some(observe), &mut host as *mut _ as *mut c_void);
            }
            assert_eq!(host.reads, 2);
            assert_eq!(ack.blocking_recv().unwrap(), 1);
            for (index, response) in reads.into_iter().enumerate() {
                let value = response.blocking_recv().unwrap().unwrap();
                assert_eq!(value["workdirs"][0]["workspace_id"], if index < 4 { 0 } else { 1 });
            }
            let response = read(&queue);
            unsafe {
                wheelhouse_ingress_poll_observed(&mut server, apply, None, Some(observe), &mut host as *mut _ as *mut c_void);
            }
            assert!(response.blocking_recv().unwrap().is_some());
            assert_eq!(host.reads, 3); // The cache never survives a UI drain.
        }
    }
}

#[cfg(any(unix, windows))]
pub use transport::Ingress;
#[cfg(not(any(unix, windows)))]
pub struct Ingress;

/// Start a local HTTP listener (Unix socket or Windows named pipe).
/// Error is NUL-terminated on failure.
///
/// # Safety
/// A null or empty path is rejected. Other input/output pointers must be valid
/// for their lengths; `error` may be null only when capacity is zero. `wake` must be safe
/// on a background thread and remain valid until stop returns.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_ingress_start(
    path: *const u8,
    len: usize,
    wake: Wake,
    error: *mut u8,
    capacity: usize,
) -> *mut Ingress {
    wheelhouse_ingress_start_recorded(path, len, wake, std::ptr::null(), 0, 0, 0, error, capacity)
}

/// Start with optional UTF-8 recording file and retention limits; NULL disables recording.
/// # Safety
/// Same requirements as start; recording_path must be valid for recording_len bytes.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_ingress_start_recorded(
    path: *const u8,
    len: usize,
    wake: Wake,
    recording_path: *const u8,
    recording_len: usize,
    bytes: u64,
    files: usize,
    error: *mut u8,
    capacity: usize,
) -> *mut Ingress {
    #[cfg(any(unix, windows))]
    let result = if path.is_null() || len == 0 {
        Err("socket path must not be null or empty".into())
    } else {
        std::str::from_utf8(std::slice::from_raw_parts(path, len)).map_err(|e| e.to_string()).and_then(|p| {
            let recording = if recording_path.is_null() {
                None
            } else {
                let path = std::str::from_utf8(std::slice::from_raw_parts(recording_path, recording_len)).map_err(|e| e.to_string())?;
                Some((path.into(), bytes, files))
            };
            Ingress::start(p.into(), wake, recording)
        })
    };
    #[cfg(not(any(unix, windows)))]
    let result: Result<Ingress, String> = {
        let _ = (path, len, wake);
        Err("Local HTTP ingress is supported on Unix and Windows hosts".into())
    };
    boxed_or_error(result, error, capacity)
}

/// The handle for `result`, or NULL with its message NUL-terminated in `error`.
unsafe fn boxed_or_error<T>(result: Result<T, String>, error: *mut u8, capacity: usize) -> *mut T {
    match result {
        Ok(value) => Box::into_raw(Box::new(value)),
        Err(message) => {
            if capacity > 0 {
                let n = message.len().min(capacity - 1);
                std::ptr::copy_nonoverlapping(message.as_ptr(), error, n);
                *error.add(n) = 0;
            }
            std::ptr::null_mut()
        }
    }
}

#[cfg(any(unix, windows))]
pub use connector::Connector;
#[cfg(not(any(unix, windows)))]
pub struct Connector;

/// Start supervising a connector: `argv` (program first), in Wheelhouse's
/// environment changed by `env` entries ("NAME=value" sets NAME, "NAME"
/// removes it), its output appended to `log`. It restarts when it exits; see
/// connector.rs. `flags` is [`CONNECTOR_READY_LINE`] or 0.
///
/// # Safety
/// Text pointers must be valid UTF-8 for their lengths (NULL only for length
/// zero) and arrays valid for their counts; `error` as for start. `wake` must
/// be safe on a background thread and remain valid until stop returns.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_connector_start(
    argv: *const Text,
    argc: usize,
    env: *const Text,
    env_count: usize,
    log: Text,
    flags: u32,
    wake: Wake,
    error: *mut u8,
    capacity: usize,
) -> *mut Connector {
    unsafe fn text(t: &Text) -> Result<String, String> {
        if t.len == 0 {
            return Ok(String::new());
        }
        std::str::from_utf8(std::slice::from_raw_parts(t.data, t.len)).map(str::to_owned).map_err(|e| e.to_string())
    }
    unsafe fn list(items: *const Text, count: usize) -> Result<Vec<String>, String> {
        if count == 0 {
            return Ok(Vec::new());
        }
        std::slice::from_raw_parts(items, count).iter().map(|t| text(t)).collect()
    }
    #[cfg(any(unix, windows))]
    let result = (|| {
        let argv = list(argv, argc)?;
        let env = list(env, env_count)?
            .into_iter()
            .map(|entry| match entry.split_once('=') {
                Some((name, value)) if !name.is_empty() => Ok((name.to_owned(), Some(value.to_owned()))),
                None if !entry.is_empty() => Ok((entry, None)),
                _ => Err(format!("environment entry must be NAME=value or NAME: {entry}")),
            })
            .collect::<Result<Vec<_>, _>>()?;
        let log = text(&log)?;
        if log.is_empty() {
            return Err("connector log path must not be empty".to_owned());
        }
        Connector::start(connector::Spec { argv, env, log: log.into(), ready_line: flags & CONNECTOR_READY_LINE != 0 }, wake)
    })();
    #[cfg(not(any(unix, windows)))]
    let result: Result<Connector, String> = {
        let _ = (argv, argc, env, env_count, log, flags, wake, text, list);
        Err("Connectors are supported on Unix and Windows hosts".into())
    };
    boxed_or_error(result, error, capacity)
}

/// Whether the connector's command runs now, and (optional) how many times
/// it has started.
///
/// # Safety
/// `connector` must be NULL or a live handle; `starts` NULL or writable.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_connector_running(connector: *const Connector, starts: *mut u64) -> u32 {
    #[cfg(any(unix, windows))]
    if let Some(connector) = connector.as_ref() {
        if !starts.is_null() {
            *starts = connector.starts();
        }
        return connector.running() as u32;
    }
    #[cfg(not(any(unix, windows)))]
    let _ = connector;
    if !starts.is_null() {
        *starts = 0;
    }
    0
}

/// The first stdout line is the ready line; see connector.rs.
pub const CONNECTOR_READY_LINE: u32 = 1;

/// Where the connector is: 0 waiting (starting, or to restart), 1 running,
/// 2 ready, 3 failed. For ready its ready line, and for failed why, is copied
/// to `text` (NUL-terminated, cut at a character boundary to fit `capacity`)
/// and its full length stored in `len`; otherwise `len` is 0.
///
/// # Safety
/// `connector` must be NULL or a live handle; `text` writable for `capacity`
/// bytes (or NULL with 0); `len` NULL or writable.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_connector_state(connector: *const Connector, text: *mut u8, capacity: usize, len: *mut usize) -> u32 {
    #[cfg(any(unix, windows))]
    let (code, detail) = match connector.as_ref().map(Connector::state) {
        None | Some(connector::State::Waiting) => (0, String::new()),
        Some(connector::State::Running) => (1, String::new()),
        Some(connector::State::Ready(line)) => (2, line),
        Some(connector::State::Failed(why)) => (3, why),
    };
    #[cfg(not(any(unix, windows)))]
    let (code, detail) = {
        let _ = connector;
        (0, String::new())
    };
    if !len.is_null() {
        *len = detail.len();
    }
    if !text.is_null() && capacity > 0 {
        let mut end = detail.len().min(capacity - 1);
        while !detail.is_char_boundary(end) {
            end -= 1;
        }
        std::ptr::copy_nonoverlapping(detail.as_ptr(), text, end);
        *text.add(end) = 0;
    }
    code
}

/// Stop the connector and whatever it started, and release the handle.
///
/// # Safety
/// `connector` must be NULL or a live handle, never used again after this.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_connector_stop(connector: *mut Connector) {
    if !connector.is_null() {
        drop(Box::from_raw(connector));
    }
}

/// End the connector's current run and start the next at once, without
/// waiting.
///
/// # Safety
/// `connector` must be NULL or a live handle.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_connector_restart(connector: *const Connector) {
    #[cfg(any(unix, windows))]
    if let Some(connector) = connector.as_ref() {
        connector.restart();
    }
    #[cfg(not(any(unix, windows)))]
    let _ = connector;
}

/// Ask the connector to stop without waiting, so that several stop at once;
/// stop then waits for it and releases the handle.
///
/// # Safety
/// `connector` must be NULL or a live handle, exclusively accessed by this call.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_connector_stop_begin(connector: *mut Connector) {
    #[cfg(any(unix, windows))]
    if let Some(connector) = connector.as_mut() {
        connector.stop_begin();
    }
    #[cfg(not(any(unix, windows)))]
    let _ = connector;
}

/// Have SIGTERM, SIGINT and SIGHUP ask to quit (see quit.rs): `wake` runs
/// on a background thread at the first, and `wheelhouse_quit_signalled`
/// reports it; a second ends the process. Returns 0 if they could not be
/// installed. Without such signals (Windows) it does nothing and returns 1.
#[no_mangle]
pub extern "C" fn wheelhouse_quit_signals_install(wake: Wake) -> u32 {
    #[cfg(unix)]
    return quit::install(wake).is_ok() as u32;
    #[cfg(not(unix))]
    {
        let _ = wake;
        1
    }
}

/// Whether a quit signal has arrived since the handlers were installed.
#[no_mangle]
pub extern "C" fn wheelhouse_quit_signalled() -> u32 {
    #[cfg(unix)]
    return quit::signalled() as u32;
    #[cfg(not(unix))]
    0
}

/// Drain queued patches on the UI thread.
///
/// # Safety
/// `server` must be NULL or a live handle, exclusively accessed by this call.
/// `apply` and its context must remain valid for the call. The callback may
/// borrow its byte slice only until it returns and must not unwind.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_ingress_poll(server: *mut Ingress, apply: Apply, retract: Option<Retract>, context: *mut c_void) {
    wheelhouse_ingress_poll_observed(server, apply, retract, None, context);
}

/// Drain patch and read requests on the UI thread. Each emitted record is
/// copied during the callback; no host pointer reaches the transport thread.
///
/// # Safety
/// Same handle/context requirements as poll. Observe must call emit synchronously
/// with valid buffers (NULL only for length zero; invalid UTF-8 yields 503), and must not unwind.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_ingress_poll_observed(
    server: *mut Ingress,
    apply: Apply,
    retract: Option<Retract>,
    observe: Option<Observe>,
    context: *mut c_void,
) {
    #[cfg(any(unix, windows))]
    if let Some(server) = server.as_mut() {
        struct Snapshot {
            values: Vec<serde_json::Value>,
            valid: bool,
        }
        unsafe extern "C" fn emit(context: *mut c_void, record: *const Workdir) {
            let snapshot = &mut *(context as *mut Snapshot);
            let r = &*record;
            unsafe fn text(t: &Text) -> Result<Option<&str>, std::str::Utf8Error> {
                if t.len == 0 {
                    Ok(None)
                } else {
                    std::str::from_utf8(std::slice::from_raw_parts(t.data, t.len)).map(Some)
                }
            }
            let (Ok(cwd), Ok(live_cwd), Ok(kind), Ok(id)) = (text(&r.cwd), text(&r.live_cwd), text(&r.entity_kind), text(&r.entity_id))
            else {
                snapshot.valid = false;
                return;
            };
            if cwd.is_none() && live_cwd.is_none() {
                return;
            }
            snapshot.values.push(serde_json::json!({"workspace_id": r.workspace_id, "view_id": r.view_id,
                "entity_kind": kind, "entity_id": id, "cwd": cwd, "live_cwd": live_cwd}));
        }
        // Coalesce adjacent reads within this UI drain. A patch invalidates the
        // snapshot so a later read still observes preceding host mutations.
        let mut cached = None;
        for _ in 0..64 {
            let Ok(pending) = server.queue.try_recv() else {
                break;
            };
            match pending {
                transport::Pending::Patch { bodies, replace, reply } => {
                    if !reply.is_closed() {
                        cached = None;
                        let _ = reply.send(apply_request(context, apply, retract, &bodies, replace));
                    }
                }
                transport::Pending::Workdirs { reply } => {
                    if !reply.is_closed() {
                        let value = cached.get_or_insert_with(|| {
                            let mut snapshot = Snapshot { values: Vec::new(), valid: true };
                            let ok = observe.is_some_and(|f| f(context, emit, &mut snapshot as *mut _ as *mut c_void) == 1);
                            (ok && snapshot.valid).then(|| std::sync::Arc::new(serde_json::json!({"workdirs": snapshot.values})))
                        });
                        let _ = reply.send(value.clone());
                    }
                }
            }
        }
    }
    #[cfg(not(any(unix, windows)))]
    let _ = (server, apply, retract, observe, context);
}

/// One request's patches, between frames: a snapshot retracts the provider's
/// facts first (refused without a retract), then every patch is applied. Any
/// rejected patch rejects the request; one the host couldn't take yet makes
/// it unavailable.
#[cfg(any(unix, windows))]
unsafe fn apply_request(context: *mut c_void, apply: Apply, retract: Option<Retract>, bodies: &[axum::body::Bytes], replace: bool) -> u32 {
    use transport::{APPLIED, REJECTED, UNAVAILABLE, UNSUPPORTED};
    // A retract some window couldn't take still lets the patches reach the
    // others; the request is unavailable, so the producer resends it.
    let mut outcome = APPLIED;
    if replace {
        match retract.map(|retract| retract(context)) {
            Some(APPLIED) => {}
            Some(UNAVAILABLE) => outcome = UNAVAILABLE,
            _ => return UNSUPPORTED,
        }
    }
    for body in bodies {
        match apply(context, body.as_ptr(), body.len()) {
            REJECTED => outcome = REJECTED,
            APPLIED => {}
            _ if outcome == APPLIED => outcome = UNAVAILABLE,
            _ => {}
        }
    }
    outcome
}

/// Stop the worker and release the listener.
///
/// # Safety
/// `server` must be NULL or a live handle returned by start, with no concurrent
/// poll/stop calls. It must never be accessed again after this call.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_ingress_stop(server: *mut Ingress) {
    if !server.is_null() {
        drop(Box::from_raw(server));
    }
}

/// Process-local monotonic milliseconds, shared by apply and expiry ticks.
#[no_mangle]
pub extern "C" fn wheelhouse_ingress_now_ms() -> u64 {
    static START: std::sync::OnceLock<std::time::Instant> = std::sync::OnceLock::new();
    START.get_or_init(std::time::Instant::now).elapsed().as_millis() as u64
}
