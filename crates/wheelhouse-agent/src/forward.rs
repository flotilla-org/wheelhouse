//! A producer's socket on an ssh destination, forwarded to one here with
//! OpenSSH's unix-socket forwarding (wheelhouse#737), so the far host needs
//! nothing but sshd. The user's ssh configuration is the authentication, as
//! for the Cleat ssh route.
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::{env, fs, io, process, thread};

use tokio::time::sleep;

/// Longest ssh may take to connect, authenticate and listen.
const FORWARD_WITHIN: Duration = Duration::from_secs(20);
const POLL: Duration = Duration::from_millis(20);
/// How long a transport error waits for ssh to say why.
const HEARD_WITHIN: Duration = Duration::from_millis(250);

pub(crate) struct Forward {
    host: String,
    remote: String,
    local: PathBuf,
    state: tokio::sync::Mutex<State>,
    /// Attempts to forward that have ended in failure.
    failures: AtomicU64,
}

#[derive(Default)]
struct State {
    ssh: Option<Ssh>,
    failed: Option<String>,
}

struct Ssh {
    child: Child,
    started: Instant,
    forwarding: bool,
    said: Arc<Mutex<Said>>,
}

/// What ssh has written to stderr.
#[derive(Default)]
struct Said {
    lines: u64,
    last: String,
    done: bool,
}

impl Forward {
    pub(crate) fn new(host: &str, remote: &str) -> io::Result<Forward> {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let dir = match env::var_os("XDG_RUNTIME_DIR") {
            Some(runtime) if !runtime.is_empty() => PathBuf::from(runtime).join("wheelhouse-agent"),
            _ => env::temp_dir().join(format!("wheelhouse-agent-{}", process::id())),
        };
        private_dir(&dir)?;
        let local = dir.join(format!("{}-{}.sock", process::id(), NEXT.fetch_add(1, Ordering::Relaxed)));
        Ok(Forward { host: host.to_owned(), remote: remote.to_owned(), local, state: Default::default(), failures: AtomicU64::new(0) })
    }

    /// Where the forward listens here.
    pub(crate) fn local(&self) -> &Path {
        &self.local
    }

    /// Starts ssh unless it is running, and waits for it to forward. Ok has
    /// how many lines ssh had said, for [`Forward::why`]. Callers that wait
    /// on an attempt that fails share its reason rather than start another.
    pub(crate) async fn ready(&self) -> Result<u64, String> {
        let failures = self.failures.load(Ordering::Acquire);
        let mut state = self.state.lock().await;
        if state.ssh.is_none() {
            if let Some(why) = state.failed.as_ref().filter(|_| self.failures.load(Ordering::Acquire) != failures) {
                return Err(why.clone());
            }
            match self.spawn() {
                Ok(ssh) => state.ssh = Some(ssh),
                Err(why) => return Err(self.fail(&mut state, why)),
            }
        }
        let ssh = state.ssh.as_mut().unwrap();
        match self.forwarding(ssh).await {
            Ok(()) => Ok(ssh.said.lock().unwrap().lines),
            Err(why) => {
                if let Some(mut ssh) = state.ssh.take() {
                    end(&mut ssh.child);
                }
                Err(self.fail(&mut state, why))
            }
        }
    }

    /// A request's transport error, with what ssh said since `heard`: a
    /// producer that refuses the forwarded connection shows only there.
    pub(crate) async fn why(&self, heard: u64, error: String) -> String {
        // Held, ssh is starting over and has nothing to say of this request.
        let Some(said) = self.state.try_lock().ok().and_then(|state| state.ssh.as_ref().map(|ssh| ssh.said.clone())) else {
            return error;
        };
        let deadline = Instant::now() + HEARD_WITHIN;
        loop {
            {
                let said = said.lock().unwrap();
                if said.lines > heard {
                    return format!("{error}: {}", said.last);
                }
            }
            if Instant::now() >= deadline {
                return error;
            }
            sleep(POLL).await;
        }
    }

    fn fail(&self, state: &mut State, why: String) -> String {
        state.failed = Some(why.clone());
        self.failures.fetch_add(1, Ordering::AcqRel);
        why
    }

    fn spawn(&self) -> Result<Ssh, String> {
        // A socket left by an earlier ssh would read as this one listening.
        if let Err(error) =
            fs::remove_file(&self.local).or_else(|error| if error.kind() == io::ErrorKind::NotFound { Ok(()) } else { Err(error) })
        {
            return Err(format!("can't clear {}: {error}", self.local.display()));
        }
        let program = env::var_os("WHEELHOUSE_SSH").unwrap_or_else(|| "ssh".into());
        // BatchMode fails with a reason instead of prompting: there is no
        // terminal to ask on.
        let mut child = Command::new(&program)
            .args(["-T", "-N", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes", "-L"])
            .arg(format!("{}:{}", self.local.display(), self.remote))
            .arg("--")
            .arg(&self.host)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|error| format!("can't run {}: {error}", program.to_string_lossy()))?;
        let said = Arc::new(Mutex::new(Said::default()));
        let stderr = child.stderr.take().unwrap();
        let heard = said.clone();
        thread::Builder::new()
            .name("wheelhouse-agent-ssh".into())
            .spawn(move || {
                for line in BufReader::new(stderr).lines().map_while(Result::ok) {
                    let line = line.trim();
                    if !line.is_empty() {
                        let mut said = heard.lock().unwrap();
                        said.lines += 1;
                        said.last = line.to_owned();
                    }
                }
                heard.lock().unwrap().done = true;
            })
            .map_err(|error| error.to_string())?;
        Ok(Ssh { child, started: Instant::now(), forwarding: false, said })
    }

    /// Ok once ssh listens here, for as long as it runs.
    async fn forwarding(&self, ssh: &mut Ssh) -> Result<(), String> {
        loop {
            if let Some(status) = ssh.child.try_wait().map_err(|error| error.to_string())? {
                // Its last words may still be on their way.
                let deadline = Instant::now() + HEARD_WITHIN;
                while !ssh.said.lock().unwrap().done && Instant::now() < deadline {
                    sleep(POLL).await;
                }
                let said = ssh.said.lock().unwrap();
                return Err(if said.last.is_empty() { format!("ssh to {} exited with {status}", self.host) } else { said.last.clone() });
            }
            if ssh.forwarding {
                return Ok(());
            }
            if self.local.exists() {
                ssh.forwarding = true;
                return Ok(());
            }
            if ssh.started.elapsed() >= FORWARD_WITHIN {
                return Err(format!("ssh to {} didn't forward within {}s", self.host, FORWARD_WITHIN.as_secs()));
            }
            sleep(POLL).await;
        }
    }
}

impl Drop for Forward {
    fn drop(&mut self) {
        if let Some(mut ssh) = self.state.get_mut().ssh.take() {
            end(&mut ssh.child);
        }
        let _ = fs::remove_file(&self.local);
    }
}

fn end(child: &mut Child) {
    let _ = child.kill();
    let _ = child.wait();
}

fn private_dir(dir: &Path) -> io::Result<()> {
    let mut builder = fs::DirBuilder::new();
    builder.recursive(true);
    #[cfg(unix)]
    std::os::unix::fs::DirBuilderExt::mode(&mut builder, 0o700);
    builder.create(dir)
}
