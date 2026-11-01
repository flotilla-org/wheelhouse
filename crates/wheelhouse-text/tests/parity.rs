//! fontique against the platform's own font fallback (#560, part 1): for each
//! cluster of a mixed corpus, the face parley and fontique pick beside the one
//! fontconfig (Linux), DirectWrite (Windows) or CoreText (macOS) picks, from
//! the same base family and language. Three passes:
//!
//! - `system`: fontique's own fallback, nothing configured;
//! - `wheelhouse`: Wheelhouse's bundled fallbacks (DEFAULT_FALLBACKS) after
//!   the base family, as wheelhouse-text stacks them;
//! - `configured`: `wheelhouse`, after `Collection::set_fallbacks` gives each
//!   corpus line's script and language (not Common's: the bundled fonts
//!   cover symbols) the family the platform chose where `system` differed.
//!   What still differs can't be configured away: that's the gate in #560.
//!
//! The tables it prints are the evidence in docs/research/fontique-parity.md;
//! `WHEELHOUSE_PARITY_REPORT` also writes them to that path. It fails only on
//! tofu in `configured`: a cluster left without a glyph where the platform
//! has one. A different face is a finding, as each host's fonts differ.
use std::{borrow::Cow, collections::BTreeMap, fmt::Write, fs, sync::Arc};

use fontique::{Blob, Collection, CollectionOptions, FallbackKey, GenericFamily, Script, SourceCache};
use parley::{FontContext, FontData, FontFamily, FontFamilyName, Language, LayoutContext, PositionedLayoutItem, StyleProperty};
use read_fonts::{types::NameId, FileRef, FontRef, TableProvider};
use wheelhouse_text::DEFAULT_FALLBACKS;

/// (language, script, text). Languages are BCP 47, as parley takes them; a
/// line of mixed scripts has none to seed.
const CORPUS: &[(&str, Option<&str>, &str)] = &[
    ("en", Some("Latn"), "Hello, world. Ligatures: office affluent. 0123 €£¥"),
    ("en", None, "Ünïcödé façade naïve Ωμέγα Кириллица"),
    ("ja", Some("Hani"), "日本語の漢字"),
    ("ja", Some("Hira"), "ひらがなとカタカナ。"),
    ("zh-CN", Some("Hani"), "简体中文的汉字与标点。"),
    ("zh-TW", Some("Hani"), "繁體中文的漢字與標點。"),
    ("ko", Some("Hang"), "한국어 텍스트입니다."),
    ("en", Some("Zyyy"), "😀 👍🏽 👨‍👩‍👧‍👦 🏳️‍🌈 ❤️ 🇯🇵 🧑🏿‍💻"),
    ("ar", Some("Arab"), "مرحبا بالعالم ١٢٣"),
    ("he", Some("Hebr"), "שלום עולם, עִבְרִית"),
    ("hi", Some("Deva"), "नमस्ते दुनिया, हिन्दी"),
    ("th", Some("Thai"), "สวัสดีชาวโลก ภาษาไทย"),
    ("en", Some("Zyyy"), "→ ⇒ ∑ ∞ ≠ ★ ♠ ✓ ⌘ ⚠ ☃ ♪ ⏻ ␣ ▶ ●"),
    ("en", None, "Latin 中文 العربية 😀 हिन्दी ไทย עברית"),
];

#[derive(Clone, Debug, PartialEq)]
struct Face {
    family: String,
    postscript: String,
}

struct Pick {
    face: Face,
    covers: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
enum Verdict {
    Same,
    SameFamily,
    Differs,
    PlatformTofu,
    FontiqueTofu,
    /// The platform named no font (DirectWrite maps nothing to ⏻, say).
    NoPlatformFont,
    BothTofu,
}

struct Row {
    line: usize,
    cluster: String,
    fontique: Pick,
    platform: Option<Pick>,
    verdict: Verdict,
}

// It depends on the host's fonts and its fontconfig, so it isn't part of a
// plain `cargo test`: native/build.sh and build.bat run it with --ignored.
#[test]
#[ignore = "host-dependent: run with --ignored, as the native builds do"]
fn fontique_agrees_with_the_platform() {
    let mut report = String::new();
    let mut system = Pass::new(false);
    writeln!(report, "Platform: {}. Base family: {}.", platform::NAME, system.base).unwrap();
    let rows = system.run();
    section(&mut report, "system", &rows, true);

    let mut wheelhouse = Pass::new(true);
    let wheelhouse_rows = wheelhouse.run();
    section(&mut report, "wheelhouse", &wheelhouse_rows, false);

    let mut configured = Pass::new(true);
    let mut seeds = BTreeMap::<(usize, String), String>::new();
    for row in rows.iter().filter(|r| r.verdict == Verdict::Differs || r.verdict == Verdict::FontiqueTofu) {
        let (_, Some(script), _) = CORPUS[row.line] else { continue };
        if script == "Zyyy" {
            continue;
        }
        if let Some(platform) = &row.platform {
            // A line's first differing cluster names the family; the rest of
            // the line's script almost always agrees with it.
            seeds.entry((row.line, script.to_owned())).or_insert_with(|| platform.face.family.clone());
        }
    }
    for ((line, script), family) in &seeds {
        let set = configured.seed(CORPUS[*line].0, script, family);
        writeln!(report, "\nSeeded {script} for {} with {family}: {}", CORPUS[*line].0, if set { "set" } else { "not found" }).unwrap();
    }
    let configured_rows = configured.run();
    section(&mut report, "configured", &configured_rows, false);

    println!("{report}");
    if let Ok(path) = std::env::var("WHEELHOUSE_PARITY_REPORT") {
        fs::write(path, &report).unwrap();
    }
    let tofu = configured_rows
        .iter()
        .filter(|r| r.verdict == Verdict::FontiqueTofu)
        .map(|r| format!("{} {:?}: {:?}", CORPUS[r.line].0, r.cluster, r.fontique.face))
        .collect::<Vec<_>>();
    assert!(tofu.is_empty(), "configured, fontique left clusters the platform covers without glyphs:\n{}", tofu.join("\n"));
}

fn section(report: &mut String, name: &str, rows: &[Row], all: bool) {
    let mut counts = BTreeMap::<Verdict, usize>::new();
    for row in rows {
        *counts.entry(row.verdict).or_default() += 1;
    }
    writeln!(report, "\n## {name}\n\nCounts: {counts:?}\n").unwrap();
    writeln!(report, "| lang | cluster | code points | fontique | {} | verdict |", platform::NAME).unwrap();
    writeln!(report, "|---|---|---|---|---|---|").unwrap();
    let shown = |p: &Pick| format!("{} ({}){}", p.face.family, p.face.postscript, if p.covers { "" } else { " — tofu" });
    for row in rows.iter().filter(|r| all || r.verdict != Verdict::Same) {
        let platform = row.platform.as_ref().map_or_else(|| "none".to_owned(), shown);
        let (lang, _, _) = CORPUS[row.line];
        writeln!(
            report,
            "| {lang} | {} | {} | {} | {platform} | {:?} |",
            row.cluster,
            code_points(&row.cluster),
            shown(&row.fontique),
            row.verdict
        )
        .unwrap();
    }
}

struct Pass {
    fonts: FontContext,
    layout: LayoutContext<()>,
    base: String,
    bundled: bool,
}

impl Pass {
    fn new(bundled: bool) -> Self {
        let mut fonts = FontContext {
            collection: Collection::new(CollectionOptions { shared: false, system_fonts: true }),
            source_cache: SourceCache::default(),
        };
        let id = fonts.collection.generic_families(GenericFamily::SansSerif).next().expect("a sans-serif family");
        let base = fonts.collection.family_name(id).expect("its name").to_owned();
        if bundled {
            for file in [
                "NotoSansSymbols2-Regular.ttf",
                "NotoSansMath-Regular.ttf",
                "NotoSansSymbols.ttf",
                "NotoColorEmoji.ttf",
                "NotoEmoji-Regular.ttf",
            ] {
                let data = fs::read(format!("{}/../../data/{file}", env!("CARGO_MANIFEST_DIR"))).expect(file);
                fonts.collection.register_fonts(Blob::new(Arc::new(data)), None);
            }
        }
        Self { fonts, layout: LayoutContext::new(), base, bundled }
    }

    /// Whether it could seed the family, or the one fontique stands in for it: it leaves PingFang
    /// out (its outlines are hvgl) and uses Heiti in its place.
    fn seed(&mut self, lang: &str, script: &str, family: &str) -> bool {
        let stand_in = family.strip_prefix("PingFang ").map(|region| format!("Heiti {region}"));
        let Some(id) = self.fonts.collection.family_id(family).or_else(|| stand_in.and_then(|f| self.fonts.collection.family_id(&f)))
        else {
            return false;
        };
        let script = Script::from_bytes(script.as_bytes().try_into().expect("a four-letter script"));
        let language = Language::parse(lang).expect("a language");
        let key = FallbackKey::new(script, Some(&language));
        let key = if key.is_tracked() { key } else { FallbackKey::from(script) };
        self.fonts.collection.set_fallbacks(key, std::iter::once(id))
    }

    fn run(&mut self) -> Vec<Row> {
        let mut rows = vec![];
        for (line, &(lang, _, text)) in CORPUS.iter().enumerate() {
            for (cluster, fontique) in self.clusters(lang, text) {
                if cluster.chars().all(|c| c.is_whitespace() || c.is_control()) {
                    continue;
                }
                let platform = platform::pick(&self.base, lang, cluster);
                let verdict = match (fontique.covers, platform.as_ref()) {
                    (_, None) => Verdict::NoPlatformFont,
                    (false, Some(p)) if p.covers => Verdict::FontiqueTofu,
                    (false, Some(_)) => Verdict::BothTofu,
                    (true, Some(p)) if !p.covers => Verdict::PlatformTofu,
                    (true, Some(p)) if p.face.postscript == fontique.face.postscript => Verdict::Same,
                    (true, Some(p)) if p.face.family == fontique.face.family => Verdict::SameFamily,
                    (true, Some(_)) => Verdict::Differs,
                };
                rows.push(Row { line, cluster: cluster.to_owned(), fontique, platform, verdict });
            }
        }
        rows
    }

    fn clusters<'t>(&mut self, lang: &str, text: &'t str) -> Vec<(&'t str, Pick)> {
        let mut names = vec![FontFamilyName::Named(Cow::Owned(self.base.clone()))];
        if self.bundled {
            names.extend(DEFAULT_FALLBACKS.iter().map(|n| FontFamilyName::Named(Cow::Borrowed(*n))));
        }
        let mut builder = self.layout.ranged_builder(&mut self.fonts, text, 1.0, false);
        builder.push_default(StyleProperty::FontFamily(FontFamily::List(Cow::Owned(names))));
        builder.push_default(StyleProperty::FontSize(16.0));
        builder.push_default(StyleProperty::Locale(Some(Language::parse(lang).expect("a language"))));
        let mut built = builder.build(text);
        built.break_all_lines(None);
        let mut out = vec![];
        for line in built.lines() {
            for item in line.items() {
                let PositionedLayoutItem::GlyphRun(glyphs) = item else { continue };
                let run = glyphs.run();
                let face = face_of(run.font());
                for cluster in run.clusters() {
                    // A ligature's later clusters draw nothing of their own.
                    if cluster.is_ligature_continuation() {
                        continue;
                    }
                    let covers = cluster.glyphs().all(|g| g.id != 0);
                    out.push((cluster.text_range(), Pick { face: face.clone(), covers }));
                }
            }
        }
        // Logical order: lines and bidi runs visit clusters out of it.
        out.sort_by_key(|(range, _)| range.start);
        out.into_iter().map(|(range, pick)| (&text[range], pick)).collect()
    }
}

fn face_of(font: &FontData) -> Face {
    let face = FontRef::from_index(font.data.as_ref(), font.index).expect("a face");
    Face {
        family: name(&face, NameId::TYPOGRAPHIC_FAMILY_NAME).or_else(|| name(&face, NameId::FAMILY_NAME)).unwrap_or_default(),
        postscript: name(&face, NameId::POSTSCRIPT_NAME).unwrap_or_default(),
    }
}

fn name(face: &FontRef<'_>, id: NameId) -> Option<String> {
    let table = face.name().ok()?;
    let data = table.string_data();
    table.name_record().iter().filter(|r| r.name_id() == id).find_map(|r| r.string(data).ok().map(|s| s.chars().collect()))
}

/// Default-ignorable code points need no glyph of their own: ZWJ, ZWNJ,
/// variation selectors and emoji tags.
fn ignorable(c: char) -> bool {
    matches!(c, '\u{200c}' | '\u{200d}' | '\u{fe00}'..='\u{fe0f}' | '\u{e0020}'..='\u{e007f}')
}

/// Whether the face in `data` named `postscript` (or a file's only face) maps
/// every code point that needs a glyph.
#[cfg_attr(target_os = "windows", allow(dead_code))]
fn file_covers(data: &[u8], postscript: &str, cluster: &str) -> bool {
    let faces: Vec<FontRef<'_>> = match FileRef::new(data) {
        Ok(FileRef::Font(f)) => vec![f],
        Ok(FileRef::Collection(c)) => c.iter().filter_map(Result::ok).collect(),
        Err(_) => vec![],
    };
    // A lone face may misname itself; in a collection, only the named one counts.
    let face = faces
        .iter()
        .find(|f| name(f, NameId::POSTSCRIPT_NAME).as_deref() == Some(postscript))
        .or(faces.first().filter(|_| faces.len() == 1));
    let Some(cmap) = face.and_then(|f| f.cmap().ok()) else { return false };
    cluster.chars().filter(|&c| !ignorable(c)).all(|c| cmap.map_codepoint(c).is_some_and(|g| g.to_u32() != 0))
}

fn code_points(cluster: &str) -> String {
    cluster.chars().map(|c| format!("{:04X}", c as u32)).collect::<Vec<_>>().join(" ")
}

#[cfg(target_os = "linux")]
mod platform {
    use std::process::Command;

    use super::{file_covers, Face, Pick};

    pub const NAME: &str = "fontconfig";

    /// fc-match with the cluster's code points as its charset and the
    /// language, as a browser's FcFontSort query asks.
    pub fn pick(base: &str, lang: &str, cluster: &str) -> Option<Pick> {
        let charset = cluster.chars().map(|c| format!("{:x}", c as u32)).collect::<Vec<_>>().join(" ");
        let pattern = format!("{}:lang={}:charset={charset}", escape(base), fc_lang(lang));
        // A host without fontconfig has nothing to compare with: say so, rather than tabulating.
        let out = Command::new("fc-match")
            .args(["-f", "%{file}\n%{postscriptname}\n%{family[0]}", &pattern])
            .output()
            .expect("fc-match, from fontconfig, to run");
        assert!(out.status.success(), "fc-match {pattern}: {}", String::from_utf8_lossy(&out.stderr));
        let out = String::from_utf8(out.stdout).ok()?;
        let mut lines = out.lines();
        let (file, postscript, family) = (lines.next()?, lines.next()?, lines.next()?);
        let covers = std::fs::read(file).is_ok_and(|data| file_covers(&data, postscript, cluster));
        Some(Pick { face: Face { family: family.to_owned(), postscript: postscript.to_owned() }, covers })
    }

    fn escape(family: &str) -> String {
        family.chars().flat_map(|c| if matches!(c, '-' | ':' | ',' | '\\') { vec!['\\', c] } else { vec![c] }).collect()
    }

    fn fc_lang(lang: &str) -> String {
        match lang {
            "zh-Hans" => "zh-cn".to_owned(),
            "zh-Hant" => "zh-tw".to_owned(),
            other => other.to_ascii_lowercase(),
        }
    }
}

#[cfg(target_os = "macos")]
mod platform {
    use objc2_core_foundation::{CFRange, CFString, CFURLPathStyle, CFURL};
    use objc2_core_text::{kCTFontURLAttribute, CTFont};

    use super::{file_covers, Face, Pick};

    pub const NAME: &str = "CoreText";

    /// CTFontCreateForStringWithLanguage from the base family: CoreText's
    /// cascade list for that language.
    pub fn pick(base: &str, lang: &str, cluster: &str) -> Option<Pick> {
        let lang = match lang {
            "zh-CN" => "zh-Hans",
            "zh-TW" => "zh-Hant",
            other => other,
        };
        unsafe {
            let base = CTFont::with_name(&CFString::from_str(base), 16.0, std::ptr::null());
            let text = CFString::from_str(cluster);
            let range = CFRange { location: 0, length: text.length() };
            let font = base.for_string_with_language(&text, range, Some(&CFString::from_str(lang)));
            let face = Face { family: font.family_name().to_string(), postscript: font.post_script_name().to_string() };
            let path = font
                .attribute(kCTFontURLAttribute)
                .and_then(|url| url.downcast::<CFURL>().ok())
                .and_then(|url| url.file_system_path(CFURLPathStyle::CFURLPOSIXPathStyle))
                .map(|p| p.to_string());
            let covers = path.and_then(|p| std::fs::read(p).ok()).is_some_and(|data| file_covers(&data, &face.postscript, cluster));
            Some(Pick { face, covers })
        }
    }
}

#[cfg(target_os = "windows")]
mod platform {
    use windows::{
        core::{implement, Interface, OutRef, Result, PCWSTR},
        Win32::Graphics::DirectWrite::{
            DWriteCreateFactory, IDWriteFactory, IDWriteFactory2, IDWriteFont, IDWriteFontCollection, IDWriteLocalizedStrings,
            IDWriteNumberSubstitution, IDWriteTextAnalysisSource, IDWriteTextAnalysisSource_Impl, DWRITE_FACTORY_TYPE_SHARED,
            DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME,
            DWRITE_READING_DIRECTION, DWRITE_READING_DIRECTION_LEFT_TO_RIGHT,
        },
    };

    use super::{ignorable, Face, Pick};

    pub const NAME: &str = "DirectWrite";

    /// IDWriteFontFallback::MapCharacters over the system collection, from
    /// the base family, for the cluster in that locale.
    pub fn pick(base: &str, lang: &str, cluster: &str) -> Option<Pick> {
        let text: Vec<u16> = cluster.encode_utf16().collect();
        let locale: Vec<u16> = lang.encode_utf16().chain([0]).collect();
        let family: Vec<u16> = base.encode_utf16().chain([0]).collect();
        unsafe {
            let factory: IDWriteFactory2 = DWriteCreateFactory::<IDWriteFactory>(DWRITE_FACTORY_TYPE_SHARED).ok()?.cast().ok()?;
            let mut collection: Option<IDWriteFontCollection> = None;
            factory.GetSystemFontCollection(&mut collection, false).ok()?;
            let collection = collection?;
            let fallback = factory.GetSystemFontFallback().ok()?;
            let source: IDWriteTextAnalysisSource = Source { text: &text, locale: &locale }.into();
            let (mut mapped_len, mut mapped, mut scale) = (0, None, 1.0);
            fallback
                .MapCharacters(
                    &source,
                    0,
                    text.len() as u32,
                    &collection,
                    PCWSTR::from_raw(family.as_ptr()),
                    DWRITE_FONT_WEIGHT_REGULAR,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    &mut mapped_len,
                    &mut mapped,
                    &mut scale,
                )
                .ok()?;
            let font: IDWriteFont = mapped?;
            let family = string(&font.GetFontFamily().ok()?.GetFamilyNames().ok()?).unwrap_or_default();
            let mut info = None;
            let mut exists = Default::default();
            font.GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, &mut info, &mut exists).ok()?;
            let postscript = info.as_ref().and_then(string).unwrap_or_default();
            // MapCharacters maps a prefix it can; the rest is tofu here.
            let covers = mapped_len as usize == text.len()
                && cluster.chars().filter(|&c| !ignorable(c)).all(|c| font.HasCharacter(c as u32).is_ok_and(|b| b.as_bool()));
            Some(Pick { face: Face { family, postscript }, covers })
        }
    }

    fn string(strings: &IDWriteLocalizedStrings) -> Option<String> {
        unsafe {
            let len = strings.GetStringLength(0).ok()? as usize;
            let mut buf = vec![0u16; len + 1];
            strings.GetString(0, &mut buf).ok()?;
            buf.pop();
            Some(String::from_utf16_lossy(&buf))
        }
    }

    // As fontique's own (fontique/src/backend/dwrite.rs). The pointers it
    // hands out are into slices that outlive the call; DirectWrite only reads
    // through them, so casting away const is sound.
    #[implement(IDWriteTextAnalysisSource)]
    struct Source<'a> {
        text: &'a [u16],
        locale: &'a [u16],
    }

    impl IDWriteTextAnalysisSource_Impl for Source_Impl<'_> {
        fn GetLocaleName(&self, position: u32, length: *mut u32, locale: *mut *mut u16) -> Result<()> {
            unsafe {
                *length = (self.text.len() as u32).saturating_sub(position);
                *locale = self.locale.as_ptr().cast_mut();
            }
            Ok(())
        }

        fn GetNumberSubstitution(
            &self,
            position: u32,
            length: *mut u32,
            substitution: OutRef<'_, IDWriteNumberSubstitution>,
        ) -> Result<()> {
            substitution.write(None)?;
            unsafe { *length = (self.text.len() as u32).saturating_sub(position) };
            Ok(())
        }

        fn GetParagraphReadingDirection(&self) -> DWRITE_READING_DIRECTION {
            DWRITE_READING_DIRECTION_LEFT_TO_RIGHT
        }

        fn GetTextAtPosition(&self, position: u32, text: *mut *mut u16, length: *mut u32) -> Result<()> {
            let rest = self.text.get(position as usize..).unwrap_or_default();
            unsafe {
                *length = rest.len() as u32;
                *text = rest.as_ptr().cast_mut();
            }
            Ok(())
        }

        fn GetTextBeforePosition(&self, position: u32, text: *mut *mut u16, length: *mut u32) -> Result<()> {
            let before = self.text.get(..position as usize).unwrap_or_default();
            unsafe {
                *length = before.len() as u32;
                *text = before.as_ptr().cast_mut();
            }
            Ok(())
        }
    }
}
