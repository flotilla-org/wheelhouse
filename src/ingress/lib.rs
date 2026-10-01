//! Wheelhouse's HTTP ingress. UI state stays on the C host's main thread.
use std::ffi::c_void;

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

#[cfg(unix)]
mod unix {
    use super::*;
    use axum::{
        body::Bytes,
        extract::{DefaultBodyLimit, State},
        http::{HeaderMap, StatusCode},
        response::{IntoResponse, Response},
        routing::{get, post},
        Router,
    };
    use std::{
        os::unix::fs::{MetadataExt, PermissionsExt},
        path::PathBuf,
        sync::mpsc,
        thread,
        time::Duration,
    };
    use tokio::sync::oneshot;

    pub enum Pending {
        Patch {
            body: Bytes,
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

    async fn patch(State(state): State<Shared>, headers: HeaderMap, body: Bytes) -> StatusCode {
        if headers
            .get("content-type")
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.split(';').next())
            .map(str::trim)
            != Some("application/json")
        {
            return StatusCode::UNSUPPORTED_MEDIA_TYPE;
        }
        let Ok(value) = serde_json::from_slice::<serde_json::Value>(&body) else {
            return StatusCode::BAD_REQUEST;
        };
        if value.get("type").and_then(|v| v.as_str()) != Some("metadata-patch") {
            return StatusCode::BAD_REQUEST;
        }
        let (reply, response) = oneshot::channel();
        if state
            .queue
            .try_send(Pending::Patch { body, reply })
            .is_err()
        {
            return StatusCode::SERVICE_UNAVAILABLE;
        }
        (state.wake)();
        match tokio::time::timeout(Duration::from_secs(5), response).await {
            Ok(Ok(1)) => StatusCode::NO_CONTENT,
            Ok(Ok(0)) => StatusCode::UNPROCESSABLE_ENTITY,
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
            Ok(Ok(Some(value))) => (
                [
                    ("cache-control", "no-store"),
                    ("content-type", "application/json"),
                ],
                value.to_string(),
            )
                .into_response(),
            _ => StatusCode::SERVICE_UNAVAILABLE.into_response(),
        }
    }

    struct SocketGuard {
        path: PathBuf,
        dev: u64,
        ino: u64,
    }
    impl Drop for SocketGuard {
        fn drop(&mut self) {
            if std::fs::symlink_metadata(&self.path)
                .is_ok_and(|m| m.dev() == self.dev && m.ino() == self.ino)
            {
                let _ = std::fs::remove_file(&self.path);
            }
        }
    }
    impl Ingress {
        pub fn start(path: PathBuf, wake: Wake) -> Result<Self, String> {
            let parent = path
                .parent()
                .ok_or("socket needs a private parent directory")?;
            let meta = std::fs::metadata(parent).map_err(|e| e.to_string())?;
            if !meta.is_dir()
                || meta.mode() & 0o077 != 0
                || meta.uid() != unsafe { libc::geteuid() }
            {
                return Err(
                    "socket parent must be owned by this user and private (mode 0700)".into(),
                );
            }
            let listener =
                std::os::unix::net::UnixListener::bind(&path).map_err(|e| e.to_string())?;
            let meta = std::fs::symlink_metadata(&path).map_err(|e| e.to_string())?;
            let guard = SocketGuard {
                path: path.clone(),
                dev: meta.dev(),
                ino: meta.ino(),
            };
            std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600))
                .map_err(|e| e.to_string())?;
            listener.set_nonblocking(true).map_err(|e| e.to_string())?;
            let runtime = tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build()
                .map_err(|e| e.to_string())?;
            // Reads and writes deliberately share one bounded admission queue:
            // a burst of either may make the other receive 503 and retry. This
            // preserves request ordering and bounds total pending UI work.
            let (queue, receiver) = mpsc::sync_channel(64);
            let (shutdown, stop) = oneshot::channel();
            let thread = thread::Builder::new()
                .name("wheelhouse-ingress".into())
                .spawn(move || {
                    let _guard = guard;
                    runtime.block_on(async move {
                        let listener = tokio::net::UnixListener::from_std(listener)
                            .expect("registered Unix listener");
                        let app = Router::new()
                            .route("/v1/health", get(|| async { StatusCode::NO_CONTENT }))
                            .route("/v1/metadata/patch", post(patch))
                            .route("/v1/observed/workdirs", get(workdirs))
                            .layer(DefaultBodyLimit::max(1024 * 1024))
                            .with_state(Shared { queue, wake });
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
            Ok(Self {
                queue: receiver,
                shutdown: Some(shutdown),
                thread: Some(thread),
            })
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
            let empty = || Text {
                data: std::ptr::null(),
                len: 0,
            };
            emit(
                output,
                &Workdir {
                    workspace_id: host.generation,
                    view_id: 9,
                    entity_kind: empty(),
                    entity_id: empty(),
                    live_cwd: empty(),
                    cwd: Text {
                        data: b"/repo".as_ptr(),
                        len: 5,
                    },
                },
            );
            1
        }
        fn read(
            queue: &mpsc::SyncSender<Pending>,
        ) -> oneshot::Receiver<Option<std::sync::Arc<serde_json::Value>>> {
            let (reply, response) = oneshot::channel();
            assert!(queue.send(Pending::Workdirs { reply }).is_ok());
            response
        }
        #[test]
        fn failed_observation_is_shared_only_within_the_drain() {
            let (queue, receiver) = mpsc::sync_channel(64);
            let mut server = Ingress {
                queue: receiver,
                shutdown: None,
                thread: None,
            };
            let mut host = Host {
                fail: true,
                ..Host::default()
            };
            let reads: Vec<_> = (0..4).map(|_| read(&queue)).collect();
            unsafe {
                wheelhouse_ingress_poll_observed(
                    &mut server,
                    apply,
                    Some(observe),
                    &mut host as *mut _ as *mut c_void,
                );
            }
            assert_eq!(host.reads, 1);
            for response in reads {
                assert!(response.blocking_recv().unwrap().is_none());
            }
            host.fail = false;
            let response = read(&queue);
            unsafe {
                wheelhouse_ingress_poll_observed(
                    &mut server,
                    apply,
                    Some(observe),
                    &mut host as *mut _ as *mut c_void,
                );
            }
            assert!(response.blocking_recv().unwrap().is_some());
            assert_eq!(host.reads, 2);
        }
        #[test]
        fn burst_reads_share_snapshot_but_patches_invalidate_it() {
            let (queue, receiver) = mpsc::sync_channel(64);
            let mut server = Ingress {
                queue: receiver,
                shutdown: None,
                thread: None,
            };
            let mut host = Host::default();
            let mut reads = Vec::new();
            drop(read(&queue)); // Cancelled reads do not call the observer.
            for _ in 0..4 {
                reads.push(read(&queue));
            }
            let (reply, ack) = oneshot::channel();
            assert!(queue
                .send(Pending::Patch {
                    body: Bytes::new(),
                    reply
                })
                .is_ok());
            for _ in 0..4 {
                reads.push(read(&queue));
            }
            unsafe {
                wheelhouse_ingress_poll_observed(
                    &mut server,
                    apply,
                    Some(observe),
                    &mut host as *mut _ as *mut c_void,
                );
            }
            assert_eq!(host.reads, 2);
            assert_eq!(ack.blocking_recv().unwrap(), 1);
            for (index, response) in reads.into_iter().enumerate() {
                let value = response.blocking_recv().unwrap().unwrap();
                assert_eq!(
                    value["workdirs"][0]["workspace_id"],
                    if index < 4 { 0 } else { 1 }
                );
            }
            let response = read(&queue);
            unsafe {
                wheelhouse_ingress_poll_observed(
                    &mut server,
                    apply,
                    Some(observe),
                    &mut host as *mut _ as *mut c_void,
                );
            }
            assert!(response.blocking_recv().unwrap().is_some());
            assert_eq!(host.reads, 3); // The cache never survives a UI drain.
        }
    }
}

#[cfg(unix)]
pub use unix::Ingress;
#[cfg(not(unix))]
pub struct Ingress;

/// Start a Unix HTTP listener. Error is NUL-terminated on failure.
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
    #[cfg(unix)]
    let result = if path.is_null() || len == 0 {
        Err("socket path must not be null or empty".into())
    } else {
        std::str::from_utf8(std::slice::from_raw_parts(path, len))
            .map_err(|e| e.to_string())
            .and_then(|p| Ingress::start(p.into(), wake))
    };
    #[cfg(not(unix))]
    let result: Result<Ingress, String> = {
        let _ = (path, len, wake);
        Err("HTTP/UDS ingress is supported on Unix hosts".into())
    };
    match result {
        Ok(server) => Box::into_raw(Box::new(server)),
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

/// Drain queued patches on the UI thread.
///
/// # Safety
/// `server` must be NULL or a live handle, exclusively accessed by this call.
/// `apply` and its context must remain valid for the call. The callback may
/// borrow its byte slice only until it returns and must not unwind.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_ingress_poll(
    server: *mut Ingress,
    apply: Apply,
    context: *mut c_void,
) {
    wheelhouse_ingress_poll_observed(server, apply, None, context);
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
    observe: Option<Observe>,
    context: *mut c_void,
) {
    #[cfg(unix)]
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
            let (Ok(cwd), Ok(live_cwd), Ok(kind), Ok(id)) = (
                text(&r.cwd),
                text(&r.live_cwd),
                text(&r.entity_kind),
                text(&r.entity_id),
            ) else {
                snapshot.valid = false;
                return;
            };
            if cwd.is_none() && live_cwd.is_none() {
                return;
            }
            snapshot.values.push(
                serde_json::json!({"workspace_id": r.workspace_id, "view_id": r.view_id,
                "entity_kind": kind, "entity_id": id, "cwd": cwd, "live_cwd": live_cwd}),
            );
        }
        // Coalesce adjacent reads within this UI drain. A patch invalidates the
        // snapshot so a later read still observes preceding host mutations.
        let mut cached = None;
        for _ in 0..64 {
            let Ok(pending) = server.queue.try_recv() else {
                break;
            };
            match pending {
                unix::Pending::Patch { body, reply } => {
                    if !reply.is_closed() {
                        cached = None;
                        let outcome = apply(context, body.as_ptr(), body.len());
                        let _ = reply.send(outcome);
                    }
                }
                unix::Pending::Workdirs { reply } => {
                    if !reply.is_closed() {
                        let value = cached.get_or_insert_with(|| {
                            let mut snapshot = Snapshot {
                                values: Vec::new(),
                                valid: true,
                            };
                            let ok = observe.is_some_and(|f| {
                                f(context, emit, &mut snapshot as *mut _ as *mut c_void) == 1
                            });
                            (ok && snapshot.valid).then(|| {
                                std::sync::Arc::new(
                                    serde_json::json!({"workdirs": snapshot.values}),
                                )
                            })
                        });
                        let _ = reply.send(value.clone());
                    }
                }
            }
        }
    }
    #[cfg(not(unix))]
    let _ = (server, apply, observe, context);
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
    START
        .get_or_init(std::time::Instant::now)
        .elapsed()
        .as_millis() as u64
}
