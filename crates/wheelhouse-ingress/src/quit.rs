//! SIGTERM, SIGINT and SIGHUP ask Wheelhouse to quit as its Exit command
//! does, so it stops its connectors (connector.rs) and saves on the way out
//! rather than dying where it stands. A second such signal ends it at once.
//!
//! The handler only counts and writes a byte to a pipe; a thread of its own
//! reads it and wakes the UI thread, which checks `signalled` each frame.
use std::sync::atomic::{AtomicI32, AtomicU32, Ordering};

static SIGNALS: AtomicU32 = AtomicU32::new(0);
static PIPE: AtomicI32 = AtomicI32::new(-1);
const HANDLED: [libc::c_int; 3] = [libc::SIGTERM, libc::SIGINT, libc::SIGHUP];

extern "C" fn handle(signal: libc::c_int) {
    // Only async-signal-safe calls here.
    if SIGNALS.fetch_add(1, Ordering::SeqCst) > 0 {
        unsafe {
            libc::signal(signal, libc::SIG_DFL);
            libc::raise(signal);
        }
        return;
    }
    // The handler interrupts whatever the thread was doing; `write` can set
    // errno, which that code may be about to read.
    let saved = unsafe { *errno() };
    let byte = 1u8;
    unsafe {
        libc::write(PIPE.load(Ordering::SeqCst), &byte as *const u8 as *const _, 1);
        *errno() = saved;
    }
}

#[cfg(any(target_os = "macos", target_os = "ios"))]
unsafe fn errno() -> *mut libc::c_int {
    unsafe { libc::__error() }
}

#[cfg(not(any(target_os = "macos", target_os = "ios")))]
unsafe fn errno() -> *mut libc::c_int {
    unsafe { libc::__errno_location() }
}

/// Installs the handlers once; `wake` runs on a background thread when the
/// first signal arrives.
pub fn install(wake: super::Wake) -> std::io::Result<()> {
    if PIPE.load(Ordering::SeqCst) >= 0 {
        return Ok(());
    }
    let (mut reader, writer) = std::io::pipe()?;
    std::thread::Builder::new().name("wheelhouse-quit-signals".into()).spawn(move || {
        use std::io::Read;
        let mut byte = [0u8; 1];
        if matches!(reader.read(&mut byte), Ok(1)) {
            wake();
        }
    })?;
    use std::os::fd::IntoRawFd;
    PIPE.store(writer.into_raw_fd(), Ordering::SeqCst);
    for signal in HANDLED {
        unsafe {
            let mut action: libc::sigaction = std::mem::zeroed();
            action.sa_sigaction = handle as extern "C" fn(libc::c_int) as libc::sighandler_t;
            action.sa_flags = libc::SA_RESTART;
            libc::sigemptyset(&mut action.sa_mask);
            if libc::sigaction(signal, &action, std::ptr::null_mut()) != 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
    }
    Ok(())
}

/// Whether a quit signal has arrived.
pub fn signalled() -> bool {
    SIGNALS.load(Ordering::SeqCst) > 0
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::AtomicBool;

    static WOKEN: AtomicBool = AtomicBool::new(false);
    extern "C" fn wake() {
        WOKEN.store(true, Ordering::SeqCst);
    }

    #[test]
    fn first_signal_asks_to_quit_and_wakes() {
        install(wake).unwrap();
        assert!(!signalled());
        unsafe {
            libc::raise(libc::SIGHUP);
        }
        assert!(signalled());
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while !WOKEN.load(Ordering::SeqCst) {
            assert!(std::time::Instant::now() < deadline, "not woken");
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
    }
}
