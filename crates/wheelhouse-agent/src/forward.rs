//! A producer's socket on an ssh destination, forwarded to one here with
//! OpenSSH's unix-socket forwarding (wheelhouse#737), so the far host needs
//! nothing but sshd. The user's ssh configuration is the authentication, as
//! for the Cleat ssh route.
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use std::{env, fs, io, process, thread};

use tokio::time::sleep;

/// Longest ssh may take to connect, authenticate and listen.
const FORWARD_WITHIN: Duration = Duration::from_secs(20);
const POLL: Duration = Duration::from_millis(20);
/// How long a transport error waits for ssh to say why.
const HEARD_WITHIN: Duration = Duration::from_millis(250);
/// The longest socket path every platform binds: `sun_path` is 104 bytes on
/// macOS, with its NUL.
const PATH_MOST: usize = 103;

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
        let name = format!("{}-{}.sock", process::id(), NEXT.fetch_add(1, Ordering::Relaxed));
        let runtime = env::var_os("XDG_RUNTIME_DIR").filter(|dir| !dir.is_empty()).map(PathBuf::from);
        let local = local_path(runtime, &name)?;
        Ok(Forward { host: host.to_owned(), remote: remote.to_owned(), local, state: Default::default(), failures: AtomicU64::new(0) })
    }

    /// Where the forward listens here.
    pub(crate) fn local(&self) -> &Path {
        &self.local
    }

    /// Starts ssh unless it is running, and waits for it to forward. Ok has
    /// how many lines ssh had said, for [`Forward::why`]. One attempt runs
    /// at a time, up to [`FORWARD_WITHIN`], and every request to the
    /// producer waits on it; those that waited on one that fails share its
    /// reason rather than start another.
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
        // terminal to ask on. Keepalives end an ssh whose network went away
        // silently, which the next request then starts again.
        let mut child = Command::new(&program)
            .args(["-T", "-N", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes"])
            .args(["-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3", "-L"])
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

/// Where a forward listens: `name` under the runtime directory, or this
/// process's scratch directory when that path wouldn't do.
fn local_path(runtime: Option<PathBuf>, name: &str) -> io::Result<PathBuf> {
    if let Some(local) = runtime.map(|dir| dir.join("wheelhouse-agent").join(name)).filter(|local| usable(local)) {
        fs::DirBuilder::new().recursive(true).mode_private().create(local.parent().unwrap())?;
        return Ok(local);
    }
    let local = scratch_dir()?.join(name);
    if !usable(&local) {
        return Err(io::Error::other(format!("no socket path for an ssh forward fits -L and sun_path: {}", local.display())));
    }
    Ok(local)
}

/// A local socket path `-L` reads whole (it splits on `:`) and every
/// platform binds.
fn usable(local: &Path) -> bool {
    let text = local.to_string_lossy();
    !text.contains(':') && text.len() <= PATH_MOST
}

/// This process's own directory for sockets when there is no usable
/// runtime directory. Under `/tmp`, as macOS's temporary directory is too
/// long for `sun_path`; made afresh, so no one else can have made it first.
fn scratch_dir() -> io::Result<&'static Path> {
    static DIR: OnceLock<PathBuf> = OnceLock::new();
    if let Some(dir) = DIR.get() {
        return Ok(dir);
    }
    let base = if cfg!(unix) { PathBuf::from("/tmp") } else { env::temp_dir() };
    let mut attempt = 0u32;
    let dir = loop {
        let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().subsec_nanos();
        let dir = base.join(format!("wheelhouse-agent-{}-{nanos:x}", process::id()));
        match fs::DirBuilder::new().mode_private().create(&dir) {
            Ok(()) => break dir,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists && attempt < 8 => attempt += 1,
            Err(error) => return Err(error),
        }
    };
    Ok(DIR.get_or_init(|| dir))
}

trait Private {
    fn mode_private(&mut self) -> &mut Self;
}

impl Private for fs::DirBuilder {
    fn mode_private(&mut self) -> &mut Self {
        #[cfg(unix)]
        std::os::unix::fs::DirBuilderExt::mode(self, 0o700);
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // A runtime directory whose socket path ssh's -L would split, or that
    // is too long to bind, gives way to the scratch directory (#741).
    #[test]
    fn a_forward_listens_where_ssh_and_the_platform_can_both_take_the_path() {
        let base = env::temp_dir().join(format!("wh-forward-{}", process::id()));
        let fine = local_path(Some(base.join("run")), "1-0.sock").unwrap();
        assert!(fine.starts_with(base.join("run")));
        for runtime in [base.join("a:b"), base.join("x".repeat(PATH_MOST))] {
            let local = local_path(Some(runtime.clone()), "1-0.sock").unwrap();
            assert!(!local.starts_with(&runtime) && usable(&local), "{} for {}", local.display(), runtime.display());
        }
        assert!(local_path(None, "1-0.sock").unwrap().starts_with(scratch_dir().unwrap()));
        let _ = fs::remove_dir_all(base);
    }
}
