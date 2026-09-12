//! Wheelhouse's HTTP ingress. UI state stays on the C host's main thread.
use std::ffi::c_void;

pub type Wake = extern "C" fn();
pub type Apply = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> u32;

#[cfg(unix)]
mod unix {
    use super::*;
    use axum::{
        body::Bytes,
        extract::{DefaultBodyLimit, State},
        http::{HeaderMap, StatusCode},
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

    pub struct Pending {
        pub body: Bytes,
        pub reply: oneshot::Sender<u32>,
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
        if state.queue.try_send(Pending { body, reply }).is_err() {
            return StatusCode::SERVICE_UNAVAILABLE;
        }
        (state.wake)();
        match tokio::time::timeout(Duration::from_secs(5), response).await {
            Ok(Ok(1)) => StatusCode::NO_CONTENT,
            Ok(Ok(0)) => StatusCode::UNPROCESSABLE_ENTITY,
            _ => StatusCode::SERVICE_UNAVAILABLE,
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
}

#[cfg(unix)]
pub use unix::Ingress;
#[cfg(not(unix))]
pub struct Ingress;

/// Start a Unix HTTP listener. Error is NUL-terminated on failure.
///
/// # Safety
/// Input/output pointers must be valid for their lengths. `wake` must be safe
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
    let result = std::str::from_utf8(std::slice::from_raw_parts(path, len))
        .map_err(|e| e.to_string())
        .and_then(|p| Ingress::start(p.into(), wake));
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
    #[cfg(unix)]
    if let Some(server) = server.as_mut() {
        for _ in 0..64 {
            let Ok(pending) = server.queue.try_recv() else {
                break;
            };
            if !pending.reply.is_closed() {
                let outcome = apply(context, pending.body.as_ptr(), pending.body.len());
                let _ = pending.reply.send(outcome);
            }
        }
    }
    #[cfg(not(unix))]
    let _ = (server, apply, context);
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
