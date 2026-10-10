//! Supervised connector processes: one per Dashboard subscription. Each runs
//! its command on a thread of its own, restarts it when it exits, and stops
//! it (with whatever it started) when the subscription goes.
//!
//! Restarts wait 1s, doubling to 30s; a run that lasted 30s resets the wait,
//! as `tools/daily-driver.py` did when it supervised `flotilla pm connect`.
//! The child's output appends to one log, where each exit is noted too.
use std::{
    fs::{File, OpenOptions},
    io::{Read, Seek, SeekFrom, Write},
    path::PathBuf,
    process::{Child, Command, Stdio},
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc, Arc,
    },
    thread,
    time::{Duration, Instant},
};

pub const STABLE: Duration = Duration::from_secs(30);
pub const MAX_BACKOFF: Duration = Duration::from_secs(30);
const POLL: Duration = Duration::from_millis(100);

pub struct Spec {
    pub argv: Vec<String>,
    /// Variables set (`Some`) or removed (`None`) in the inherited environment.
    pub env: Vec<(String, Option<String>)>,
    pub log: PathBuf,
}

pub struct Connector {
    running: Arc<AtomicBool>,
    starts: Arc<AtomicU64>,
    stop: Option<mpsc::Sender<()>>,
    thread: Option<thread::JoinHandle<()>>,
}

/// A started child and what owns its descendants: its process group on
/// Unix, a job object on Windows.
struct Run {
    child: Child,
    #[cfg(windows)]
    job: windows::Job,
}

impl Run {
    fn spawn(spec: &Spec, log: &File) -> std::io::Result<Self> {
        let mut command = Command::new(&spec.argv[0]);
        command.args(&spec.argv[1..]);
        for (name, value) in &spec.env {
            match value {
                Some(value) => command.env(name, value),
                None => command.env_remove(name),
            };
        }
        command
            .stdin(Stdio::null())
            .stdout(log.try_clone()?)
            .stderr(log.try_clone()?);
        #[cfg(unix)]
        {
            use std::os::unix::process::CommandExt;
            command.process_group(0);
            // A connector never outlives Wheelhouse: it is stopped when the
            // thread that started it ends, as when the process ends abruptly.
            #[cfg(target_os = "linux")]
            unsafe {
                command.pre_exec(|| {
                    libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGTERM);
                    Ok(())
                });
            }
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            const CREATE_NO_WINDOW: u32 = 0x0800_0000;
            command.creation_flags(CREATE_NO_WINDOW);
        }
        #[cfg(windows)]
        let job = windows::Job::new()?;
        let child = command.spawn()?;
        #[cfg(windows)]
        {
            use std::os::windows::io::AsRawHandle;
            // pm connect starts nothing before it has connected, so assigning
            // just after spawn still owns everything it starts. Closing the
            // job (KILL_ON_JOB_CLOSE) stops them all, as when Wheelhouse ends.
            if let Err(error) = job.assign(child.as_raw_handle()) {
                let mut child = child;
                let _ = child.kill();
                let _ = child.wait();
                return Err(error);
            }
        }
        Ok(Self {
            child,
            #[cfg(windows)]
            job,
        })
    }

    /// Stops what is left of the run: the child, if it still runs, and
    /// anything it started.
    fn stop(mut self) {
        #[cfg(unix)]
        {
            let group = self.child.id() as libc::pid_t;
            unsafe {
                libc::killpg(group, libc::SIGTERM);
            }
            let deadline = Instant::now() + Duration::from_secs(5);
            while Instant::now() < deadline && matches!(self.child.try_wait(), Ok(None)) {
                thread::sleep(Duration::from_millis(20));
            }
            if matches!(self.child.try_wait(), Ok(None)) {
                unsafe {
                    libc::killpg(group, libc::SIGKILL);
                }
            }
        }
        #[cfg(windows)]
        self.job.terminate();
        #[cfg(not(any(unix, windows)))]
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

/// Flotilla's hint for a client and daemon from different builds, from what
/// the connector wrote before it exited.
pub fn mismatch_hint(output: &str) -> Option<String> {
    let rest = &output[output.find("wire generation mismatch:")?..];
    let build = |label: &str| -> Option<&str> {
        let from = &rest[rest.find(label)?..];
        let start = from.find("(build ")? + "(build ".len();
        let end = from[start..].find(')')?;
        Some(&from[start..start + end])
    };
    let client = build("client fingerprint")?;
    let daemon = build("daemon fingerprint")?;
    Some(format!(
        "Flotilla build mismatch: client {client}, daemon {daemon}; set FLOTILLA_BIN to a binary \
         matching the daemon (fleet install: ~/.local/opt/flotilla-fleet/current/bin/flotilla)."
    ))
}

fn note(log: &mut File, message: &str) {
    let _ = writeln!(log, "{message}");
    eprintln!("{message}");
}

/// What the run wrote to the log since `from`.
fn written_since(log: &mut File, from: u64) -> String {
    let mut output = Vec::new();
    if log.seek(SeekFrom::Start(from)).is_ok() {
        let _ = log.read_to_end(&mut output);
    }
    String::from_utf8_lossy(&output).into_owned()
}

impl Connector {
    pub fn start(spec: Spec, wake: super::Wake) -> Result<Self, String> {
        if spec.argv.is_empty() {
            return Err("connector command must not be empty".into());
        }
        if let Some(parent) = spec.log.parent() {
            std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let mut log = OpenOptions::new()
            .create(true)
            .append(true)
            .read(true)
            .open(&spec.log)
            .map_err(|e| e.to_string())?;
        let running = Arc::new(AtomicBool::new(false));
        let starts = Arc::new(AtomicU64::new(0));
        let (stop, stopped) = mpsc::channel::<()>();
        let (running_, starts_) = (running.clone(), starts.clone());
        let thread = thread::Builder::new()
            .name("wheelhouse-connector".into())
            .spawn(move || {
                let mut backoff = Duration::from_secs(1);
                // Waits `delay`; true when asked to stop first.
                let stopping = |delay| {
                    !matches!(
                        stopped.recv_timeout(delay),
                        Err(mpsc::RecvTimeoutError::Timeout)
                    )
                };
                loop {
                    let from = log.seek(SeekFrom::End(0)).unwrap_or(0);
                    let mut run = match Run::spawn(&spec, &log) {
                        Ok(run) => run,
                        Err(error) => {
                            let message = format!(
                                "Connector could not start: {error}; retrying in {}s",
                                backoff.as_secs()
                            );
                            note(&mut log, &message);
                            if stopping(backoff) {
                                return;
                            }
                            backoff = (backoff * 2).min(MAX_BACKOFF);
                            continue;
                        }
                    };
                    let started = Instant::now();
                    running_.store(true, Ordering::SeqCst);
                    starts_.fetch_add(1, Ordering::SeqCst);
                    wake();
                    let status = loop {
                        if stopping(POLL) {
                            run.stop();
                            running_.store(false, Ordering::SeqCst);
                            return;
                        }
                        match run.child.try_wait() {
                            Ok(Some(status)) => break status.to_string(),
                            Ok(None) => {}
                            Err(error) => break error.to_string(),
                        }
                    };
                    // Whatever it started goes with it.
                    run.stop();
                    running_.store(false, Ordering::SeqCst);
                    wake();
                    if started.elapsed() >= STABLE {
                        backoff = Duration::from_secs(1);
                    }
                    if let Some(hint) = mismatch_hint(&written_since(&mut log, from)) {
                        note(&mut log, &hint);
                    }
                    let message = format!(
                        "Connector exited with {status}; restarting in {}s",
                        backoff.as_secs()
                    );
                    note(&mut log, &message);
                    if stopping(backoff) {
                        return;
                    }
                    backoff = (backoff * 2).min(MAX_BACKOFF);
                }
            })
            .map_err(|e| e.to_string())?;
        Ok(Self {
            running,
            starts,
            stop: Some(stop),
            thread: Some(thread),
        })
    }

    pub fn running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }

    pub fn starts(&self) -> u64 {
        self.starts.load(Ordering::SeqCst)
    }
}

impl Drop for Connector {
    fn drop(&mut self) {
        drop(self.stop.take());
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

#[cfg(windows)]
mod windows {
    use std::{io, mem::size_of, os::windows::io::RawHandle};
    use windows_sys::Win32::{
        Foundation::{CloseHandle, HANDLE},
        System::JobObjects::{
            AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation,
            SetInformationJobObject, TerminateJobObject, JOBOBJECT_EXTENDED_LIMIT_INFORMATION,
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
        },
    };

    pub struct Job(HANDLE);
    // SAFETY: a job handle may be used and closed from any thread.
    unsafe impl Send for Job {}

    impl Job {
        pub fn new() -> io::Result<Self> {
            let handle = unsafe { CreateJobObjectW(std::ptr::null(), std::ptr::null()) };
            if handle.is_null() {
                return Err(io::Error::last_os_error());
            }
            let job = Self(handle);
            // SAFETY: a plain C struct; zero is its empty value.
            let mut limits: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = unsafe { std::mem::zeroed() };
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            let ok = unsafe {
                SetInformationJobObject(
                    job.0,
                    JobObjectExtendedLimitInformation,
                    &limits as *const _ as *const _,
                    size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
                )
            };
            if ok == 0 {
                return Err(io::Error::last_os_error());
            }
            Ok(job)
        }

        pub fn assign(&self, process: RawHandle) -> io::Result<()> {
            if unsafe { AssignProcessToJobObject(self.0, process as HANDLE) } == 0 {
                return Err(io::Error::last_os_error());
            }
            Ok(())
        }

        pub fn terminate(&self) {
            unsafe {
                TerminateJobObject(self.0, 1);
            }
        }
    }

    impl Drop for Job {
        fn drop(&mut self) {
            unsafe {
                CloseHandle(self.0);
            }
        }
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    extern "C" fn wake() {}

    fn wait_for(what: &str, condition: impl Fn() -> bool) {
        let deadline = Instant::now() + Duration::from_secs(10);
        while !condition() {
            assert!(Instant::now() < deadline, "timed out waiting for {what}");
            thread::sleep(Duration::from_millis(20));
        }
    }

    #[test]
    fn restarts_after_exit_and_stops_what_it_started() {
        let dir = std::env::temp_dir().join(format!("wh-connector-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let log = dir.join("connector.log");
        let marker = dir.join("grandchild.pid");
        // The first run exits; the next starts a grandchild and stays.
        let script = format!(
            "echo \"daemon=$FLOTILLA_DAEMON home=${{HOME:-unset}}\"; if [ -e {m}.once ]; then sleep 60 & echo $! > {m}; wait; \
             else touch {m}.once; exit 7; fi",
            m = marker.display()
        );
        let connector = Connector::start(
            Spec {
                argv: vec!["/bin/sh".into(), "-c".into(), script],
                env: vec![
                    ("FLOTILLA_DAEMON".into(), Some("ssh://alpha".into())),
                    ("HOME".into(), None),
                ],
                log: log.clone(),
            },
            wake,
        )
        .unwrap();
        wait_for("a restart", || {
            connector.starts() == 2
                && std::fs::read_to_string(&marker).is_ok_and(|t| t.ends_with('\n'))
        });
        assert!(connector.running());
        let text = std::fs::read_to_string(&log).unwrap();
        assert!(text.contains("daemon=ssh://alpha home=unset"), "{text}");
        assert!(text.contains("exit status: 7; restarting in 1s"), "{text}");
        let grandchild: libc::pid_t = std::fs::read_to_string(&marker)
            .unwrap()
            .trim()
            .parse()
            .unwrap();
        drop(connector);
        // Stopping took the grandchild too.
        wait_for(
            "the grandchild to go",
            || unsafe { libc::kill(grandchild, 0) } != 0,
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn mismatch_hint_names_both_builds() {
        let output =
            "wire generation mismatch: client fingerprint old speaks proto 21 (build old+dirty); \
                      daemon fingerprint new speaks proto 21 (build new)";
        let hint = mismatch_hint(output).unwrap();
        assert!(hint.starts_with("Flotilla build mismatch: client old+dirty, daemon new;"));
        assert!(mismatch_hint("connector ready").is_none());
    }
}
