//! Test-only: the native shell's C arrangement and docking rules, as they
//! were when the core took them over (#516), behind the core's own types,
//! so that the equivalence tests (tests/) can apply each edit to both and
//! compare. The C is native/src/shell/tests/arrangement_oracle.c, built by
//! build.rs on Linux and macOS (where CI runs `cargo test`); elsewhere this
//! crate is empty.
//!
//! Tabs are numbers from 1. Every call holds one lock: the oracle keeps its
//! inputs (hidden tabs, registrations, counts) in C globals.
#![cfg(oracle)]
use std::ffi::c_void;
use std::sync::{Mutex, MutexGuard};

use wheelhouse_core::arrangement::{Arrangement, Dir, Edit, Outcome, Panel, PanelId};
use wheelhouse_core::dock::{Host, HostKind, Proposal, Registration, Rule, SIDEBAR_SECTION};
use wheelhouse_core::suggested_layout::Axis;

pub type Tab = u64;

#[repr(C)]
#[derive(Clone, Copy)]
struct OracleHost {
    kind: u32,
    has_workspace_subject: u32,
    controlled_split: u64,
    workspace_region: u64,
    level: u64,
    available_width: f32,
    pad: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct OracleProposal {
    host: OracleHost,
    selected_workspace_region: u64,
    view_level: u64,
    instances_after: u32,
    control_surfaces_after: u32,
    closing: u32,
    pad: u32,
}

#[repr(C)]
struct OracleRegistration {
    traits: u32,
    minimum_width: f32,
    default_host: u32,
    sidebar_section: u32,
}

extern "C" {
    fn oracle_new() -> *mut c_void;
    fn oracle_free(h: *mut c_void);
    fn oracle_set_named(h: *mut c_void, panel: u64, name: u64);
    fn oracle_set_hidden(tabs: *const u64, count: u64);
    fn oracle_dump(h: *mut c_void, out: *mut u64, cap: u64) -> u64;
    fn oracle_problem(h: *mut c_void, out: *mut u8, cap: u64) -> u64;
    fn oracle_quantize(weight: f32) -> f32;
    fn oracle_rule_message(rule: u32, out: *mut u8, cap: u64) -> u64;
    fn oracle_clear(h: *mut c_void) -> u64;
    fn oracle_discard(h: *mut c_void);
    fn oracle_split(h: *mut c_void, panel: u64, dir: i32) -> u64;
    fn oracle_add(h: *mut c_void, parent: u64, weight: f32) -> u64;
    fn oracle_remove(h: *mut c_void, panel: u64) -> u32;
    fn oracle_close(h: *mut c_void, panel: u64) -> u64;
    fn oracle_insert_tab(h: *mut c_void, tab: u64, to: u64, after: u64, select: u32) -> u32;
    fn oracle_detach_tab(h: *mut c_void, tab: u64) -> u32;
    fn oracle_remove_tab(h: *mut c_void, tab: u64) -> u32;
    fn oracle_select(h: *mut c_void, panel: u64, tab: u64) -> u32;
    fn oracle_reorder(h: *mut c_void, panel: u64, after: u64) -> u32;
    fn oracle_resize(h: *mut c_void, panel: u64, delta: f32, floor: f32);
    fn oracle_equalize(h: *mut c_void, panel: u64);
    fn oracle_readdress(h: *mut c_void, from: u64, to: u64) -> u32;
    fn oracle_settle(h: *mut c_void, panel: u64, dir: i32, may_close: u32, heir: *mut u64) -> u32;
    fn oracle_drop(h: *mut c_void, tab: u64, to: u64, dir: i32, after: u64, out: *mut u64);
    fn oracle_drop_across(h: *mut c_void, other: *mut c_void, tab: u64, to: u64, dir: i32, after: u64, out: *mut u64);
    fn oracle_set_registrations(r: *const OracleRegistration, count: u64);
    fn oracle_set_instances(enabled: u32, splits: *const u64, counts: *const u32, count: u64);
    fn oracle_check(registration: i64, p: OracleProposal) -> u32;
    fn oracle_placement(exists: u32, registration: i64, source: OracleHost, level: u64, destination: OracleHost) -> u32;
    fn oracle_creation(registration: i64, destination: OracleHost) -> u32;
    fn oracle_closure(exists: u32, registration: i64, source: OracleHost, level: u64) -> u32;
    fn oracle_view_level(registration: i64, host: OracleHost) -> u64;
    fn oracle_set_tab_registrations(tabs: *const u64, registrations: *const i64, count: u64);
    fn oracle_restore(
        arrangements: *const *mut c_void,
        hosts: *const OracleHost,
        count: u64,
        sidebar: i64,
        workspace: i64,
        dirty: *mut u32,
        pending: *mut u64,
        cap: u64,
    ) -> u64;
}

static LOCK: Mutex<()> = Mutex::new(());

fn lock() -> MutexGuard<'static, ()> {
    LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn dir(dir: Option<Dir>) -> i32 {
    match dir {
        None => -1,
        Some(Dir::Left) => 0,
        Some(Dir::Up) => 1,
        Some(Dir::Right) => 2,
        Some(Dir::Down) => 3,
    }
}

fn id(raw: u64) -> Option<PanelId> {
    (raw != 0).then_some(PanelId(raw))
}

fn set_hidden(hidden: &[Tab]) {
    unsafe { oracle_set_hidden(hidden.as_ptr(), hidden.len() as u64) }
}

/// The C's quantization of a weight.
pub fn quantize(weight: f32) -> f32 {
    let _guard = lock();
    unsafe { oracle_quantize(weight) }
}

/// The name a panel is given here: "n" and a number, as the C keeps it.
pub fn name(n: u64) -> String {
    format!("n{n}")
}

/// One C arrangement.
pub struct Oracle(*mut c_void);

// Each handle is used by one test at a time, under the lock.
unsafe impl Send for Oracle {}

impl Default for Oracle {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Oracle {
    fn drop(&mut self) {
        let _guard = lock();
        unsafe { oracle_free(self.0) }
    }
}

impl Oracle {
    pub fn new() -> Self {
        let _guard = lock();
        Self(unsafe { oracle_new() })
    }

    /// Gives `panel` a provider's name, [`name`]`(n)`, or none for 0.
    pub fn set_named(&mut self, panel: PanelId, n: u64) {
        let _guard = lock();
        unsafe { oracle_set_named(self.0, panel.0, n) }
    }

    fn dump_locked(&self) -> Arrangement<Tab> {
        let mut out = vec![0u64; 256];
        let len = unsafe { oracle_dump(self.0, out.as_mut_ptr(), out.len() as u64) } as usize;
        if len > out.len() {
            out.resize(len, 0);
            unsafe { oracle_dump(self.0, out.as_mut_ptr(), out.len() as u64) };
        }
        let mut words = out.into_iter();
        let mut next = || words.next().expect("the dump is whole");
        let next_id = next();
        let root_axis = if next() == 0 { Axis::Row } else { Axis::Column };
        let count = next() as usize;
        let mut panels: Vec<(Option<usize>, Panel<Tab>)> = vec![];
        for _ in 0..count {
            let mut panel = Panel::new(PanelId(next()));
            let parent = next();
            panel.weight = f32::from_bits(next() as u32);
            panel.options_from = id(next());
            panel.restore_emptied = next() != 0;
            panel.named = match next() {
                0 => None,
                n => Some(name(n)),
            };
            panel.selected = match next() {
                0 => None,
                tab => Some(tab),
            };
            let tabs = next();
            panel.tabs = (0..tabs).map(|_| next()).collect();
            panels.push(((parent != u64::MAX).then_some(parent as usize), panel));
        }
        let removed = next();
        let mut removed: Vec<Tab> = (0..removed).map(|_| next()).collect();
        removed.reverse();
        // Attach children last first: each is whole when attached.
        let mut built: Vec<Option<Panel<Tab>>> = panels.iter().map(|(_, p)| Some(p.clone())).collect();
        for index in (0..built.len()).rev() {
            if let Some(parent) = panels[index].0 {
                let child = built[index].take().expect("attached once");
                built[parent].as_mut().expect("a parent comes first").children.insert(0, child);
            }
        }
        Arrangement { root: built.into_iter().next().flatten(), root_axis, next_id, removed }
    }

    /// The C arrangement as the core's.
    pub fn dump(&self) -> Arrangement<Tab> {
        let _guard = lock();
        self.dump_locked()
    }

    /// The C validity check.
    pub fn problem(&self) -> Option<String> {
        let _guard = lock();
        let mut out = vec![0u8; 512];
        let len = unsafe { oracle_problem(self.0, out.as_mut_ptr(), out.len() as u64) } as usize;
        out.truncate(len.min(512));
        (len != 0).then(|| String::from_utf8(out).expect("the C writes ASCII"))
    }

    /// Applies `edit` as the C function it replaced does, reporting what the
    /// C reports in the core's terms. Where the C returns nothing (a close's
    /// refusal, a resize), `applied` is what the core means by it, read from
    /// the document before.
    pub fn apply(&mut self, edit: &Edit<Tab>, hidden: &[Tab]) -> Outcome<Tab> {
        let _guard = lock();
        let before = self.dump_locked();
        set_hidden(hidden);
        let h = self.0;
        let mut out = Outcome::default();
        let has_next = |panel: PanelId| {
            fn walk(p: &Panel<Tab>, id: PanelId) -> Option<bool> {
                p.children.iter().enumerate().find_map(|(i, c)| if c.id == id { Some(i + 1 < p.children.len()) } else { walk(c, id) })
            }
            before.root.as_ref().and_then(|root| walk(root, panel)).unwrap_or(false)
        };
        unsafe {
            match edit {
                Edit::Clear => {
                    out.panel = id(oracle_clear(h));
                    out.applied = true;
                }
                Edit::Discard => {
                    oracle_discard(h);
                    out.applied = true;
                }
                Edit::Split { panel, dir: d } => {
                    out.panel = id(oracle_split(h, panel.0, dir(Some(*d))));
                    out.applied = out.panel.is_some();
                }
                Edit::Add { parent, weight } => {
                    out.panel = id(oracle_add(h, parent.map_or(0, |p| p.0), *weight));
                    out.applied = out.panel.is_some();
                }
                Edit::Remove { panel } => out.applied = oracle_remove(h, panel.0) != 0,
                Edit::Close { panel } => {
                    out.applied = before.panel(*panel).is_some() && before.root.as_ref().is_some_and(|r| r.id != *panel);
                    out.heir = id(oracle_close(h, panel.0));
                }
                Edit::InsertTab { tab, to, after, select } => {
                    out.applied = oracle_insert_tab(h, *tab, to.0, after.unwrap_or(0), *select as u32) != 0
                }
                Edit::DetachTab { tab } => out.applied = oracle_detach_tab(h, *tab) != 0,
                Edit::RemoveTab { tab } => out.applied = oracle_remove_tab(h, *tab) != 0,
                Edit::Drop { tab, to, dir: d, after } => {
                    let mut result = [0u64; 3];
                    oracle_drop(h, tab.unwrap_or(0), to.0, dir(*d), after.unwrap_or(0), result.as_mut_ptr());
                    (out.panel, out.source, out.heir) = (id(result[0]), id(result[1]), id(result[2]));
                    out.applied = out.panel.is_some();
                }
                Edit::Settle { panel, dir: d, may_close } => {
                    let mut heir = 0;
                    out.applied = oracle_settle(h, panel.0, dir(*d), *may_close as u32, &mut heir) != 0;
                    out.heir = id(heir);
                }
                Edit::Select { panel, tab } => out.applied = oracle_select(h, panel.0, tab.unwrap_or(0)) != 0,
                Edit::Reorder { panel, after } => out.applied = oracle_reorder(h, panel.0, after.map_or(0, |p| p.0)) != 0,
                Edit::Resize { panel, delta, floor } => {
                    out.applied = has_next(*panel);
                    oracle_resize(h, panel.0, *delta, *floor);
                }
                Edit::Equalize { panel } => {
                    out.applied = has_next(*panel);
                    oracle_equalize(h, panel.0);
                }
                Edit::Readdress { from, to } => out.applied = oracle_readdress(h, *from, *to) != 0,
            }
        }
        let after = self.dump_locked();
        out.removed = after.removed[before.removed.len().min(after.removed.len())..].to_vec();
        out
    }

    /// A drop of `tab` into this arrangement from `source`, another one, as
    /// the shell's drop command edits the two (uishell_drop_view): the
    /// drop's outcome, and whether the source changed with its settle's heir.
    pub fn drop_across(
        &mut self,
        source: &mut Oracle,
        tab: Tab,
        to: PanelId,
        d: Option<Dir>,
        after: Option<Tab>,
        hidden: &[Tab],
    ) -> (Outcome<Tab>, bool, Option<PanelId>) {
        let _guard = lock();
        set_hidden(hidden);
        let mut result = [0u64; 5];
        unsafe { oracle_drop_across(self.0, source.0, tab, to.0, dir(d), after.unwrap_or(0), result.as_mut_ptr()) };
        let out = Outcome { applied: result[0] != 0, panel: id(result[0]), source: id(result[1]), heir: id(result[2]), removed: vec![] };
        (out, result[3] != 0, id(result[4]))
    }
}

fn host(h: &Host) -> OracleHost {
    OracleHost {
        kind: match h.kind {
            Some(HostKind::Sidebar) => 0,
            Some(HostKind::WorkspaceRegion) => 1,
            None => 2,
        },
        has_workspace_subject: h.has_workspace_subject as u32,
        controlled_split: h.controlled_split,
        workspace_region: h.workspace_region,
        level: h.level,
        available_width: h.available_width,
        pad: 0,
    }
}

/// The words the C gives `rule`.
pub fn rule_message(rule: Rule) -> String {
    let raw = Rule::ALL.iter().position(|r| *r == rule).expect("every rule is in ALL") as u32;
    let mut out = vec![0u8; 256];
    // SAFETY: out holds cap bytes; the C writes at most that many.
    let len = unsafe { oracle_rule_message(raw, out.as_mut_ptr(), out.len() as u64) } as usize;
    out.truncate(len.min(256));
    String::from_utf8(out).expect("the C's messages are UTF-8")
}

/// The bits of each panel's weight, in tree order: `==` on `f32` can't
/// tell -0.0 from 0.0, and `%f` writes them differently.
pub fn weight_bits(a: &Arrangement<Tab>) -> Vec<u32> {
    a.panels().map(|p| p.weight.to_bits()).collect()
}

fn rule(raw: u32) -> Rule {
    Rule::ALL[raw as usize]
}

fn index(registration: Option<usize>) -> i64 {
    registration.map_or(-1, |i| i as i64)
}

/// The C's registry and counts: the registrations (at most 16; one named
/// [`SIDEBAR_SECTION`] is that), and, when `instances` is some, each
/// Controlled Split's count (0 for one not listed).
pub struct Docking<'a> {
    _guard: MutexGuard<'static, ()>,
    registrations: &'a [Registration],
}

impl<'a> Docking<'a> {
    pub fn new(registrations: &'a [Registration], instances: Option<&[(u64, u32)]>) -> Self {
        let guard = lock();
        let c: Vec<OracleRegistration> = registrations
            .iter()
            .map(|r| OracleRegistration {
                traits: r.traits,
                minimum_width: r.minimum_width,
                default_host: match r.default_host {
                    HostKind::Sidebar => 0,
                    HostKind::WorkspaceRegion => 1,
                },
                sidebar_section: (r.name == SIDEBAR_SECTION) as u32,
            })
            .collect();
        let (splits, counts): (Vec<u64>, Vec<u32>) = instances.unwrap_or(&[]).iter().copied().unzip();
        unsafe {
            oracle_set_registrations(c.as_ptr(), c.len() as u64);
            oracle_set_instances(instances.is_some() as u32, splits.as_ptr(), counts.as_ptr(), splits.len() as u64);
        }
        Self { _guard: guard, registrations }
    }

    /// The name the C gives registration `i`.
    pub fn name(&self, i: usize) -> String {
        if self.registrations[i].name == SIDEBAR_SECTION {
            SIDEBAR_SECTION.into()
        } else {
            format!("r{i}")
        }
    }

    pub fn check(&self, registration: Option<usize>, p: &Proposal) -> Rule {
        let p = OracleProposal {
            host: host(&p.host),
            selected_workspace_region: p.selected_workspace_region,
            view_level: p.view_level,
            instances_after: p.instances_after,
            control_surfaces_after: p.control_surfaces_after,
            closing: p.closing as u32,
            pad: 0,
        };
        rule(unsafe { oracle_check(index(registration), p) })
    }

    pub fn placement(&self, exists: bool, registration: Option<usize>, source: &Host, level: u64, destination: &Host) -> Rule {
        rule(unsafe { oracle_placement(exists as u32, index(registration), host(source), level, host(destination)) })
    }

    /// Creation by name, the registration's or (none) a name nothing has.
    pub fn creation(&self, registration: Option<usize>, destination: &Host) -> Rule {
        rule(unsafe { oracle_creation(index(registration), host(destination)) })
    }

    pub fn closure(&self, exists: bool, registration: Option<usize>, source: &Host, level: u64) -> Rule {
        rule(unsafe { oracle_closure(exists as u32, index(registration), host(source), level) })
    }

    pub fn view_level(&self, registration: Option<usize>, h: &Host) -> u64 {
        unsafe { oracle_view_level(index(registration), host(h)) }
    }

    /// Restore over `documents` with their hosts, each tab's registration
    /// from `tabs`: the dirty marks and the moved sections.
    pub fn restore(
        &self,
        documents: &mut [Oracle],
        hosts: &[Host],
        sidebar: Option<usize>,
        workspace: Option<usize>,
        tabs: &[(Tab, Option<usize>)],
    ) -> (Vec<bool>, Vec<Tab>) {
        let (keys, registrations): (Vec<u64>, Vec<i64>) = tabs.iter().map(|&(tab, r)| (tab, index(r))).unzip();
        let handles: Vec<*mut c_void> = documents.iter().map(|d| d.0).collect();
        let hosts: Vec<OracleHost> = hosts.iter().map(host).collect();
        let mut dirty = vec![0u32; handles.len()];
        let mut pending = vec![0u64; 256];
        let n = unsafe {
            oracle_set_tab_registrations(keys.as_ptr(), registrations.as_ptr(), keys.len() as u64);
            oracle_restore(
                handles.as_ptr(),
                hosts.as_ptr(),
                handles.len() as u64,
                index(sidebar),
                index(workspace),
                dirty.as_mut_ptr(),
                pending.as_mut_ptr(),
                pending.len() as u64,
            )
        };
        pending.truncate(n as usize);
        (dirty.into_iter().map(|d| d != 0).collect(), pending)
    }

    /// Dumps a document under the registry's lock.
    pub fn dump(&self, oracle: &Oracle) -> Arrangement<Tab> {
        oracle.dump_locked()
    }
}
