//! A workspace's lifecycle and home (#631). Lifecycle intents
//! (WHEELHOUSE_INTENT_NEW_WORKSPACE, _CLOSE_WORKSPACE, _SET_WORKSPACE_HOME)
//! go through wheelhouse_apply_intent, and their effects come with
//! wheelhouse_effects_take; these calls read what they keep, and seed a
//! record written before it existed.
use super::*;
use wheelhouse_core::lifecycle::{EffectiveHome, Home};

/// A workspace's home: none (a subject workspace), a local group's ID, or a
/// project's.
pub const WHEELHOUSE_HOME_NONE: u32 = 0;
pub const WHEELHOUSE_HOME_GROUP: u32 = 1;
pub const WHEELHOUSE_HOME_PROJECT: u32 = 2;

/// A workspace's lifecycle part, as the core keeps it.
#[repr(C)]
pub struct WorkspaceLifeView {
    /// 0 while unknown: a record written before the part existed, or a
    /// workspace the host registered itself, which the host seeds
    /// (wheelhouse_workspace_seed_life).
    pub kept_known: u32,
    /// Closed and kept (a Kept Workspace: native's `detached_workspace`).
    pub kept: u32,
    /// Its home's two fields: the group it lives in, and the project it
    /// lives with, which wins (empty: none).
    pub group: Text,
    pub project: Text,
    /// Its one home (WHEELHOUSE_HOME_*): the project, else the group while
    /// it exists, else the default group; none for a subject workspace.
    pub home_kind: u32,
    pub home: Text,
}

#[repr(C)]
struct OwnedLife {
    value: WorkspaceLifeView,
    texts: [String; 3],
}

/// A registered workspace's lifecycle part. Free it with
/// wheelhouse_workspace_life_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_workspace_life(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    error_out: *mut *mut c_char,
) -> *mut WorkspaceLifeView {
    run(h, error_out, |h| {
        let (life, home) = h.sidebar.workspace_life(workspace.into())?;
        let (home_kind, home) = match home {
            None => (WHEELHOUSE_HOME_NONE, String::new()),
            Some(EffectiveHome::Group(group)) => (WHEELHOUSE_HOME_GROUP, group),
            Some(EffectiveHome::Project(project)) => (WHEELHOUSE_HOME_PROJECT, project),
        };
        let texts = [life.home.group.unwrap_or_default(), life.home.project.unwrap_or_default(), home];
        let value = WorkspaceLifeView {
            kept_known: life.kept.is_some() as u32,
            kept: life.kept.unwrap_or(false) as u32,
            group: Text::borrowed(&texts[0]),
            project: Text::borrowed(&texts[1]),
            home_kind,
            home: Text::borrowed(&texts[2]),
        };
        // The texts' heap buffers don't move with the box.
        Ok(Box::into_raw(Box::new(OwnedLife { value, texts })).cast::<WorkspaceLifeView>())
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_workspace_life_free(life: *mut WorkspaceLifeView) {
    if !life.is_null() {
        drop(Box::from_raw(life.cast::<OwnedLife>()));
    }
}

/// The generation of every workspace's lifecycle part, and of which are
/// registered: it changes whenever any does. Cheap enough to read every
/// frame; a home's effective group also follows the Dashboard's local
/// groups. 0 on failure.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_life_generation(h: *mut WheelhouseCore, error_out: *mut *mut c_char) -> u64 {
    run(h, error_out, |h| Ok(h.sidebar.life_generation())).unwrap_or(0)
}

/// Seed the lifecycle part of a record that has none (kept_known 0) from
/// what the host has: whether it is kept, and its home's group and project
/// (empty: none). 1 if seeded; 0 with no error when it already had one;
/// after that the core's is the truth.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_workspace_seed_life(
    h: *mut WheelhouseCore,
    workspace: WorkspaceIdView,
    kept: u32,
    group: Text,
    project: Text,
    error_out: *mut *mut c_char,
) -> u32 {
    run(h, error_out, |h| {
        let home = Home { group: Some(group.read()?), project: Some(project.read()?) };
        h.sidebar.seed_life(workspace.into(), kept != 0, home)
    })
    .unwrap_or(false) as u32
}
