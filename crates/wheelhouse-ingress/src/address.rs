//! Where the Automation Endpoint listens when no launcher names an address,
//! and how a launcher learns it (#554, ADR 0011). Each instance has its own
//! logical address, `wheelhouse/<instance-uuid>`: a Unix socket under the
//! runtime directory, or a named pipe on Windows. There is no fixed global
//! name, so no instance takes over another's endpoint.
use std::{
    hash::{BuildHasher, Hasher},
    path::{Path, PathBuf},
};

/// A random version 4 UUID naming this instance. Uniqueness is all it needs:
/// the endpoint is protected by the private directory or the pipe's DACL,
/// not by being hard to guess.
pub fn instance_uuid() -> String {
    let random = || {
        let mut hasher = std::collections::hash_map::RandomState::new().build_hasher();
        hasher.write_u32(std::process::id());
        hasher.write_u128(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos()));
        hasher.finish()
    };
    let mut bytes = [0u8; 16];
    bytes[..8].copy_from_slice(&random().to_le_bytes());
    bytes[8..].copy_from_slice(&random().to_le_bytes());
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    format!("{}-{}-{}-{}-{}", &hex[..8], &hex[8..12], &hex[12..16], &hex[16..20], &hex[20..])
}

/// The longest Unix socket path, NUL excluded (`sun_path` is 104 bytes on
/// macOS and the BSDs, 108 on Linux).
#[cfg(unix)]
const SOCKET_PATH_MAX: usize = if cfg!(any(target_os = "linux", target_os = "android")) { 107 } else { 103 };

/// The directory this user's instances listen in: `$XDG_RUNTIME_DIR/wheelhouse`,
/// or, without a runtime directory, `wheelhouse-<uid>` in the temporary
/// directory, which other users share; `/tmp` when the temporary directory's
/// path leaves no room for a socket in it (a long macOS `$TMPDIR`).
#[cfg(unix)]
fn runtime_dir(runtime: Option<std::ffi::OsString>, temp: PathBuf, uid: u32) -> PathBuf {
    // Room for the slash and the UUID after it.
    let fits = |dir: &PathBuf| dir.as_os_str().len() + 37 <= SOCKET_PATH_MAX;
    let runtime = runtime.map(PathBuf::from).filter(|dir| dir.is_absolute()).map(|dir| dir.join("wheelhouse"));
    let temp = temp.join(format!("wheelhouse-{uid}"));
    runtime.into_iter().chain([temp]).find(fits).unwrap_or_else(|| PathBuf::from(format!("/tmp/wheelhouse-{uid}")))
}

/// Creates `dir` private to this user (mode 0700), or checks that it already
/// is; the listener refuses any other parent.
#[cfg(unix)]
fn private_dir(dir: &Path) -> Result<(), String> {
    use std::os::unix::fs::{DirBuilderExt, MetadataExt};
    match std::fs::DirBuilder::new().mode(0o700).create(dir) {
        Ok(()) => {}
        Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {}
        Err(e) => return Err(format!("{}: {e}", dir.display())),
    }
    let meta = std::fs::symlink_metadata(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    if !meta.is_dir() || meta.mode() & 0o077 != 0 || meta.uid() != unsafe { libc::geteuid() } {
        return Err(format!("{} must be a directory owned by this user and private (mode 0700)", dir.display()));
    }
    Ok(())
}

/// This instance's default address, its directory made ready on Unix.
pub fn default_address() -> Result<PathBuf, String> {
    #[cfg(unix)]
    {
        let dir = runtime_dir(std::env::var_os("XDG_RUNTIME_DIR"), std::env::temp_dir(), unsafe { libc::geteuid() });
        private_dir(&dir)?;
        Ok(dir.join(instance_uuid()))
    }
    #[cfg(windows)]
    {
        Ok(PathBuf::from(format!(r"\\.\pipe\wheelhouse\{}", instance_uuid())))
    }
}

/// Writes `address` to `path` for a launcher to read: the whole file at once,
/// so a reader never sees part of it. An empty address says there is none.
pub fn write_address_file(path: &Path, address: &str) -> Result<(), String> {
    let name = path.file_name().ok_or_else(|| format!("{} names no file", path.display()))?;
    let mut temporary = name.to_owned();
    temporary.push(format!(".{}.tmp", std::process::id()));
    let temporary = path.with_file_name(temporary);
    std::fs::write(&temporary, address).and_then(|()| std::fs::rename(&temporary, path)).map_err(|e| {
        let _ = std::fs::remove_file(&temporary);
        format!("{}: {e}", path.display())
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn instances_get_distinct_version_4_uuids() {
        let ids: std::collections::HashSet<String> = (0..1000).map(|_| instance_uuid()).collect();
        assert_eq!(ids.len(), 1000);
        for id in ids.iter().take(10) {
            let groups: Vec<_> = id.split('-').map(str::len).collect();
            assert_eq!(groups, [8, 4, 4, 4, 12], "{id}");
            assert!(id.chars().all(|c| c == '-' || c.is_ascii_hexdigit()), "{id}");
            assert_eq!(&id[14..15], "4", "{id}");
            assert!("89ab".contains(&id[19..20]), "{id}");
        }
    }

    #[cfg(unix)]
    #[test]
    fn the_runtime_directory_is_preferred_and_the_shared_temporary_one_is_per_user() {
        let temp = PathBuf::from("/var/folders/xy/T");
        let dir = |runtime: Option<&str>| runtime_dir(runtime.map(Into::into), temp.clone(), 7);
        assert_eq!(dir(Some("/run/user/7")), PathBuf::from("/run/user/7/wheelhouse"));
        assert_eq!(dir(None), PathBuf::from("/var/folders/xy/T/wheelhouse-7"));
        // A relative runtime directory is ignored, as the XDG spec requires.
        assert_eq!(dir(Some("relative")), PathBuf::from("/var/folders/xy/T/wheelhouse-7"));
    }

    // A socket path is at most 104 bytes on macOS: a directory that leaves
    // no room for the address is passed over, ending at /tmp, rather than
    // failing to bind.
    #[cfg(unix)]
    #[test]
    fn a_default_address_fits_a_socket_path() {
        let long = PathBuf::from(format!("/{}", "t".repeat(SOCKET_PATH_MAX - 40)));
        assert_eq!(runtime_dir(Some(long.clone().into()), long.clone(), 7), PathBuf::from("/tmp/wheelhouse-7"));
        let mac = PathBuf::from("/var/folders/zz/zyxvpxvq6csfxvn_n0000000000000/T/");
        assert_eq!(runtime_dir(Some(long.into()), mac.clone(), 501), mac.join("wheelhouse-501"));
        for temp in [mac, std::env::temp_dir()] {
            let address = runtime_dir(None, temp, u32::MAX).join(instance_uuid());
            assert!(address.as_os_str().len() <= SOCKET_PATH_MAX, "{}", address.display());
        }
    }

    #[cfg(unix)]
    #[test]
    fn the_directory_is_made_private_and_a_shared_one_is_refused() {
        use std::os::unix::fs::PermissionsExt;
        let root = std::env::temp_dir().join(format!("wheelhouse-address-test-{}", instance_uuid()));
        std::fs::create_dir(&root).unwrap();
        let dir = root.join("wheelhouse");
        private_dir(&dir).unwrap();
        assert_eq!(std::fs::metadata(&dir).unwrap().permissions().mode() & 0o777, 0o700);
        private_dir(&dir).unwrap(); // Already there: still fine.
        std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o755)).unwrap();
        assert!(private_dir(&dir).is_err());
        let file = root.join("file");
        std::fs::write(&file, "").unwrap();
        assert!(private_dir(&file).is_err());
        std::fs::remove_dir_all(&root).unwrap();
    }

    #[test]
    fn the_address_file_holds_exactly_the_address() {
        let root = std::env::temp_dir().join(format!("wheelhouse-address-file-{}", instance_uuid()));
        std::fs::create_dir(&root).unwrap();
        let path = root.join("address");
        write_address_file(&path, "/run/user/7/wheelhouse/x").unwrap();
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "/run/user/7/wheelhouse/x");
        write_address_file(&path, "").unwrap();
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "");
        assert_eq!(std::fs::read_dir(&root).unwrap().count(), 1, "no temporary file is left");
        assert!(write_address_file(&root.join("missing/address"), "x").is_err());
        assert_eq!(std::fs::read_dir(&root).unwrap().count(), 1);
        std::fs::remove_dir_all(&root).unwrap();
    }
}
