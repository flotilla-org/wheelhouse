//! Best-effort bounded recording. Filesystem work never runs on ingress/UI threads.
use std::{
    fs::{self, File, OpenOptions},
    io::{self, Write},
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc, Arc,
    },
    thread,
};

enum Record {
    Request {
        recording_id: String,
        sequence: u64,
        received_ms: u64,
        timestamp_ms: u64,
        path: String,
        method: String,
        body: Option<axum::body::Bytes>,
        status: u16,
    },
    #[cfg(test)]
    Value(serde_json::Value),
}
impl Record {
    fn into_value(self) -> serde_json::Value {
        match self {
            #[cfg(test)]
            Self::Value(value) => value,
            Self::Request {
                recording_id,
                sequence,
                received_ms,
                timestamp_ms,
                path,
                method,
                body,
                status,
            } => {
                let text = body.as_ref().and_then(|b| std::str::from_utf8(b).ok());
                let decoded = text.and_then(|s| serde_json::from_str::<serde_json::Value>(s).ok());
                let producer = decoded.as_ref().and_then(|v| v.get("source_id")).cloned();
                serde_json::json!({"recording_id": recording_id, "sequence": sequence,
                    "received_ms": received_ms, "timestamp_ms": timestamp_ms,
                    "producer": producer, "path": path, "method": method,
                    "body": decoded, "body_utf8": text, "status": status})
            }
        }
    }
}

pub struct Recorder {
    sender: Option<mpsc::SyncSender<Record>>,
    worker: Option<thread::JoinHandle<()>>,
    disabled: Arc<AtomicBool>,
    sequence: AtomicU64,
    recording_id: String,
}
fn disable(disabled: &AtomicBool, error: impl std::fmt::Display) {
    if !disabled.swap(true, Ordering::Relaxed) {
        let _ = writeln!(
            io::stderr(),
            "wheelhouse ingress: recording disabled: {error}"
        );
    }
}
impl Recorder {
    pub fn start(path: PathBuf, bytes: u64, files: usize) -> Arc<Self> {
        let (sender, receiver) = mpsc::sync_channel::<Record>(64);
        let disabled = Arc::new(AtomicBool::new(false));
        let worker_disabled = disabled.clone();
        let result = thread::Builder::new()
            .name("ingress-recorder".into())
            .spawn(move || {
                let result = (|| -> io::Result<()> {
                    let mut writer = Rotating::new(path, bytes, files)?;
                    while let Ok(value) = receiver.recv() {
                        if worker_disabled.load(Ordering::Relaxed) {
                            break;
                        }
                        let mut line = serde_json::to_vec(&value.into_value())?;
                        line.push(b'\n');
                        writer.write(&line)?;
                    }
                    Ok(())
                })();
                if let Err(error) = result {
                    disable(&worker_disabled, error);
                }
            });
        let worker = match result {
            Ok(worker) => Some(worker),
            Err(error) => {
                disable(&disabled, error);
                None
            }
        };
        Arc::new(Self {
            sender: Some(sender),
            worker,
            disabled,
            sequence: AtomicU64::new(0),
            recording_id: format!(
                "{:039}-{}",
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos(),
                std::process::id()
            ),
        })
    }
    fn record(&self, value: Record) {
        if !self.disabled.load(Ordering::Relaxed)
            && self.sender.as_ref().unwrap().try_send(value).is_err()
        {
            disable(&self.disabled, "writer queue full or unavailable");
        }
    }
}
impl Drop for Recorder {
    fn drop(&mut self) {
        drop(self.sender.take());
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
struct Rotating {
    path: PathBuf,
    bytes: u64,
    files: usize,
    file: Option<File>,
    size: u64,
}
impl Rotating {
    fn new(path: PathBuf, bytes: u64, files: usize) -> io::Result<Self> {
        if bytes == 0 || files == 0 || files > 100 {
            return Err(io::Error::other("invalid retention limits"));
        }
        // Apply reduced retention across restarts as well as within this run.
        let parent = path
            .parent()
            .filter(|p| !p.as_os_str().is_empty())
            .unwrap_or_else(|| std::path::Path::new("."));
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .ok_or_else(|| io::Error::other("recording filename must be UTF-8"))?;
        for entry in fs::read_dir(parent)? {
            let entry = entry?;
            let entry_name = entry.file_name();
            let Some(entry_name) = entry_name.to_str() else {
                continue;
            };
            let generation = if entry_name == name {
                Some(0)
            } else {
                entry_name
                    .strip_prefix(&format!("{name}."))
                    .and_then(|suffix| suffix.parse::<usize>().ok())
            };
            if let Some(n) = generation {
                if n >= files || entry.metadata()?.len() > bytes {
                    fs::remove_file(entry.path())?;
                }
            }
        }
        let file = OpenOptions::new().create(true).append(true).open(&path)?;
        // An interrupted write from an earlier run must not join two JSONL entries.
        let previous = fs::read(&path)?;
        let size = if previous.last().is_some_and(|last| *last != b'\n') {
            let complete = previous
                .iter()
                .rposition(|b| *b == b'\n')
                .map_or(0, |i| i + 1) as u64;
            // Windows append-only handles cannot truncate; repair with write access.
            OpenOptions::new()
                .write(true)
                .open(&path)?
                .set_len(complete)?;
            complete
        } else {
            previous.len() as u64
        };
        Ok(Self {
            path,
            bytes,
            files,
            file: Some(file),
            size,
        })
    }
    fn numbered(&self, n: usize) -> PathBuf {
        if n == 0 {
            self.path.clone()
        } else {
            PathBuf::from(format!("{}.{}", self.path.display(), n))
        }
    }
    fn write(&mut self, line: &[u8]) -> io::Result<()> {
        if line.len() as u64 > self.bytes {
            return Err(io::Error::other("entry exceeds recording file limit"));
        }
        if self.size + line.len() as u64 > self.bytes {
            // Close before rename for Windows as well as Unix.
            drop(self.file.take());
            for n in (1..self.files).rev() {
                let dst = self.numbered(n);
                match fs::remove_file(&dst) {
                    Ok(()) => {}
                    Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                    Err(e) => return Err(e),
                }
                let src = self.numbered(n - 1);
                if src.exists() {
                    fs::rename(src, dst)?;
                }
            }
            self.file = Some(
                OpenOptions::new()
                    .create(true)
                    .truncate(true)
                    .write(true)
                    .open(&self.path)?,
            );
            self.size = 0;
        }
        self.file.as_mut().unwrap().write_all(line)?;
        self.size += line.len() as u64;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn directory() -> PathBuf {
        let path = std::env::temp_dir().join(format!(
            "wh-recorder-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&path).unwrap();
        path
    }
    #[test]
    fn rotation_never_exceeds_total_bound_and_keeps_complete_lines() {
        // Issue #223: vary record sizes across exact-fit and overflow boundaries,
        // and retention from a single file to multiple generations.
        for files in 1..=4 {
            for limit in [16, 17, 31, 32] {
                let dir = directory();
                let path = dir.join("ingress.jsonl");
                let mut writer = Rotating::new(path.clone(), limit, files).unwrap();
                for size in (2..=16).cycle().take(80) {
                    let line = format!("{}\n", "x".repeat(size - 1));
                    writer.write(line.as_bytes()).unwrap();
                    let entries: Vec<_> = fs::read_dir(&dir).unwrap().map(Result::unwrap).collect();
                    assert!(entries.len() <= files);
                    let total: u64 = entries.iter().map(|e| e.metadata().unwrap().len()).sum();
                    assert!(total <= limit * files as u64);
                    for entry in entries {
                        let data = fs::read(entry.path()).unwrap();
                        assert!(data.len() as u64 <= limit);
                        assert!(data.is_empty() || data.last() == Some(&b'\n'));
                    }
                }
                // Oversized entries are rejected without breaking retention.
                assert!(writer.write(&vec![b'x'; limit as usize + 1]).is_err());
                drop(writer);
                fs::remove_dir_all(dir).unwrap();
            }
        }
    }
    #[test]
    fn restart_discards_an_interrupted_tail() {
        // Issue #223: every retained entry must remain a complete JSONL line.
        let dir = directory();
        let path = dir.join("ingress.jsonl");
        fs::write(&path, b"{\"ok\":1}\n{\"partial\":").unwrap();
        let mut writer = Rotating::new(path.clone(), 100, 2).unwrap();
        writer.write(b"{\"ok\":2}\n").unwrap();
        drop(writer);
        assert_eq!(fs::read(&path).unwrap(), b"{\"ok\":1}\n{\"ok\":2}\n");
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn reduced_retention_is_enforced_on_restart() {
        // Issue #223: total retention includes files left by previous runs.
        let dir = directory();
        let path = dir.join("ingress.jsonl");
        let mut writer = Rotating::new(path.clone(), 32, 4).unwrap();
        for _ in 0..8 {
            writer.write(b"123456789012345\n").unwrap();
        }
        drop(writer);
        let mut writer = Rotating::new(path, 16, 2).unwrap();
        writer.write(b"new\n").unwrap();
        let entries: Vec<_> = fs::read_dir(&dir).unwrap().map(Result::unwrap).collect();
        assert!(entries.len() <= 2);
        assert!(
            entries
                .iter()
                .map(|e| e.metadata().unwrap().len())
                .sum::<u64>()
                <= 32
        );
        drop(writer);
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn saturated_queue_disables_recording_without_waiting() {
        // Issue #223: pressure at the filesystem boundary never blocks ingress.
        let (sender, _receiver) = mpsc::sync_channel(1);
        let recorder = Recorder {
            sender: Some(sender),
            worker: None,
            disabled: Arc::new(AtomicBool::new(false)),
            sequence: AtomicU64::new(0),
            recording_id: "test".into(),
        };
        recorder.record(Record::Value(serde_json::json!({"first": 1})));
        assert!(!recorder.disabled.load(Ordering::Relaxed));
        recorder.record(Record::Value(serde_json::json!({"second": 2})));
        assert!(recorder.disabled.load(Ordering::Relaxed));
    }
    #[test]
    fn writer_failure_disables_once_without_affecting_caller() {
        // Issue #223: an unavailable filesystem disables best-effort recording.
        let dir = directory();
        let recorder = Recorder::start(dir.clone(), 100, 2); // opening a directory fails
        for _ in 0..100 {
            recorder.record(Record::Value(serde_json::json!({"status": 204})));
            if recorder.disabled.load(Ordering::Relaxed) {
                break;
            }
            thread::sleep(std::time::Duration::from_millis(1));
        }
        assert!(recorder.disabled.load(Ordering::Relaxed));
        recorder.record(Record::Value(serde_json::json!({"status": 422})));
        drop(recorder);
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn recorder_drains_byte_preserving_entries_on_drop() {
        // Issue #223: shutdown retains every queued entry in order.
        let dir = directory();
        let path = dir.join("ingress.jsonl");
        let recorder = Recorder::start(path.clone(), 4096, 2);
        for sequence in 0..20 {
            recorder.record(Record::Value(
                serde_json::json!({"sequence": sequence, "body_utf8": "{  \"path\": \"/東京\" }"}),
            ));
        }
        drop(recorder);
        let values: Vec<serde_json::Value> = fs::read_to_string(path)
            .unwrap()
            .lines()
            .map(|s| serde_json::from_str(s).unwrap())
            .collect();
        assert_eq!(values.len(), 20);
        for (i, value) in values.iter().enumerate() {
            assert_eq!(value["sequence"], i);
            assert_eq!(value["body_utf8"], "{  \"path\": \"/東京\" }");
        }
        fs::remove_dir_all(dir).unwrap();
    }
}

/// Installed only when opted in; the ordinary router has no recording work.
pub async fn capture(
    axum::extract::State(recorder): axum::extract::State<Arc<Recorder>>,
    request: axum::extract::Request,
    next: axum::middleware::Next,
) -> axum::response::Response {
    use axum::{extract::FromRequest, response::IntoResponse};
    let path = request.uri().path().to_owned();
    let method = request.method().to_string();
    let (parts, body) = request.into_parts();
    if method != "POST" || path != "/v1/metadata/patch" {
        let (received_ms, timestamp_ms) = received_time();
        let sequence = recorder.sequence.fetch_add(1, Ordering::Relaxed);
        let response = next
            .run(axum::extract::Request::from_parts(parts, body))
            .await;
        recorder.record(Record::Request {
            recording_id: recorder.recording_id.clone(),
            sequence,
            received_ms,
            timestamp_ms,
            path,
            method,
            body: None,
            status: response.status().as_u16(),
        });
        return response;
    }
    let bytes = axum::body::Bytes::from_request(
        axum::extract::Request::from_parts(parts.clone(), body),
        &(),
    )
    .await;
    // Assign order/time when a complete request reaches the router, before admission.
    let (received_ms, timestamp_ms) = received_time();
    let sequence = recorder.sequence.fetch_add(1, Ordering::Relaxed);
    let (body, response) = match bytes {
        Ok(bytes) => {
            let copy = bytes.clone();
            let response = next
                .run(axum::extract::Request::from_parts(
                    parts,
                    axum::body::Body::from(bytes),
                ))
                .await;
            (Some(copy), response)
        }
        Err(error) => (None, error.into_response()),
    };
    recorder.record(Record::Request {
        recording_id: recorder.recording_id.clone(),
        sequence,
        received_ms,
        timestamp_ms,
        path,
        method,
        body,
        status: response.status().as_u16(),
    });
    response
}

fn received_time() -> (u64, u64) {
    (
        crate::wheelhouse_ingress_now_ms(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64,
    )
}
