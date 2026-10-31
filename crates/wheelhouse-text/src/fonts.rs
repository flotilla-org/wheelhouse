use std::{
    borrow::Cow,
    cell::RefCell,
    collections::HashMap,
    iter,
    path::Path,
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc, Mutex, MutexGuard, Weak,
    },
};

use fontique::{Blob, Collection, CollectionOptions, SourceCache, SourceKind};
use parley::{FontContext, FontData, FontFamily, FontFamilyName, Layout, LayoutContext};
use read_fonts::{types::NameId, FontRef, TableProvider};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Family {
    Main,
    Code,
    Icons,
}

impl Family {
    pub const ALL: [Family; 3] = [Family::Main, Family::Code, Family::Icons];

    pub fn from_slot(slot: u32) -> Option<Self> {
        Self::ALL.get(slot as usize).copied()
    }

    fn index(self) -> usize {
        self as usize
    }
}

// native's defaults (native/src/shell/shell.mdesk), all bundled in data/.
pub const DEFAULT_FAMILIES: [&str; 3] = ["Atkinson Hyperlegible Next", "JetBrainsMono Nerd Font", "fontello"];

// In data/FONTS.md's order, then emoji.
pub const DEFAULT_FALLBACKS: [&str; 5] = ["Noto Sans Symbols 2", "Noto Sans Math", "Noto Sans Symbols", "Noto Color Emoji", "Noto Emoji"];

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum FontSource {
    Bytes(u64),
    Path(Arc<str>),
    /// Neither a file nor a registration; [`FontFace::data`] still has its bytes.
    Unknown,
}

#[derive(Clone, Debug)]
pub struct FontFace {
    pub source: FontSource,
    pub index: u32,
    pub data: Blob<u8>,
}

pub struct TextContext {
    id: u64,
    /// Held weakly by each thread's `Local`, which is pruned once it goes.
    alive: Arc<()>,
    epoch: AtomicU64,
    pub(crate) state: Mutex<State>,
}

pub(crate) struct State {
    system_fonts: bool,
    collection: Collection,
    /// Bumped when the collection is replaced, so each thread re-clones it.
    pub(crate) generation: u64,
    source_cache: SourceCache,
    /// Registered again when the collection is rebuilt.
    registered: Vec<(Blob<u8>, u64)>,
    registered_blobs: HashMap<u64, u64>,
    families: [String; 3],
    fallbacks: Vec<String>,
    pub(crate) scale: f32,
    faces: Vec<FontFace>,
    face_ids: HashMap<(u64, u32), u32>,
    face_by_source: HashMap<(FontSource, u32), u32>,
    /// Id 0 is "none".
    coords: Vec<Arc<[i16]>>,
    coord_ids: HashMap<Arc<[i16]>, u32>,
}

static NEXT_CONTEXT: AtomicU64 = AtomicU64::new(1);

impl TextContext {
    pub fn new(system_fonts: bool) -> Self {
        let state = State {
            system_fonts,
            collection: new_collection(system_fonts),
            generation: 0,
            source_cache: SourceCache::new_shared(),
            registered: vec![],
            registered_blobs: HashMap::new(),
            families: DEFAULT_FAMILIES.map(str::to_owned),
            fallbacks: DEFAULT_FALLBACKS.map(str::to_owned).to_vec(),
            scale: 1.0,
            faces: vec![],
            face_ids: HashMap::new(),
            face_by_source: HashMap::new(),
            coords: vec![Arc::from([])],
            coord_ids: HashMap::new(),
        };
        Self { id: NEXT_CONTEXT.fetch_add(1, Ordering::Relaxed), alive: Arc::new(()), epoch: AtomicU64::new(0), state: Mutex::new(state) }
    }

    /// A layout made at another epoch is stale.
    pub fn epoch(&self) -> u64 {
        self.epoch.load(Ordering::Acquire)
    }

    fn changed(&self) {
        self.epoch.fetch_add(1, Ordering::AcqRel);
    }

    pub(crate) fn lock(&self) -> MutexGuard<'_, State> {
        self.state.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    /// Returns how many faces it registered.
    pub fn register_fonts(&self, bytes: Vec<u8>, bytes_id: u64) -> usize {
        let blob = Blob::new(Arc::new(bytes));
        let count = {
            let mut state = self.lock();
            let count = register(&mut state.collection, &blob);
            if count > 0 {
                state.registered_blobs.insert(blob.id(), bytes_id);
                state.registered.push((blob, bytes_id));
            }
            count
        };
        if count > 0 {
            self.changed();
        }
        count
    }

    pub fn set_family(&self, family: Family, name: &str) {
        self.lock().families[family.index()] = name.to_owned();
        self.changed();
    }

    /// Tried in order before fontique's own fallback.
    pub fn set_fallbacks<S: AsRef<str>>(&self, names: &[S]) {
        self.lock().fallbacks = names.iter().map(|n| n.as_ref().to_owned()).collect();
        self.changed();
    }

    pub fn set_scale(&self, scale: f32) {
        self.lock().scale = scale;
        self.changed();
    }

    pub fn fonts_changed(&self) {
        {
            let mut state = self.lock();
            let mut collection = new_collection(state.system_fonts);
            for (blob, _) in &state.registered {
                register(&mut collection, blob);
            }
            state.collection = collection;
            state.generation += 1;
        }
        self.changed();
    }

    /// Ids, and the path and bytes they name, stay valid for the context's life.
    pub fn font(&self, id: u32) -> Option<FontFace> {
        self.lock().faces.get(id as usize).cloned()
    }

    /// Valid for the context's life, like [`TextContext::font`].
    pub fn coords(&self, id: u32) -> Arc<[i16]> {
        self.lock().coords.get(id as usize).cloned().unwrap_or_else(|| Arc::from([]))
    }

    pub(crate) fn with_local<R>(&self, f: impl FnOnce(&mut Local) -> R) -> R {
        thread_local! {
            static LOCALS: RefCell<Vec<Local>> = const { RefCell::new(Vec::new()) };
        }
        LOCALS.with(|locals| {
            let mut locals = locals.borrow_mut();
            locals.retain(|l| l.alive.strong_count() > 0);
            let generation = self.lock().generation;
            let at = match locals.iter().position(|l| l.context == self.id) {
                Some(at) => at,
                None => {
                    locals.push(self.new_local());
                    locals.len() - 1
                }
            };
            if locals[at].generation != generation {
                locals[at] = self.new_local();
            }
            f(&mut locals[at])
        })
    }

    fn new_local(&self) -> Local {
        let state = self.lock();
        Local {
            context: self.id,
            alive: Arc::downgrade(&self.alive),
            generation: state.generation,
            fonts: FontContext { collection: state.collection.clone(), source_cache: state.source_cache.clone() },
            layout: LayoutContext::new(),
            paragraph: Layout::new(),
            ellipsis: Layout::new(),
        }
    }
}

fn new_collection(system_fonts: bool) -> Collection {
    Collection::new(CollectionOptions { shared: true, system_fonts })
}

fn register(collection: &mut Collection, blob: &Blob<u8>) -> usize {
    collection.register_fonts(blob.clone(), None).iter().map(|(_, fonts)| fonts.len()).sum()
}

pub(crate) struct Local {
    context: u64,
    alive: Weak<()>,
    generation: u64,
    pub(crate) fonts: FontContext,
    pub(crate) layout: LayoutContext<()>,
    /// Buffers only: every call rebuilds their shaping, so no layout is retained.
    pub(crate) paragraph: Layout<()>,
    pub(crate) ellipsis: Layout<()>,
}

impl State {
    pub(crate) fn family_stack(&self, family: Family) -> FontFamily<'static> {
        let names = iter::once(&self.families[family.index()])
            .chain(&self.fallbacks)
            .map(|name| FontFamilyName::Named(Cow::Owned(name.clone())))
            .collect::<Vec<_>>();
        FontFamily::List(Cow::Owned(names))
    }

    pub(crate) fn face_id(&mut self, font: &FontData, fonts: &mut FontContext) -> u32 {
        let key = (font.data.id(), font.index);
        if let Some(&id) = self.face_ids.get(&key) {
            return id;
        }
        let source = match self.registered_blobs.get(&font.data.id()) {
            Some(&bytes_id) => FontSource::Bytes(bytes_id),
            None => find_path(font, fonts).map_or(FontSource::Unknown, FontSource::Path),
        };
        // A file fontique loaded again (its cache pruned it) is the same face.
        let id = match source {
            FontSource::Unknown => None,
            ref known => self.face_by_source.get(&(known.clone(), font.index)).copied(),
        }
        .unwrap_or_else(|| {
            let id = self.faces.len() as u32;
            if source != FontSource::Unknown {
                self.face_by_source.insert((source.clone(), font.index), id);
            }
            self.faces.push(FontFace { source, index: font.index, data: font.data.clone() });
            id
        });
        self.face_ids.insert(key, id);
        id
    }

    pub(crate) fn coords_id(&mut self, coords: Vec<i16>) -> u32 {
        if let Some(&id) = self.coord_ids.get(coords.as_slice()) {
            return id;
        }
        let id = self.coords.len() as u32;
        let coords = Arc::<[i16]>::from(coords);
        self.coords.push(coords.clone());
        self.coord_ids.insert(coords, id);
        id
    }
}

// fontique doesn't say which file a resolved face came from, so compare its
// blob with the faces of the family its name table names. Not every family:
// that opens files while the state is locked, and the bytes serve instead.
fn find_path(font: &FontData, fonts: &mut FontContext) -> Option<Arc<str>> {
    let named = family_names(font).into_iter().filter_map(|name| fonts.collection.family_id(&name)).collect::<Vec<_>>();
    for family in named {
        let Some(info) = fonts.collection.family(family) else { continue };
        for face in info.fonts() {
            if face.index() != font.index {
                continue;
            }
            let SourceKind::Path(path) = face.source().kind() else { continue };
            if fonts.source_cache.get(face.source()).is_some_and(|blob| blob.id() == font.data.id()) {
                return Some(path_string(path));
            }
        }
    }
    None
}

fn path_string(path: &Path) -> Arc<str> {
    path.to_string_lossy().into()
}

fn family_names(font: &FontData) -> Vec<String> {
    let Ok(face) = FontRef::from_index(font.data.as_ref(), font.index) else { return vec![] };
    let Ok(name) = face.name() else { return vec![] };
    let data = name.string_data();
    let mut names = vec![];
    for id in [NameId::TYPOGRAPHIC_FAMILY_NAME, NameId::FAMILY_NAME] {
        for record in name.name_record() {
            if record.name_id() == id {
                if let Ok(s) = record.string(data) {
                    let s = s.chars().collect::<String>();
                    if !names.contains(&s) {
                        names.push(s);
                    }
                }
            }
        }
    }
    names
}
