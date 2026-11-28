//! Typed reading of a provider's Suggested Layout from an entity's flat facts.
//! The schema is docs/core/sidebar-design/suggested-layouts.md. Parsing has no
//! effect on managed content, placement or snapshots yet.
//!
//! Two kinds of problem are distinguished. A malformed *spec* (slot list,
//! view specs, arrangement) is a [`LayoutError`] for the whole layout. An
//! incomplete *resolution* of a provider-facet slot is that slot's
//! [`SlotResolution::Unavailable`], matching managed primary content.
use crate::{EntityRef, FactMap, MetadataValue};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt;
use std::ops::Bound;

/// The reserved slot key for the `workspace.primary.*` facts.
pub const PRIMARY_SLOT: &str = "primary";
const PRIMARY_FACET: &str = "primary";
const KEY_VERSION: &str = "layout.version";
const KEY_SLOTS: &str = "layout.slots";
const KEY_ROOT: &str = "layout.root";
const SLOT_PREFIX: &str = "layout.slot.";
const PANEL_PREFIX: &str = "layout.panel.";
const KEY_PRIMARY_STATE: &str = "workspace.primary.state";
const KEY_PRIMARY_TARGET: &str = "workspace.primary.target";
const KEY_PRIMARY_RECIPE: &str = "action.primary.recipe";
const KEY_PRIMARY_CWD: &str = "git.root";
const KEY_ACTION_TARGET: &str = "action.primary.target";
const KEY_DIRECT_PUBLICATION_SCOPE: &str = "action.primary.direct.publication.scope";
const KEY_DIRECT_PUBLICATION_NAME: &str = "action.primary.direct.publication.name";
const KEY_DIRECT_PUBLICATION_LABEL: &str = "action.primary.direct.publication.label";
const KEY_DIRECT_SESSION: &str = "action.primary.direct.session";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SuggestedLayout {
    pub version: BaselineVersion,
    /// In the provider's order, which is also the default placement order.
    pub slots: Vec<Slot>,
    /// `None` when the provider gave no arrangement hint.
    pub arrangement: Option<Arrangement>,
}

impl SuggestedLayout {
    pub fn slot(&self, key: &str) -> Option<&Slot> {
        self.slots.iter().find(|slot| slot.key == key)
    }
}

/// What a Workspace Overlay records its edits against.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum BaselineVersion {
    /// The opaque `layout.version` fact; compared only for equality.
    Published(String),
    /// Only today's primary-slot facts: one slot whose spec never changes.
    PrimaryOnly,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Slot {
    pub key: String,
    pub spec: ViewSpec,
    pub rebind: RebindPolicy,
    pub resolution: SlotResolution,
}

/// The logical definition of a View: its content and preferred presentation.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct ViewSpec {
    pub content: Content,
    /// Open vocabulary (`terminal`, `web`, `markdown`, ...); frontends pick a
    /// renderer they support and otherwise show a placeholder.
    pub presentation: Option<String>,
    /// What the View is for (`agent`, `files`, `shell`, ...; open
    /// vocabulary), which Workspace Layouts arrange by.
    #[serde(default)]
    pub role: Option<String>,
}

/// The primary slot's role when the provider names none, by its subject
/// kind (docs/core/sidebar-design/workspace-layouts.md#roles).
pub fn default_primary_role(kind: &str) -> Option<&'static str> {
    match kind {
        "vessel" | "convoy" => Some("agent"),
        "project" | "role" => Some("tui"),
        _ => None,
    }
}

#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum Content {
    /// An aspect of a provider entity; the provider publishes its resolution.
    ProviderFacet { entity: EntityRef, facet: String },
    /// Belongs to the `local` provider and is resolved by the frontend.
    Local(LocalRecipe),
    /// State document `key` of the agent session the host Slot
    /// `relative_to` shows now, so it follows the host as it rebinds or
    /// switches Facet (#706); pinning makes it a [`LocalRecipe::AgentDocument`].
    HostDocument { relative_to: String, key: String },
}

#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(from = "LocalRecipeRepr")]
pub enum LocalRecipe {
    Command {
        line: CommandLine,
        cwd: Option<String>,
    },
    File {
        path: String,
    },
    Url {
        url: String,
    },
    Jackstay {
        launcher: String,
        endpoint: String,
    },
    /// A direct Cleat packet view of `session`, served by the daemon that
    /// Tender publishes as `name` in `scope`. The viewer binds it through its
    /// own Tender instance, so it is a socket on this machine whichever host
    /// carries the session (`wheelhouse-ffi` computes the path).
    CleatBinding {
        scope: String,
        name: String,
        session: String,
    },
    /// An agent thread watched over the Agent View protocol (#691): its
    /// producer's Local Endpoint (a socket on this machine, or on the ssh
    /// destination `host`, #737) and the thread, whose id is the same
    /// wherever it is watched from (#706). `host` is empty when local.
    AgentSession {
        // Absent in layouts saved before #737, which are all local.
        #[serde(default)]
        transport: CleatTransport,
        #[serde(default)]
        host: String,
        endpoint: String,
        thread: String,
    },
    /// State document `key` of an agent thread, reached as
    /// [`LocalRecipe::AgentSession`] is: a subagent's, or a pinned View's.
    AgentDocument {
        #[serde(default)]
        transport: CleatTransport,
        #[serde(default)]
        host: String,
        endpoint: String,
        thread: String,
        key: String,
    },
}

impl LocalRecipe {
    /// Document `key` of this agent session; None for any other recipe.
    pub fn document(&self, key: &str) -> Option<LocalRecipe> {
        match self {
            LocalRecipe::AgentSession { transport, host, endpoint, thread } => Some(LocalRecipe::AgentDocument {
                transport: *transport,
                host: host.clone(),
                endpoint: endpoint.clone(),
                thread: thread.clone(),
                key: key.to_owned(),
            }),
            _ => None,
        }
    }
}

/// What a saved [`LocalRecipe`] decodes from: the recipe's own shapes, and the
/// host-path `CleatSession` that layouts saved before Tender publications
/// (#789) hold. It opens through the command recipe it names, the way the
/// session was reached; the primary's own command is not in a saved layout.
///
/// Tracked in wheelhouse#794. Remove `CleatSession` here, with [`legacy_cleat_command`], once layouts
/// saved before #789 have been rewritten by a release that no longer decodes
/// it: one generation of releases after this one.
#[derive(serde::Deserialize)]
enum LocalRecipeRepr {
    Command {
        line: CommandLine,
        cwd: Option<String>,
    },
    File {
        path: String,
    },
    Url {
        url: String,
    },
    Jackstay {
        launcher: String,
        endpoint: String,
    },
    CleatBinding {
        scope: String,
        name: String,
        session: String,
    },
    CleatSession {
        transport: CleatTransport,
        host: String,
        runtime_root: String,
        daemon: String,
        session: String,
    },
    AgentSession {
        #[serde(default)]
        transport: CleatTransport,
        #[serde(default)]
        host: String,
        endpoint: String,
        thread: String,
    },
    AgentDocument {
        #[serde(default)]
        transport: CleatTransport,
        #[serde(default)]
        host: String,
        endpoint: String,
        thread: String,
        key: String,
    },
}

impl From<LocalRecipeRepr> for LocalRecipe {
    fn from(repr: LocalRecipeRepr) -> Self {
        match repr {
            LocalRecipeRepr::Command { line, cwd } => Self::Command { line, cwd },
            LocalRecipeRepr::File { path } => Self::File { path },
            LocalRecipeRepr::Url { url } => Self::Url { url },
            LocalRecipeRepr::Jackstay { launcher, endpoint } => Self::Jackstay { launcher, endpoint },
            LocalRecipeRepr::CleatBinding { scope, name, session } => Self::CleatBinding { scope, name, session },
            // A field no host could pass on as one argument opens nothing.
            LocalRecipeRepr::CleatSession { transport, host, runtime_root, daemon, session } => {
                if invalid_legacy_cleat_field(&host, &runtime_root, &daemon, &session).is_some() {
                    return Self::Command { line: CommandLine::Argv(vec!["false".to_owned()]), cwd: None };
                }
                legacy_cleat_command(transport, &host, &runtime_root, &daemon, &session)
            }
            LocalRecipeRepr::AgentSession { transport, host, endpoint, thread } => Self::AgentSession { transport, host, endpoint, thread },
            LocalRecipeRepr::AgentDocument { transport, host, endpoint, thread, key } => {
                Self::AgentDocument { transport, host, endpoint, thread, key }
            }
        }
    }
}

/// The command that opens a host-path Cleat session as the old direct
/// recipe reached it: `cleat attach` on this machine, or over ssh. It never
/// creates the session. See [`LocalRecipeRepr`] for when this can go.
pub fn legacy_cleat_command(transport: CleatTransport, host: &str, runtime_root: &str, daemon: &str, session: &str) -> LocalRecipe {
    let mut cleat: Vec<String> = vec!["cleat".to_owned()];
    if !runtime_root.is_empty() {
        cleat.extend(["--runtime-root".to_owned(), runtime_root.to_owned()]);
    }
    if !daemon.is_empty() {
        cleat.extend(["--server".to_owned(), daemon.to_owned()]);
    }
    cleat.extend(["attach", "--no-create", "--", session].map(str::to_owned));
    let argv = match transport {
        CleatTransport::Local => cleat,
        // The remote shell re-parses the words after the host, so each is quoted.
        CleatTransport::Ssh => {
            let remote = cleat.iter().map(|word| shell_word(word)).collect::<Vec<_>>().join(" ");
            ["ssh", "-t", "--"].map(str::to_owned).into_iter().chain([host.to_owned(), remote]).collect()
        }
    };
    LocalRecipe::Command { line: CommandLine::Argv(argv), cwd: None }
}

/// The first host-path Cleat session field a host could not pass on as one
/// argument: each is nonempty, never starts with `-`, and has no control
/// characters; all but the runtime root, a path, also have no whitespace.
/// Part of the shim [`LocalRecipeRepr`] describes.
pub fn invalid_legacy_cleat_field(host: &str, runtime_root: &str, daemon: &str, session: &str) -> Option<&'static str> {
    let word = |text: &str, spaces: bool| {
        !text.is_empty() && !text.starts_with('-') && !text.chars().any(|c| c.is_control() || (!spaces && c.is_whitespace()))
    };
    [("host", host, false), ("runtime_root", runtime_root, true), ("daemon", daemon, false), ("session", session, false)]
        .into_iter()
        .find(|(_, text, spaces)| !word(text, *spaces))
        .map(|(field, ..)| field)
}

fn shell_word(word: &str) -> String {
    format!("'{}'", word.replace('\'', "'\\''"))
}

/// How a frontend reaches a Cleat session's daemon, or an agent's producer.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum CleatTransport {
    #[default]
    Local,
    Ssh,
}

impl CleatTransport {
    pub fn parse(text: &str) -> Option<Self> {
        match text {
            "local" => Some(Self::Local),
            "ssh" => Some(Self::Ssh),
            _ => None,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Local => "local",
            Self::Ssh => "ssh",
        }
    }
}

/// The first Cleat binding field a host could not use, if any. Each is
/// nonempty and has no control characters; the session also never starts with
/// `-`, so it is never read as an option to `cleat`. The scope and name are
/// Tender identity text (`vessel/<id>`, `cleat/<daemon>`): they reach a socket
/// path only through Tender's own encoding, so they may hold `/`.
pub fn invalid_cleat_field(scope: &str, name: &str, session: &str) -> Option<&'static str> {
    let word = |text: &str, option_safe: bool| {
        !text.is_empty()
            && !(option_safe && text.starts_with('-'))
            && !text.chars().any(|c| c.is_control() || (option_safe && c.is_whitespace()))
    };
    [("scope", scope, false), ("name", name, false), ("session", session, true)]
        .into_iter()
        .find(|(_, text, option_safe)| !word(text, *option_safe))
        .map(|(field, ..)| field)
}

/// The first agent session field a host could not use, if any. Each is
/// nonempty but a local session's host, which is empty. Over ssh the host is
/// one argument, as for [`invalid_cleat_field`], and the endpoint an absolute
/// path without control characters or `:`, which `-L` reads as a separator.
pub fn invalid_agent_field(transport: CleatTransport, host: &str, endpoint: &str, thread: &str) -> Option<&'static str> {
    let (host_ok, endpoint_ok) = match transport {
        CleatTransport::Local => (host.is_empty(), !endpoint.is_empty()),
        CleatTransport::Ssh => (
            !host.is_empty() && !host.starts_with('-') && !host.chars().any(|c| c.is_control() || c.is_whitespace()),
            endpoint.starts_with('/') && !endpoint.chars().any(|c| c.is_control() || c == ':'),
        ),
    };
    [("host", host_ok), ("endpoint", endpoint_ok), ("thread", !thread.is_empty())].into_iter().find(|(_, ok)| !ok).map(|(field, _)| field)
}

#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum CommandLine {
    /// Structured `argv.<n>` facts (andamento#134). Preferred when present.
    Argv(Vec<String>),
    /// A shell command line, the shape of `action.primary.recipe`.
    Shell(String),
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum RebindPolicy {
    #[default]
    Replace,
    KeepPrevious,
    Ask,
}

/// One way an entity can be shown: a slot of its own Suggested Layout,
/// named by its key.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Facet {
    pub name: String,
    /// The provider publishes a recipe for it now.
    pub ready: bool,
    /// The slot it was listed for shows it.
    pub current: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SlotResolution {
    /// What a frontend runs now. `target` identifies the backing instance and
    /// changes whenever it is replaced, even if the recipe text does not.
    Ready { target: Option<String>, recipe: LocalRecipe },
    /// The provider explicitly suspends replacement.
    Held,
    /// Missing, malformed or incomplete resolution facts. Not deletion.
    Unavailable,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Arrangement {
    pub root: String,
    pub panels: BTreeMap<String, Panel>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Panel {
    /// Relative to siblings in the parent split; ignored for the root.
    pub weight: u32,
    pub node: PanelNode,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PanelNode {
    Split { axis: Axis, children: Vec<String> },
    Tabs { slots: Vec<String>, selected: String },
}

/// `Row` lays children left to right, `Column` top to bottom.
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum Axis {
    Row,
    Column,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LayoutError {
    MissingFact {
        key: String,
    },
    WrongType {
        key: String,
        expected: &'static str,
    },
    InvalidValue {
        key: String,
        value: String,
    },
    InvalidId {
        key: String,
        id: String,
    },
    DuplicateSlot {
        slot: String,
    },
    /// `layout.slot.primary.*`: the primary slot comes from `workspace.primary.*`.
    ReservedPrimary {
        key: String,
    },
    /// `layout.slots` names `primary` but `workspace.primary.state` is absent.
    MissingPrimary,
    /// A slot fact for a key `layout.slots` does not list.
    UnlistedSlot {
        key: String,
    },
    /// Only one of `entity` and `facet`.
    IncompleteFacet {
        slot: String,
    },
    /// `argv.<n>` keys that are not exactly `0..len`.
    ArgvGap {
        slot: String,
    },
    PanelShape {
        panel: String,
    },
    UnknownPanel {
        panel: String,
    },
    PanelReused {
        panel: String,
    },
    UnreachablePanel {
        panel: String,
    },
    UnknownSlot {
        panel: String,
        slot: String,
    },
    SlotInTwoPanels {
        slot: String,
    },
    SelectedNotInPanel {
        panel: String,
        slot: String,
    },
}

impl fmt::Display for LayoutError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use LayoutError::*;
        match self {
            MissingFact { key } => write!(f, "missing {key}"),
            WrongType { key, expected } => write!(f, "{key} must be {expected}"),
            InvalidValue { key, value } => write!(f, "{key} has invalid value {value:?}"),
            InvalidId { key, id } => write!(f, "{key} has invalid id {id:?}"),
            DuplicateSlot { slot } => write!(f, "slot {slot} is listed twice"),
            ReservedPrimary { key } => {
                write!(f, "{key}: the primary slot comes from workspace.primary.*")
            }
            MissingPrimary => write!(f, "layout.slots lists primary without workspace.primary.state"),
            UnlistedSlot { key } => write!(f, "{key} names a slot layout.slots does not list"),
            IncompleteFacet { slot } => write!(f, "slot {slot} needs both entity and facet"),
            ArgvGap { slot } => write!(f, "slot {slot} argv indices are not 0..n"),
            PanelShape { panel } => {
                write!(f, "panel {panel} needs either axis and children, or slots")
            }
            UnknownPanel { panel } => write!(f, "panel {panel} has no facts"),
            PanelReused { panel } => write!(f, "panel {panel} appears twice in the arrangement"),
            UnreachablePanel { panel } => {
                write!(f, "panel {panel} is not reachable from layout.root")
            }
            UnknownSlot { panel, slot } => write!(f, "panel {panel} holds unknown slot {slot}"),
            SlotInTwoPanels { slot } => write!(f, "slot {slot} is held by two panels"),
            SelectedNotInPanel { panel, slot } => {
                write!(f, "panel {panel} selects {slot}, which it does not hold")
            }
        }
    }
}

impl std::error::Error for LayoutError {}

/// When a Suggested Layout has the primary slot.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PrimarySlot {
    /// When the entity publishes `workspace.primary.state`.
    IfPublished,
    /// Always, as a workspace subject whose workspace keeps no primary of its
    /// own has: resolved from its opening recipe when it publishes no
    /// `workspace.primary.state`, and unavailable while it has none.
    Always,
}

/// Reads the Suggested Layout `entity` publishes, if any. `facts` are its
/// resolved, live facts; `primary` says when it has the primary slot.
/// Returns `Ok(None)` when it has neither `layout.*` facts nor a primary slot.
pub fn parse(entity: &EntityRef, facts: &FactMap, primary: PrimarySlot) -> Result<Option<SuggestedLayout>, LayoutError> {
    let always = primary == PrimarySlot::Always;
    let primary = (always || facts.contains_key(KEY_PRIMARY_STATE)).then(|| primary_slot(entity, facts));
    if !facts.keys().any(|key| key.starts_with("layout.")) {
        return Ok(primary.map(|slot| SuggestedLayout { version: BaselineVersion::PrimaryOnly, slots: vec![slot], arrangement: None }));
    }
    let f = Facts(facts);
    let version = f.text(KEY_VERSION)?.filter(|v| !v.is_empty());
    let version = version.ok_or_else(|| missing(KEY_VERSION))?;
    let keys = f.list(KEY_SLOTS)?.ok_or_else(|| missing(KEY_SLOTS))?;
    let mut listed = BTreeSet::new();
    for key in keys {
        check_id(KEY_SLOTS, key)?;
        if !listed.insert(key.as_str()) {
            return Err(LayoutError::DuplicateSlot { slot: key.clone() });
        }
    }
    for fact in facts.keys() {
        if let Some((slot, _)) = fact.strip_prefix(SLOT_PREFIX).and_then(|rest| rest.split_once('.')) {
            if slot == PRIMARY_SLOT {
                return Err(LayoutError::ReservedPrimary { key: fact.to_string() });
            }
            if !listed.contains(slot) {
                return Err(LayoutError::UnlistedSlot { key: fact.to_string() });
            }
        }
    }
    let mut primary = primary;
    if listed.contains(PRIMARY_SLOT) && primary.is_none() {
        return Err(LayoutError::MissingPrimary);
    }
    // An unlisted primary slot leads the slot order.
    let mut slots: Vec<Slot> = match primary.as_ref() {
        Some(_) if !listed.contains(PRIMARY_SLOT) => primary.take().into_iter().collect(),
        _ => Vec::new(),
    };
    for key in keys {
        slots.push(match key.as_str() {
            PRIMARY_SLOT => primary.take().expect("checked above"),
            _ => provider_slot(&f, key)?,
        });
    }
    let known: BTreeSet<&str> = slots.iter().map(|s| s.key.as_str()).collect();
    let arrangement = arrangement(&f, &known)?;
    Ok(Some(SuggestedLayout { version: BaselineVersion::Published(version.to_owned()), slots, arrangement }))
}

/// The single-slot special case, read exactly as managed primary content does
/// (state.rs, `desired_primary`).
fn primary_slot(entity: &EntityRef, facts: &FactMap) -> Slot {
    let text = |key| match facts.get(key) {
        Some(MetadataValue::Text(value)) => Some(value.clone()),
        _ => None,
    };
    let ready = |target: Option<std::sync::Arc<str>>| {
        let command = || {
            let command = text(KEY_PRIMARY_RECIPE).filter(|s| !s.is_empty())?;
            Some(LocalRecipe::Command {
                line: CommandLine::Shell(command.to_string()),
                cwd: text(KEY_PRIMARY_CWD).map(|cwd| cwd.to_string()),
            })
        };
        match (target.filter(|s| !s.is_empty()), direct_primary(text).or_else(command)) {
            (Some(target), Some(recipe)) => SlotResolution::Ready { target: Some(target.to_string()), recipe },
            _ => SlotResolution::Unavailable,
        }
    };
    let resolution = match text(KEY_PRIMARY_STATE).as_deref() {
        Some("held") => SlotResolution::Held,
        Some("ready") => ready(text(KEY_PRIMARY_TARGET)),
        Some(_) => SlotResolution::Unavailable,
        // No workspace.primary.* facts: the opening recipe, at the entity's
        // action target.
        None => ready(text(KEY_ACTION_TARGET).or_else(|| Some(entity.action_target().into()))),
    };
    Slot {
        key: PRIMARY_SLOT.to_owned(),
        spec: ViewSpec {
            content: Content::ProviderFacet { entity: entity.clone(), facet: PRIMARY_FACET.to_owned() },
            presentation: None,
            role: default_primary_role(&entity.kind).map(str::to_owned),
        },
        rebind: RebindPolicy::Replace,
        resolution,
    }
}

/// The label of a publication this opens: Tender doesn't interpret labels, so
/// the application checks them (#789).
pub const CLEAT_PUBLICATION_LABEL: &str = "cleat";

/// The primary's direct Cleat endpoint (wheelhouse#104), preferred over its
/// command recipe: the Tender publication of its session's daemon
/// (`action.primary.direct.publication.{scope,name,label}` and
/// `action.primary.direct.session`), when the label is `cleat` and the fields
/// are ones [`invalid_cleat_field`] accepts. `None` when the provider
/// publishes none (only `action.primary.direct.reason`) or an incomplete,
/// foreign or unusable one; the primary then opens through its command
/// recipe.
pub(crate) fn direct_primary<S: AsRef<str>>(text: impl Fn(&'static str) -> Option<S>) -> Option<LocalRecipe> {
    let field = |key| text(key).map(|value| value.as_ref().to_owned());
    if field(KEY_DIRECT_PUBLICATION_LABEL)? != CLEAT_PUBLICATION_LABEL {
        return None;
    }
    let (scope, name, session) = (field(KEY_DIRECT_PUBLICATION_SCOPE)?, field(KEY_DIRECT_PUBLICATION_NAME)?, field(KEY_DIRECT_SESSION)?);
    if invalid_cleat_field(&scope, &name, &session).is_some() {
        return None;
    }
    Some(LocalRecipe::CleatBinding { scope, name, session })
}

fn provider_slot(f: &Facts, key: &str) -> Result<Slot, LayoutError> {
    let at = |field: &str| format!("{SLOT_PREFIX}{key}.{field}");
    let presentation = f.text(&at("presentation"))?.map(str::to_owned);
    let role = f.text(&at("role"))?.filter(|role| !role.is_empty()).map(str::to_owned);
    let rebind = match f.text(&at("rebind"))? {
        None | Some("replace") => RebindPolicy::Replace,
        Some("keep-previous") => RebindPolicy::KeepPrevious,
        Some("ask") => RebindPolicy::Ask,
        Some(other) => return Err(invalid(&at("rebind"), other)),
    };
    let entity = match f.0.get(at("entity").as_str()) {
        None => None,
        Some(MetadataValue::EntityRefs(refs)) if refs.len() == 1 => Some(refs[0].clone()),
        Some(_) => return Err(wrong(&at("entity"), "exactly one entity ref")),
    };
    let facet = f.text(&at("facet"))?.filter(|s| !s.is_empty());
    if let Some(relative_to) = f.text(&at("relative_to"))? {
        check_id(&at("relative_to"), relative_to)?;
        if relative_to == key {
            return Err(invalid(&at("relative_to"), relative_to));
        }
        let document = f.text(&at("key"))?.filter(|k| !k.is_empty()).ok_or_else(|| missing(&at("key")))?;
        let content = Content::HostDocument { relative_to: relative_to.to_owned(), key: document.to_owned() };
        // Its host's session resolves it, never the provider.
        return Ok(Slot { key: key.to_owned(), spec: ViewSpec { content, presentation, role }, rebind, resolution: SlotResolution::Held });
    }
    let state = f.text(&at("state"));
    let (content, resolution) = match (entity, facet) {
        (Some(entity), Some(facet)) => {
            // Resolution facts never invalidate the layout; they make the slot
            // unavailable, as incomplete workspace.primary.* facts do.
            let resolution = match state.clone().ok().flatten() {
                Some("held") => SlotResolution::Held,
                Some("ready") => {
                    let target = f.text(&at("target")).ok().flatten();
                    match (target.filter(|s| !s.is_empty()), recipe(f, key)) {
                        (Some(target), Ok(recipe)) => SlotResolution::Ready { target: Some(target.to_owned()), recipe },
                        _ => SlotResolution::Unavailable,
                    }
                }
                _ => SlotResolution::Unavailable,
            };
            let facet = facet.to_owned();
            (Content::ProviderFacet { entity, facet }, resolution)
        }
        (None, None) => {
            let recipe = recipe(f, key)?;
            let resolution = match state? {
                None | Some("ready") => SlotResolution::Ready { target: f.text(&at("target"))?.map(str::to_owned), recipe: recipe.clone() },
                Some("held") => SlotResolution::Held,
                Some(other) => return Err(invalid(&at("state"), other)),
            };
            (Content::Local(recipe), resolution)
        }
        _ => return Err(LayoutError::IncompleteFacet { slot: key.to_owned() }),
    };
    Ok(Slot { key: key.to_owned(), spec: ViewSpec { content, presentation, role }, rebind, resolution })
}

fn recipe(f: &Facts, key: &str) -> Result<LocalRecipe, LayoutError> {
    let at = |field: &str| format!("{SLOT_PREFIX}{key}.{field}");
    let required = |field: &str| -> Result<String, LayoutError> {
        let key = at(field);
        match f.text(&key)? {
            Some(value) if !value.is_empty() => Ok(value.to_owned()),
            _ => Err(missing(&key)),
        }
    };
    let kind = required("kind")?;
    Ok(match kind.as_str() {
        "command" => {
            let argv = argv(f, key)?;
            let line = match (argv.is_empty(), f.text(&at("command"))?) {
                (false, _) => CommandLine::Argv(argv),
                (true, Some(shell)) if !shell.is_empty() => CommandLine::Shell(shell.to_owned()),
                _ => return Err(missing(&at("argv.0"))),
            };
            LocalRecipe::Command { line, cwd: f.text(&at("cwd"))?.map(str::to_owned) }
        }
        "file" => LocalRecipe::File { path: required("path")? },
        "url" => LocalRecipe::Url { url: required("url")? },
        "jackstay" => LocalRecipe::Jackstay { launcher: required("launcher")?, endpoint: required("endpoint")? },
        "cleat-binding" => {
            let (scope, name, session) = (required("scope")?, required("name")?, required("session")?);
            if let Some(field) = invalid_cleat_field(&scope, &name, &session) {
                return Err(invalid(&at(field), &required(field)?));
            }
            LocalRecipe::CleatBinding { scope, name, session }
        }
        kind @ ("agent-session" | "agent-document") => {
            let transport = match f.text(&at("transport"))? {
                None => CleatTransport::Local,
                Some(text) => CleatTransport::parse(text).ok_or_else(|| invalid(&at("transport"), text))?,
            };
            let host = f.text(&at("host"))?.unwrap_or_default().to_owned();
            let (endpoint, thread) = (required("endpoint")?, required("thread")?);
            if let Some(field) = invalid_agent_field(transport, &host, &endpoint, &thread) {
                let value = match field {
                    "host" => &host,
                    "thread" => &thread,
                    _ => &endpoint,
                };
                return Err(invalid(&at(field), value));
            }
            let session = LocalRecipe::AgentSession { transport, host, endpoint, thread };
            match kind {
                "agent-document" => session.document(&required("key")?).expect("a session"),
                _ => session,
            }
        }
        other => return Err(invalid(&at("kind"), other)),
    })
}

fn argv(f: &Facts, key: &str) -> Result<Vec<String>, LayoutError> {
    let prefix = format!("{SLOT_PREFIX}{key}.argv.");
    let mut indexed = BTreeMap::new();
    for (fact, value) in f.0.range::<str, _>((Bound::Included(prefix.as_str()), Bound::Unbounded)) {
        let Some(index) = fact.strip_prefix(&prefix) else {
            break;
        };
        let n: usize =
            index.parse().ok().filter(|n: &usize| n.to_string() == index).ok_or_else(|| LayoutError::ArgvGap { slot: key.to_owned() })?;
        let MetadataValue::Text(arg) = value else {
            return Err(wrong(fact, "text"));
        };
        indexed.insert(n, arg.clone());
    }
    if indexed.keys().copied().ne(0..indexed.len()) {
        return Err(LayoutError::ArgvGap { slot: key.to_owned() });
    }
    Ok(indexed.into_values().map(|value| value.to_string()).collect())
}

fn arrangement(f: &Facts, known: &BTreeSet<&str>) -> Result<Option<Arrangement>, LayoutError> {
    let mut declared = BTreeSet::new();
    for fact in f.0.keys() {
        if let Some((panel, _)) = fact.strip_prefix(PANEL_PREFIX).and_then(|rest| rest.split_once('.')) {
            check_id(fact, panel)?;
            declared.insert(panel.to_owned());
        }
    }
    let Some(root) = f.text(KEY_ROOT)? else {
        return match declared.into_iter().next() {
            Some(_) => Err(missing(KEY_ROOT)),
            None => Ok(None),
        };
    };
    check_id(KEY_ROOT, root)?;
    let mut panels = BTreeMap::new();
    let mut placed = BTreeSet::new();
    let mut pending = vec![root.to_owned()];
    while let Some(id) = pending.pop() {
        if panels.contains_key(&id) {
            return Err(LayoutError::PanelReused { panel: id });
        }
        if !declared.contains(&id) {
            return Err(LayoutError::UnknownPanel { panel: id });
        }
        let at = |field: &str| format!("{PANEL_PREFIX}{id}.{field}");
        let weight = match f.0.get(at("weight").as_str()) {
            None => 1,
            Some(MetadataValue::Integer(w)) => {
                u32::try_from(*w).ok().filter(|w| *w > 0).ok_or_else(|| invalid(&at("weight"), &w.to_string()))?
            }
            Some(_) => return Err(wrong(&at("weight"), "a positive integer")),
        };
        let shape = (f.text(&at("axis"))?, f.list(&at("children"))?, f.list(&at("slots"))?);
        let node = match shape {
            (Some(axis), Some(children), None) if !children.is_empty() => {
                let axis = match axis {
                    "row" => Axis::Row,
                    "column" => Axis::Column,
                    other => return Err(invalid(&at("axis"), other)),
                };
                for child in children {
                    check_id(&at("children"), child)?;
                }
                pending.extend(children.iter().rev().cloned());
                if f.0.contains_key(at("selected").as_str()) {
                    return Err(LayoutError::PanelShape { panel: id });
                }
                PanelNode::Split { axis, children: children.clone() }
            }
            (None, None, Some(slots)) if !slots.is_empty() => {
                for slot in slots {
                    if !known.contains(slot.as_str()) {
                        return Err(LayoutError::UnknownSlot { panel: id, slot: slot.clone() });
                    }
                    if !placed.insert(slot.clone()) {
                        return Err(LayoutError::SlotInTwoPanels { slot: slot.clone() });
                    }
                }
                let selected = f.text(&at("selected"))?.unwrap_or(&slots[0]).to_owned();
                if !slots.contains(&selected) {
                    return Err(LayoutError::SelectedNotInPanel { panel: id, slot: selected });
                }
                PanelNode::Tabs { slots: slots.clone(), selected }
            }
            _ => return Err(LayoutError::PanelShape { panel: id }),
        };
        panels.insert(id, Panel { weight, node });
    }
    if let Some(panel) = declared.into_iter().find(|id| !panels.contains_key(id)) {
        return Err(LayoutError::UnreachablePanel { panel });
    }
    Ok(Some(Arrangement { root: root.to_owned(), panels }))
}

struct Facts<'a>(&'a FactMap);

impl Facts<'_> {
    fn text(&self, key: &str) -> Result<Option<&str>, LayoutError> {
        match self.0.get(key) {
            None => Ok(None),
            Some(MetadataValue::Text(value)) => Ok(Some(value)),
            Some(_) => Err(wrong(key, "text")),
        }
    }
    fn list(&self, key: &str) -> Result<Option<&Vec<String>>, LayoutError> {
        match self.0.get(key) {
            None => Ok(None),
            Some(MetadataValue::StringList(values)) => Ok(Some(values)),
            Some(_) => Err(wrong(key, "a string list")),
        }
    }
}

/// Slot keys and panel IDs are embedded in fact keys, so they exclude `.`;
/// user-added slots (`u:3`) can never collide because they contain `:`.
fn check_id(key: &str, id: &str) -> Result<(), LayoutError> {
    let mut chars = id.chars();
    let valid = id.len() <= 64
        && chars.next().is_some_and(|c| c.is_ascii_lowercase() || c.is_ascii_digit())
        && chars.all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-' || c == '_');
    if valid {
        Ok(())
    } else {
        Err(LayoutError::InvalidId { key: key.to_owned(), id: id.to_owned() })
    }
}

fn missing(key: &str) -> LayoutError {
    LayoutError::MissingFact { key: key.to_owned() }
}
fn wrong(key: &str, expected: &'static str) -> LayoutError {
    LayoutError::WrongType { key: key.to_owned(), expected }
}
fn invalid(key: &str, value: &str) -> LayoutError {
    LayoutError::InvalidValue { key: key.to_owned(), value: value.to_owned() }
}

#[cfg(test)]
mod tests;
