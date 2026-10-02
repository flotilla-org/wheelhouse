//! ADR 0011: protected local byte pipes; never take over an existing name.
use std::{
    io,
    mem::size_of,
    os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle},
    path::{Path, PathBuf},
    ptr,
};
use tokio::net::windows::named_pipe::{NamedPipeServer, PipeMode, ServerOptions};
use windows_sys::Win32::{
    Foundation::LocalFree,
    Security::{
        Authorization::{
            ConvertSidToStringSidW, ConvertStringSecurityDescriptorToSecurityDescriptorW,
            SDDL_REVISION_1,
        },
        GetTokenInformation, TokenUser, SECURITY_ATTRIBUTES, TOKEN_QUERY, TOKEN_USER,
    },
    System::Threading::{GetCurrentProcess, OpenProcessToken},
};

struct Descriptor(*mut std::ffi::c_void);
// SAFETY: immutable LocalAlloc block, only read by pipe creation.
unsafe impl Send for Descriptor {}
impl Drop for Descriptor {
    fn drop(&mut self) {
        unsafe {
            LocalFree(self.0);
        }
    }
}
fn check(ok: i32) -> io::Result<()> {
    if ok == 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}
impl Descriptor {
    fn current_user() -> io::Result<Self> {
        let mut raw = ptr::null_mut();
        // SAFETY: current process pseudo handle; returned token is owned below.
        check(unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut raw) })?;
        let token = unsafe { OwnedHandle::from_raw_handle(raw) };
        let mut len = 0;
        unsafe {
            GetTokenInformation(
                token.as_raw_handle(),
                TokenUser,
                ptr::null_mut(),
                0,
                &mut len,
            );
        }
        if len == 0 {
            return Err(io::Error::last_os_error());
        }
        let mut buffer = vec![0u64; (len as usize).div_ceil(size_of::<u64>())];
        // SAFETY: aligned buffer with at least len writable bytes.
        check(unsafe {
            GetTokenInformation(
                token.as_raw_handle(),
                TokenUser,
                buffer.as_mut_ptr().cast(),
                len,
                &mut len,
            )
        })?;
        let mut sid = ptr::null_mut();
        check(unsafe {
            ConvertSidToStringSidW((*buffer.as_ptr().cast::<TOKEN_USER>()).User.Sid, &mut sid)
        })?;
        let count = (0..).take_while(|&i| unsafe { *sid.add(i) } != 0).count();
        let user = String::from_utf16_lossy(unsafe { std::slice::from_raw_parts(sid, count) });
        unsafe {
            LocalFree(sid.cast());
        }
        let sddl: Vec<u16> = format!("D:P(A;;GA;;;SY)(A;;GA;;;{user})")
            .encode_utf16()
            .chain(Some(0))
            .collect();
        let mut descriptor = ptr::null_mut();
        check(unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.as_ptr(),
                SDDL_REVISION_1,
                &mut descriptor,
                ptr::null_mut(),
            )
        })?;
        Ok(Self(descriptor))
    }
    fn instance(&self, name: &Path, first: bool) -> io::Result<NamedPipeServer> {
        let mut attributes = SECURITY_ATTRIBUTES {
            nLength: size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: self.0,
            bInheritHandle: 0,
        };
        // SAFETY: attributes and its descriptor remain valid throughout creation.
        unsafe {
            ServerOptions::new()
                .pipe_mode(PipeMode::Byte)
                .reject_remote_clients(true)
                .first_pipe_instance(first)
                .create_with_security_attributes_raw(
                    name,
                    (&mut attributes as *mut SECURITY_ATTRIBUTES).cast(),
                )
        }
    }
}

pub(super) struct Listener {
    name: PathBuf,
    descriptor: Descriptor,
    pending: NamedPipeServer,
}
impl Listener {
    pub(super) fn bind(name: &Path) -> io::Result<Self> {
        let text = name
            .to_str()
            .ok_or_else(|| io::Error::other("pipe name must be UTF-8"))?;
        if !text.starts_with(r"\\.\pipe\") || text.len() <= 9 || text.contains('\0') {
            return Err(io::Error::other(
                r"expected a local \\.\pipe\<name> endpoint",
            ));
        }
        let descriptor = Descriptor::current_user()?;
        let pending = descriptor.instance(name, true)?;
        Ok(Self {
            name: name.into(),
            descriptor,
            pending,
        })
    }
}
impl axum::serve::Listener for Listener {
    type Io = NamedPipeServer;
    type Addr = ();
    async fn accept(&mut self) -> (Self::Io, Self::Addr) {
        loop {
            if let Err(error) = self.pending.connect().await {
                eprintln!("wheelhouse ingress: pipe connect failed: {error}");
                let _ = self.pending.disconnect();
                tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                continue;
            }
            // Keep a listening instance alive before handing the connected one
            // to HTTP, so the address never disappears between clients.
            let mut reported = false;
            loop {
                match self.descriptor.instance(&self.name, false) {
                    Ok(next) => return (std::mem::replace(&mut self.pending, next), ()),
                    Err(error) => {
                        if !reported {
                            eprintln!(
                                "wheelhouse ingress: cannot create next pipe instance: {error}"
                            );
                            reported = true;
                        }
                        tokio::time::sleep(std::time::Duration::from_millis(250)).await;
                    }
                }
            }
        }
    }
    fn local_addr(&self) -> io::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ADR 0011: a second listener must refuse an already-served pipe name.
    // This lifecycle scenario uses real Windows pipe handles.
    #[tokio::test(flavor = "current_thread")]
    async fn duplicate_bind_preserves_the_original_listener() {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let name = PathBuf::from(format!(
            r"\\.\pipe\wheelhouse-bind-{}-{nonce}",
            std::process::id()
        ));
        let owner = Listener::bind(&name).unwrap();
        assert!(Listener::bind(&name).is_err());
        drop(owner);
        assert!(Listener::bind(&name).is_ok());
    }
}
