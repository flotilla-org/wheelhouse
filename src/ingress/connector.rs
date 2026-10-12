//! Supervised connector processes: one per Dashboard subscription. Each runs
//! its command on a thread of its own, restarts it when it exits, and stops
//! it (with whatever it started) when the subscription goes.
//!
//! Restarts wait 1s, doubling to 30s; a run that lasted 30s resets the wait,
//! as `tools/daily-driver.py` did when it supervised `flotilla pm connect`.
//! The child's output appends to one log, where each exit is noted too.
//!
//! A connector never outlives Wheelhouse. Stopping ends what it started
//! (on Unix, TERM, then KILL after `GRACE`), and Wheelhouse stops every connector
//! before it exits. Should Wheelhouse end without doing so (a crash,
//! SIGKILL, a quit that skips teardown), on Unix each connector runs under
//! a guardian, a `/bin/sh` that leads its process group and holds the read
//! end of a lifeline pipe whose only write end is Wheelhouse's: when the
//! pipe closes the guardian ends the group the same way. Linux also sends
//! the guardian SIGTERM when its parent thread ends; on Windows the job
//! object kills everything in it when its last handle, Wheelhouse's, closes.
use std::{
    fs::{File, OpenOptions},
    io::{Read, Seek, SeekFrom, Write},
    path::PathBuf,
    process::{Child, Command},
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
/// How long a stopped connector has after SIGTERM before SIGKILL. The
/// guardian waits as long (`GUARDIAN`, 20 × 0.1s).
#[cfg(unix)]
pub const GRACE: Duration = Duration::from_secs(2);

/// Runs `"$@"` (the connector) in its own process group, which it leads,
/// with the lifeline as fd 0. When the lifeline closes (Wheelhouse is gone,
/// or dropped it) a watcher ignores SIGTERM, sends it to the group, waits for
/// the connector to go (up to 2s) and then SIGKILLs the group, itself
/// included. The guardian exits with the connector's status; whatever is
/// left of the group is stopped by `Run::stop`.
#[cfg(unix)]
const GUARDIAN: &str = r#"exec 3<&0 </dev/null
"$@" 3<&- &
c=$!
{ cat <&3 >/dev/null 2>&1; trap '' TERM; kill -TERM 0 2>/dev/null; i=0
  while [ $i -lt 20 ] && kill -0 $c 2>/dev/null; do sleep 0.1; i=$((i+1)); done
  kill -KILL 0; } &
exec 3<&-
wait $c
"#;

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

/// A started child and what owns its descendants: on Unix the guardian
/// (the child) leads their process group and `lifeline` keeps it waiting;
/// on Windows, a job object.
struct Run {
    child: Child,
    #[cfg(unix)]
    lifeline: Option<std::io::PipeWriter>,
    #[cfg(windows)]
    job: windows::Job,
}

impl Run {
    fn spawn(spec: &Spec, log: &File) -> std::io::Result<Self> {
        #[cfg(unix)]
        let (mut command, lifeline) = {
            let mut command = Command::new("/bin/sh");
            command
                .args(["-c", GUARDIAN, "wheelhouse-connector"])
                .args(&spec.argv);
            // Both ends close on exec: only this process holds the write end.
            let (reader, writer) = std::io::pipe()?;
            command.stdin(reader);
            (command, writer)
        };
        #[cfg(not(unix))]
        let mut command = {
            let mut command = Command::new(&spec.argv[0]);
            command
                .args(&spec.argv[1..])
                .stdin(std::process::Stdio::null());
            command
        };
        for (name, value) in &spec.env {
            match value {
                Some(value) => command.env(name, value),
                None => command.env_remove(name),
            };
        }
        command.stdout(log.try_clone()?).stderr(log.try_clone()?);
        #[cfg(unix)]
        {
            use std::os::unix::process::CommandExt;
            command.process_group(0);
            // Besides the lifeline: the guardian is sent SIGTERM when the
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
            #[cfg(unix)]
            lifeline: Some(lifeline),
            #[cfg(windows)]
            job,
        })
    }

    /// Stops what is left of the run: the child, if it still runs, and
    /// anything it started. On Unix: SIGTERM to the group, then SIGKILL to
    /// whatever of it is left after [`GRACE`].
    fn stop(mut self) {
        #[cfg(unix)]
        {
            let group = self.child.id() as libc::pid_t;
            unsafe {
                libc::killpg(group, libc::SIGTERM);
            }
            drop(self.lifeline.take());
            // The group's ID stays reserved while it has members, the
            // unreaped guardian included, so it can only name this group.
            let deadline = Instant::now() + GRACE;
            let mut reaped = false;
            loop {
                reaped = reaped || !matches!(self.child.try_wait(), Ok(None));
                if reaped && unsafe { libc::killpg(group, 0) } != 0 {
                    break;
                }
                if Instant::now() >= deadline {
                    unsafe {
                        libc::killpg(group, libc::SIGKILL);
                    }
                    break;
                }
                thread::sleep(Duration::from_millis(20));
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

    /// Asks the connector to stop without waiting for it, so several can
    /// stop at once; dropping it waits.
    pub fn stop_begin(&mut self) {
        drop(self.stop.take());
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

    fn alive(pid: libc::pid_t) -> bool {
        unsafe { libc::kill(pid, 0) == 0 }
    }

    /// A connector that ignores SIGTERM, as does the child it starts; the
    /// marker names both once they run.
    fn stubborn(dir: &std::path::Path) -> (Spec, PathBuf) {
        std::fs::create_dir_all(dir).unwrap();
        let marker = dir.join("pids");
        let script = format!(
            "trap '' TERM; sleep 60 & echo \"$$ $!\" > {m}.tmp && mv {m}.tmp {m}; wait",
            m = marker.display()
        );
        let spec = Spec {
            argv: vec!["/bin/sh".into(), "-c".into(), script],
            env: Vec::new(),
            log: dir.join("connector.log"),
        };
        (spec, marker)
    }

    fn pids(marker: &std::path::Path) -> Vec<libc::pid_t> {
        wait_for("the connector to start", || marker.exists());
        let text = std::fs::read_to_string(marker).unwrap();
        let pids: Vec<libc::pid_t> = text
            .split_whitespace()
            .map(|p| p.parse().unwrap())
            .collect();
        assert_eq!(pids.len(), 2, "{text}");
        assert!(pids.iter().all(|&pid| alive(pid)));
        pids
    }

    #[test]
    fn stop_kills_what_ignores_sigterm_within_the_grace() {
        let dir = std::env::temp_dir().join(format!("wh-connector-stop-{}", std::process::id()));
        let (spec, marker) = stubborn(&dir);
        let connector = Connector::start(spec, wake).unwrap();
        let pids = pids(&marker);
        let started = Instant::now();
        drop(connector);
        assert!(
            started.elapsed() < GRACE + Duration::from_secs(2),
            "{:?}",
            started.elapsed()
        );
        wait_for("the connector and its child to go", || {
            !pids.iter().any(|&pid| alive(pid))
        });
        let _ = std::fs::remove_dir_all(&dir);
    }

    // Wheelhouse ending without stopping its connectors closes the lifeline,
    // as dropping it here does; the guardian ends the group.
    #[test]
    fn closing_the_lifeline_ends_the_connector_and_its_children() {
        let dir =
            std::env::temp_dir().join(format!("wh-connector-lifeline-{}", std::process::id()));
        let (spec, marker) = stubborn(&dir);
        let log = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&spec.log)
            .unwrap();
        let mut run = Run::spawn(&spec, &log).unwrap();
        let pids = pids(&marker);
        let closed = Instant::now();
        drop(run.lifeline.take());
        wait_for("the connector and its child to go", || {
            !pids.iter().any(|&pid| alive(pid))
        });
        assert!(
            closed.elapsed() < GRACE + Duration::from_secs(3),
            "{:?}",
            closed.elapsed()
        );
        // The guardian went with them.
        let status = run.child.wait().unwrap();
        assert!(!status.success(), "{status}");
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
