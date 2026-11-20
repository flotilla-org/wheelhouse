# Identity colours for an open-ended set of entities (researched 2026-11-19)

Scope: how Wheelhouse should pick a stable colour for each project (and later local groups,
convoys, vessels, agents), covering perceptual colour spaces, how many categorical colours people
can tell apart, assignment strategies for a growing set, adapting to light, dark and user themes,
and what shipping products do. I read facts from primary sources (papers, specs, official docs,
source via `gh api` or raw fetches) on 2026-11-19. Anything marked **[memory]** is my prior
knowledge and was not re-verified today. Numbers marked *(computed)* come from a throwaway Python
script using Ottosson's OKLab matrices, CIE 1976 L\*a\*b\* (D65) and the Machado 2009 severity-1.0
matrices applied to linear RGB. The script is not checked in.

---

## 0. Key findings

1. **The current tint carries almost no identity.** The palette literals are used as *linear* RGB, so they render
   as pastels (OKLCH L≈0.84–0.86, C≈0.04–0.055). Mixed into the background at 3.5%, two
   projects' cards differ from each other by ΔE_OK 0.001 on Default (Light) and 0.007 on Default (Dark). The CSS gamut
   mapper treats 0.02 as one just-noticeable difference (JND). On dark themes the card does show, but as a lighter card,
   not as a hue (§1.2).
2. **A faint tint has very little chroma room.** Near a light background (OKLab L≈0.97), the largest chroma that every
   hue can reach inside sRGB is about 0.015. Twelve evenly spaced hues at that chroma sit about 0.004 apart.
   Paul Tol says his "pale" scheme is for backgrounds and is not very distinct in either normal or
   colour-blind vision (§2.4, §3.3).
3. **Small marks need large differences.** Szafir 2018: perceived colour difference varies inversely
   with mark size. A 6 px point needs about 16 Δa\* to be noticed half the time. A 3 px strip is below the
   smallest size tested (§3.2).
4. **The ceiling for colours alone is about 6–9.** Healey 1996 found 7 isoluminant colours to be the limit for
   rapid identification. Tol's colour-blind-safe qualitative sets have 6–9 colours. ColorBrewer rates its qualitative
   sets colour-blind safe only at 3–4 classes. Under deuteranopia simulation, Tableau 10's closest
   pair is ΔE_OK 0.007 apart *(computed)* (§3).
5. **Hashing gives collisions, by the birthday bound.** With K=6 colours and 4 visible projects, P(some pair shares a colour) is
   72%. With K=12 and 6 visible it is 78%. Hashing to a *continuous* hue is worse in practice: with 6 projects,
   P(every hue gap ≥ 30°) is about 3% (§4.1–4.2).
6. **Shipping products persist assignments and derive colours per theme.** None of the systems below derives an identity colour
   purely from a hash on each frame, except avatar identicons. Chrome assigns the least-used of 9 slots and stores it. Every
   product stores the choice (or the user's override). Chrome, Apple, Radix, Material and Primer keep one
   *identity* and derive separate values per mode and per use (§6).

---

## 1. Current state in Wheelhouse

### 1.1 Code

| Where | What it does |
|---|---|
| `native/src/uishell/uishell_sidebar.c` `uishell_sidebar_project_accent` | `colors[u64_hash_from_str8(identity) % 6]`. Six literal `v4f32` values: (0.40,0.66,0.83), (0.66,0.55,0.79), (0.74,0.64,0.40), (0.43,0.72,0.61), (0.78,0.53,0.49), (0.46,0.69,0.72). The comment says "Presentation defaults are local policy … A future metadata suggestion/local override can replace this resolver." |
| same file, `uishell_sidebar_card_accent` | A `.group` gets `ui_color_from_name("text")` with `w = 0.35`; anything else gets the project accent. |
| same file, render (~L4947, ~L5044) | Card and member-row background = `mix_4f32(ui_color_from_name("background"), accent, 0.035f)`. |
| working tree (this branch) | Removes the 3 px `###accent_%S` strip from `uishell_sidebar_row_begin`. |
| `native/src/base/base_strings.c` `u64_hash_from_str8` | XXH3-64 with seed 5381. It is stable across runs, but changing the palette length or order reshuffles every project. |
| `native/src/base/base_math.c` | Already has `linear_from_srgb`, `srgb_from_linear`, `oklab_from_linear` and `linear_from_oklab` (Ottosson's matrices). Nothing outside `base_math` calls the OKLab functions yet. |
| `native/src/shell/shell_core.c` ~L11605 | Theme colours are parsed as sRGB hex and stored **linear** (`linear_from_srgba(rgba_from_u32(...))`). UI colours are therefore linear, and `mix_4f32` blends in linear light. |
| `native/src/shell/shell.mdesk` `RD_ThemePresetTable` | 11 presets: Default Dark (`background` #1f1f1f), Default Light (#f4f4f4), VS Dark/Light (#1f1f1f / #ffffff), Solarized Dark/Light (#002a35 / #fdf6e3), Handmade Hero (#0c0c0c), Naysayer (#042327), 4coder (#0c0c0c), Grove (#1b1f22), Far Manager (#000080, saturated blue). Users can override any `theme_color` tag (`rd_theme_color_cfgs_from_user_project`). An identity colour therefore has to work on light, dark *and chromatic* backgrounds it has never seen. |
| `native/src/shell/shell_core.c` `rd_hover_alpha` and the `uishell_panel_diagnostics.c` test | Precedent for theme-relative derivation: the hover alpha is solved so the visible sRGB step is equal on light and dark backgrounds. |
| `crates/` | No per-entity colour choice. `wheelhouse-core` has `RailRgbColor` (a configured rail segment colour) and a template rule that frontends own colours (`template_config.rs:1832`). No other entity-colour code exists in `native/` (searched for hash/colour/palette/hue). |

Design notes: `docs/design/andamento-native-reconciliation.md` says "Project identity should have a stable, overridable
colour accent, independent of workspace selection and status. Avoid recolouring the entire workspace", and
"The palette is a local default, independent of sort order, labels and workspace selection". Future
resolution "should accept producer icon/colour suggestions, then apply local overrides ahead of
defaults … separate from entity role and grouping identity".

### 1.2 What the current palette actually shows *(computed)*

| Literal (as linear) | Rendered sRGB | OKLCH (as rendered) | OKLCH if it had been sRGB |
|---|---|---|---|
| 0.40,0.66,0.83 | #aad4eb | L .848 C .055 h 231 | L .705 C .093 h 239 |
| 0.66,0.55,0.79 | #d4c4e6 | L .844 C .050 h 307 | L .689 C .093 h 305 |
| 0.74,0.64,0.40 | #dfd1aa | L .863 C .055 h 91 | L .725 C .085 h 88 |
| 0.43,0.72,0.61 | #afddcd | L .860 C .052 h 172 | L .725 C .085 h 168 |
| 0.78,0.53,0.49 | #e5c0ba | L .839 C .042 h 30 | L .685 C .081 h 29 |
| 0.46,0.69,0.72 | #b5d8dd | L .859 C .038 h 205 | L .720 C .062 h 206 |

At full strength the closest pair is ΔE_OK 0.029 (ΔE\*ab 10). Under simulated deuteranopia it drops to 0.008, under protanopia 0.025, under tritanopia 0.008.
Two hues (h 205 and h 231) are only 26° apart.

| Theme bg | card vs bg ΔE_OK | card vs card, min ΔE_OK | min ΔE\*ab |
|---|---|---|---|
| Default Dark #1f1f1f | 0.085–0.091 | **0.0070** | 2.5 |
| Default Light #f4f4f4 | 0.0035–0.0041 | **0.0008** | 0.3 |
| Solarized Light #fdf6e3 | 0.0035–0.0045 | 0.0009 | 0.3 |
| Solarized Dark #002a35 | 0.076–0.084 | 0.0057 | 2.0 |
| Far Manager #000080 | 0.084–0.096 | 0.0031 | 1.4 |

Reading: on dark themes the tint reads as "this card is lighter", mostly a lightness lift, because the accents
are much lighter than the background. It does not read as "this is project X". On light themes the card is invisible
against the background. Mixing in linear light makes this worse on light backgrounds, because a fixed linear fraction is a much
smaller perceptual step near white. Even raising the mix to 10% gives only 0.0035 card-to-card on Default Light
for 8 hues at L .7, C .1.

---

## 2. Perceptual colour spaces

### 2.1 Spaces

| Space | What it is | Notes for us |
|---|---|---|
| **OKLab / OKLCH** | Ottosson, 23 Dec 2020: built to be "simple to use, while doing a good job at predicting perceived lightness, chroma and hue". Lightness and chroma were fit to data generated with CAM16; hue was fit to the uniform-hue data used to derive IPT, with the same structure as IPT. CIELAB/CIELUV have an "inability to predict hue. In particular blue hues are predicted badly" ([post](https://bottosson.github.io/posts/oklab/)). CSS Color 4 adopts `oklab()`/`oklch()` and defines ΔE_OK ([spec §20.3](https://www.w3.org/TR/css-color-4/#color-difference-OK)). | Already in `base_math.c`. Two matrix multiplies and a cube or cube root: cheap per frame. |
| **CIELAB + CIEDE2000** | CIE 1976 L\*a\*b\*; ΔE2000 corrects its non-uniformity (hue/chroma weighting, the blue region). Reference implementation and test data: Sharma, Wu & Dalal 2005, *Color Res. Appl.* 30(1) ([doi:10.1002/col.20070](https://doi.org/10.1002/col.20070)). | The vis literature (Healey, Szafir, Colorgorical) reports thresholds in CIELAB/ΔE2000. ΔE2000 is about 60 lines and was not designed for small-mark or categorical judgements. |
| **CAM16-UCS** | Li et al. 2017, "Comprehensive color solutions: CAM16, CAT16, and CAM16-UCS", *Color Res. Appl.* 42(6) ([doi:10.1002/col.22131](https://doi.org/10.1002/col.22131)) **[memory: citation details]**. A full appearance model with viewing conditions and a uniform space. | The most accurate option and the heaviest. Only HCT uses it in UI practice. |
| **HCT (Material)** | "CAM16 hue and chroma, and L\* from L\*a\*b\*". "A difference of 40 in HCT tone guarantees a contrast ratio >= 3.0, and a difference of 50 guarantees a contrast ratio >= 4.5" ([hct.ts](https://github.com/material-foundation/material-color-utilities/blob/main/typescript/hct/hct.ts)). Converting to sRGB needs an iterative solver (`hct_solver.ts`). | The tone↔contrast guarantee is the attractive property. OKLab L is not L\*, so the guarantee does not transfer exactly; check contrast numerically instead (§5.3). |

### 2.2 Why HSL hue rotation gives uneven lightness

Ottosson's Okhsl/Okhsv post (8 Sep 2021) says HSL lightness "does not match the perception of lightness well at all for
saturated colors", and that HSV hue "has quite severe distortions" ([post](https://bottosson.github.io/posts/colorpicker/)).
Measured *(computed)*: HSL(h, 65%, 60%) rotated in 30° steps spans OKLab L from 0.534 (h 240, blue) to 0.868 (h 60,
yellow), a range of 0.33. That is larger than the gap between a text colour and a weak-text colour in most themes. Any
"hash → HSL hue" scheme (GitHub identicons, Martin Ankerl's golden-ratio generator, Peacock's tinycolor maths) inherits this.

### 2.3 Gamut mapping into sRGB

CSS Color 4 §14.2 "Binary Search Gamut Mapping with Local MINDE" ([spec](https://www.w3.org/TR/css-color-4/#binary-search-gamut-map-with-local-minde)):
convert to OKLCH. If L ≥ 1 return white; if L ≤ 0 return black. If the colour is in gamut, return it. Otherwise binary-search chroma
(`epsilon` 0.0001), and at each step clip to sRGB. Stop when ΔE_OK(clipped, current) is just under `JND = 0.02`.
Hue and lightness are held. The spec also describes an "EdgeSeeker" LUT method (§14.2.3). Ottosson's
"sRGB gamut clipping" post gives closed-form cusp-based alternatives ([post](https://bottosson.github.io/posts/gamutclipping/)).
For identity colours we can avoid mapping entirely by choosing a chroma at or below the **all-hue ceiling** for the chosen L:

### 2.4 sRGB chroma ceiling for all hues at a given OKLab L *(computed)*

| OKLab L | max chroma every hue reaches | limiting hue | best hue's max |
|---|---|---|---|
| 0.25 | 0.043 | ~200 (cyan) | 0.170 |
| 0.40 | 0.068 | ~200 | 0.272 |
| 0.50 | 0.085 | ~200 | 0.294 |
| 0.60 | 0.102 | ~200 | 0.300 |
| 0.70 | 0.119 | ~200 | 0.321 |
| 0.75 | **0.128** (the widest band) | ~200 | 0.258 |
| 0.80 | 0.100 | ~268 (blue) | 0.269 |
| 0.90 | 0.048 | ~268 | 0.250 |
| 0.95 | 0.024 | ~268 | 0.217 |
| 0.97 | 0.014 | ~268 | — |

An equal-chroma hue ring (all identities the same "weight") is limited to C ≈ 0.10–0.12 for marks at L 0.6–0.75.
For tints, the limit is about 0.04 next to a dark background and about 0.015 next to a light one.

---

## 3. How many categorical colours people can tell apart

### 3.1 Counts from the literature

| Source | Finding |
|---|---|
| Healey 1996, "Choosing Effective Colours for Data Visualization", IEEE Vis '96 ([UBC TR-96-10 PDF](https://www.cs.ubc.ca/sites/default/files/tr/1996/TR-96-10_0.pdf)) | Colour distance, linear separation and colour category all matter. "It appears that seven isoluminant colours is the maximum we can display at one time, while still allowing rapid and accurate identification of any one of the colours." With 7 and 9 colours, some targets (purple, yellow, red, blue-green, red-purple) became slow. The paper also cites a military guideline of "no more than five to seven colours". |
| Ware, *Information Visualization: Perception for Design* | **[memory]** Recommends at most about 12 colours for nominal coding, drawn from the basic colour names (red, green, yellow, blue, black, white, pink, cyan, grey, orange, brown, purple), with fewer (about 5–8) for reliable use on small symbols. |
| Glasbey, van der Heijden, Toh & Gray 2007, "Colour displays for categorical images", *Color Res. Appl.* 32(4) ([doi:10.1002/col.20327](https://doi.org/10.1002/col.20327)) | **[memory: method details]** Picks each next colour to maximise its minimum CIELAB distance from those already chosen, optionally within lightness/chroma bounds. Produces long lists (colorcet ships 256), but distances fall quickly after about 10. |
| Harrower & Brewer 2003, "ColorBrewer.org", *Cartographic Journal* 40(1) ([doi:10.1179/000870403235002042](https://doi.org/10.1179/000870403235002042)); scheme data in [`colorbrewer_schemes.js`](https://github.com/axismaps/colorbrewer/blob/master/colorbrewer_schemes.js) | Qualitative sets top out at Set1 9, Set2 8, Set3 12, Pastel1 9, Pastel2 8, Dark2 8, Accent 8, Paired 12. The `blind` metadata (1 = safe, 2 = possibly, 0 = not) gives Set2 and Dark2 safe only at 3 classes and Paired at 3–4. Every qualitative set is 0 beyond about 5–6. |
| Gramazio, Laidlaw & Schloss 2017, "Colorgorical", IEEE TVCG 23(1) ([source](https://github.com/connorgr/colorgorical)) | Samples palettes by weighted scores: `ciede2000` (perceptual distance), `nameDifference` and `nameUniqueness` (from Heer & Stone's colour-naming model) and `pairPreference`. Defaults are 1, 1, 0, 1 ([model.py](https://github.com/connorgr/colorgorical/blob/master/src/model/model.py)). Discriminability and preference trade off. |
| Tableau 10 (Stone, with Cristy Miller, 2016) ([blog](https://www.tableau.com/blog/colors-upgrade-tableau-10-56782); the page returned 403 today) | **[memory]** Designed for distinct, *nameable* hues with medium saturation; Tableau 20 adds light/dark pairs. Hex values from [d3-scale-chromatic](https://github.com/d3/d3-scale-chromatic/blob/main/src/categorical/Tableau10.js). *(computed)*: min ΔE_OK 0.083 normally, **0.007 under deuteranopia** and 0.011 under protanopia. Some pairs merge for dichromats. |
| Paul Tol, qualitative schemes ([page](https://sronpersonalpages.nl/~pault/)) | Colour-blind safe: Bright 7, Vibrant 7, Muted 9, Medium-contrast 6, High-contrast 3. Dark 11 is safe "with only some cost". Light 9 is "reasonably distinct". **Pale 7 is for backgrounds and not very distinct**. "For more than nine distinct colours, a sequential colour scheme can be (mis)used." |
| Okabe & Ito, Color Universal Design ([page](https://jfly.uni-koeln.de/color/)) | Their colour-blind barrier-free palette is about 8 colours. Principle: "Use not only different colors but also a combination of different shapes, positions, line types and coloring patterns." "Do not convey information in color only." |

**Consensus:** about 6–9 colours for reliable identification at full chroma on a mark of reasonable size, fewer once CVD is
considered, and fewer again for pale/tinted fills.

### 3.2 Mark size

Szafir 2018, "Modeling Color Difference for Visualization Design", IEEE TVCG 24(1) ([paper PDF](https://par.nsf.gov/servlets/purl/10074616),
[project](https://cmci.colorado.edu/visualab/VisColors/)): "perceived color difference varies inversely with size and …
elongated marks provide significantly greater discriminability". Points from 0.25° (6 px) to 2° (50 px) were tested. For points, the 50%-JND along L\* is
`ND_L(p,s) = p / (0.0937 − 0.0085/s)` (s in degrees). That gives about 8.4 ΔE\* at 0.25° and about 5.6 at 2°, against the textbook "1 ΔE ≈ 1 JND" for large patches.
The talk slides summarise a 6 px point as needing **16 Δa\*** and a 6 px line **9.4 Δa\*** ([slides](https://cmci.colorado.edu/visualab/VisColors/17-VIS-ModelingColorDifference.pdf)).
The model builds on Stone, Szafir & Setlur 2014, "An Engineering Model for Color Difference as a Function of Size" (CIC 22) **[memory: venue]**.
A 3 px sidebar strip at desk distance is roughly 0.06–0.1°, below the tested range. Extrapolating says it needs very large
differences, consistent with the user's "doesn't work". A whole card is large in area, but a 3.5% tint has almost no
ΔE to spend (§1.2).

### 3.3 Faint tints

The table in §2.4 bounds the problem. Next to Default Light (L 0.967) the all-hue chroma ceiling is about 0.015. At that
chroma, 8 evenly spaced hues are 2·0.015·sin(π/8) ≈ 0.011 ΔE_OK apart and 12 are about 0.008 apart, all under the 0.02 JND.
Next to a dark bg (L ≈ 0.25) the ceiling is about 0.043: 8 hues about 0.033 apart, 12 about 0.022. **A tint can carry
"belongs to the same group as its header", plus perhaps 3–4 coarse hue families on dark themes. It cannot carry identity among
a dozen projects.** Radix's measured background steps agree (§5.2): step 3 is C ≈ 0.02 in light mode and C ≈ 0.03–0.066 in dark mode.

### 3.4 Colour-vision deficiency

- Prevalence: red-green deficiency affects about 8% of males and about 0.5% of females of European descent (Birch 2012, *JOSA A* 29(3)
  [doi:10.1364/JOSAA.29.000313](https://doi.org/10.1364/JOSAA.29.000313)) **[memory: exact figures]**. Most of these are
  anomalous trichromats, milder than the dichromat simulations below.
- Simulation: Brettel, Viénot & Mollon 1997 *JOSA A* 14(10) ([doi:10.1364/JOSAA.14.002647](https://doi.org/10.1364/JOSAA.14.002647))
  and the single-matrix simplification of Viénot, Brettel & Mollon 1999 *Color Res. Appl.* 24(4)
  ([doi:10.1002/(SICI)1520-6378(199908)24:4<243::AID-COL5>3.0.CO;2-3](https://doi.org/10.1002/(SICI)1520-6378(199908)24:4%3C243::AID-COL5%3E3.0.CO;2-3)).
  Machado, Oliveira & Fernandes 2009, IEEE TVCG 15(6), give 3×3 matrices for protan, deutan and tritan anomaly at severities
  0.0–1.0, where 1.0 is dichromacy ([page](https://www.inf.ufrgs.br/~oliveira/pubs_files/CVD_Simulation/CVD_Simulation.html)).
  Apply them to linear RGB **[memory: the page does not state the space]**. Each is one 3×3 multiply, cheap enough for a
  debug toggle or a diagnostics test.
- *(computed)*: 12 hues at OKLCH L 0.7, C 0.10 have min ΔE_OK 0.052 normally, 0.020 for protanopes, **0.004 for deuteranopes** and 0.009 for tritanopes.
  8 hues have 0.077 / 0.030 / 0.003 / 0.021. Equal-lightness hue rings collapse for dichromats; only lightness differences survive. This is why
  Okabe–Ito and Tol vary lightness inside their palettes, and why both Apple and Okabe–Ito say not to rely on colour alone.

---

## 4. Assignment strategies for an open-ended set

### 4.1 Hash into a fixed palette: birthday collisions *(computed)*

P(at least two of N visible entities share a colour) = 1 − K!/((K−N)!·K^N):

| N visible \ K colours | 6 | 8 | 9 | 10 | 12 | 16 | 24 | 64 |
|---|---|---|---|---|---|---|---|---|
| 3 | 44% | 34% | 31% | 28% | 24% | 18% | 12% | 5% |
| 4 | 72% | 59% | 54% | 50% | 43% | 33% | 23% | 9% |
| 6 | 98% | 92% | 89% | 85% | 78% | 66% | 49% | 21% |
| 8 | 100% | 100% | 99% | 98% | 95% | 88% | 73% | 37% |
| 10 | 100% | 100% | 100% | 100% | 100% | 97% | 89% | 52% |

Expected colliding pairs = N(N−1)/2K. For N = 10 that is 7.5 pairs at K = 6 and 3.75 at K = 12. Today's K = 6 gives a 72% chance that
4 projects include a duplicate. Raising K past about 9 to cut collisions pushes below the distinguishability floor of §3. **Hashing
cannot be both collision-free and distinguishable**, so some choice has to be stored somewhere.

### 4.2 Hash to a continuous hue, or golden-angle sequences

- **Hash to hue** (GitHub identicons, §6): for N uniform random hues, P(every adjacent gap ≥ δ) = (1 − Nδ/360)^(N−1). With N = 6, δ = 30° that is 3.1%.
  With N = 6, δ = 20° it is 13%; N = 8, δ = 20° is 1.6%; N = 10, δ = 15° is 0.8% *(computed)*. Random hues almost always contain a near pair.
- **Golden angle** (≈137.5°; Ankerl 2009 uses 1/φ steps in HSV with s = 0.5, v = 0.95,
  [post](https://martin.ankerl.com/2009/12/09/how-to-create-random-colors-programmatically/)): by the three-gap theorem the
  min gap after n insertions stays near the even spacing. Min gap is 85° at n = 3 (even 120°), 52.5° at 4–5, 32.5° at 6–8 (even 45° at 8), 20.1° at 10–12 and 12.4° at 16 *(computed)*.
  It needs a **stable insertion index**, which is a persisted counter, not a hash. It degrades gracefully, but it
  never reaches the distinctness of a designed 8-colour set.

### 4.3 Stable assignment that avoids collisions

Chromium `TabGroupModel::GetNextColor` counts each colour's use across existing groups, takes the minimum count, and returns the
first colour with that count in a fixed order: blue, purple, pink, red, orange, yellow, green, cyan, grey under the
colour-refresh flag ([tab_group_model.cc](https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/ui/tabs/tab_group_model.cc)).
The result is stored on the group; it is not recomputed. Variants:

- *Least-used among all known* entities (Chrome) is stable once stored and spreads colours evenly.
- *Least-used among currently visible* entities avoids on-screen duplicates, but a colour would change when visibility changes unless it is persisted at first sight. Changing identity colours violate "stable".
- *Hash for the preferred slot, probe on conflict* (open addressing) is deterministic for a given set, but adding an
  entity can bump another one's colour, so it also needs persistence to stay stable.

### 4.4 Persisted, user-overridable

Every product in §6 except identicons stores the colour per entity: Chrome per group, Firefox per container, Linear `Project.color: String!`,
Google `calendarList.backgroundColor`/`colorId`, Peacock in `.vscode/settings.json`, macOS Calendar per calendar.
The andamento note asks for exactly this layering: producer suggestion → local override → local default.

### 4.5 A second channel

When colours run out or collide, products add a non-colour channel: Linear's project `icon` ("an emoji or a decorative icon type", alongside the required colour;
[schema](https://github.com/linear/linear/blob/master/packages/sdk/src/schema.graphql)), Firefox container icons
([`getSupportedIcons`](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/contextualIdentities/getSupportedIcons)),
Gravatar's `initials` default ("profile name as initials, with a generated background and foreground color",
[docs](https://docs.gravatar.com/sdk/images/)), and the identicon's shape. WCAG 1.4.1 Use of Color (A) requires that
colour is not the only means of conveying information ([Understanding 1.4.1](https://www.w3.org/WAI/WCAG22/Understanding/use-of-color.html)).
Apple's HIG says: "Avoid relying solely on color to differentiate between objects" ([HIG Color](https://developer.apple.com/design/human-interface-guidelines/color)).
In the Wheelhouse sidebar the project name is already the primary channel; colour is redundant reinforcement, which is also why
a weak colour channel is tolerable.

---

## 5. Adapting identity colours to themes

### 5.1 Pattern: keep the hue, derive lightness and chroma per theme and per use

Each system below stores one identity (a hue, a named slot or a hex) and derives several **roles** per mode:

| System | Identity stored | Roles derived per mode |
|---|---|---|
| Chrome tab groups | `TabGroupColorId` (9 named slots) | Per slot and mode: frame active/inactive, dialog, context menu, saved-group foreground and outline, bookmark bar, plus chip and text variants under the refresh flag. The classic palette uses Google Blue 300 on dark and Blue 600 on light ([chrome_color_mixer.cc](https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/ui/color/chrome_color_mixer.cc)). |
| Apple system colours | 12 chromatic + grey (`blue … yellow`, `gray`) | Light, dark, increased-contrast light and increased-contrast dark. "Avoid hard-coding system color values … actual color values may fluctuate from release to release" ([HIG](https://developer.apple.com/design/human-interface-guidelines/color)). |
| Radix Colors | 31 scales (6 grey, 25 colour), each with a `…Dark` twin ([src](https://github.com/radix-ui/colors/tree/main/src)) | 12 steps by use: 1–2 backgrounds, 3–5 component backgrounds, 6–8 borders, 9–10 solids, 11–12 text. Steps 11 and 12 reach APCA Lc 60 and Lc 90 on step 2 ([docs](https://www.radix-ui.com/colors/docs/palette-composition/understanding-the-scale)). |
| Material 3 | Source hue in HCT; tonal palette at tones 0–100 | 2021 spec: `primary` tone 40 light / 80 dark; `primary_container` 90 light / 30 dark ([color_spec_2021.ts](https://github.com/material-foundation/material-color-utilities/blob/main/typescript/dynamiccolor/color_spec_2021.ts)). Custom colours are "harmonized": hue rotated toward the source by min(Δ/2, 15°) ([blend.ts](https://github.com/material-foundation/material-color-utilities/blob/main/typescript/blend/blend.ts)). |
| GitHub/Primer labels | Arbitrary hex | Light: bg = label colour; text black or white by a luma switch at 0.453; border darkened 25 L when the label is very light. Dark: bg = label colour at **18% alpha**; text = same H/S with L raised toward a 0.6 threshold; border at 30% alpha ([IssueLabelToken.module.css](https://github.com/primer/react/blob/main/packages/react/src/Token/IssueLabelToken.module.css)). Uses HSL and Rec. 709 luma on sRGB-encoded values. |

### 5.2 Radix scales in OKLCH *(computed from light.ts/dark.ts)*

| Scale | step 2 (light) | step 3 (light) | step 9 | step 11 (light) | step 2 (dark) | step 3 (dark) | step 11 (dark) |
|---|---|---|---|---|---|---|---|
| tomato | L .98 C .008 | L .95 C .021 | L .63 C .194 | L .57 C .198 | L .21 C .017 | L .25 C .055 | L .78 C .131 |
| amber | L .99 C .024 | L .97 C .068 | L .85 C .157 | L .57 C .129 | L .21 C .018 | L .26 C .044 | L .86 C .173 |
| grass | L .98 C .010 | L .96 C .022 | L .65 C .147 | L .53 C .129 | L .21 C .013 | L .27 C .030 | L .78 C .142 |
| teal | L .98 C .009 | L .96 C .026 | L .65 C .114 | L .55 C .101 | L .22 C .016 | L .27 C .038 | L .79 C .147 |
| blue | L .98 C .009 | L .96 C .020 | L .65 C .193 | L .56 C .162 | L .21 C .030 | L .27 C .066 | L .76 C .126 |
| violet | L .98 C .009 | L .96 C .019 | L .54 C .179 | L .51 C .159 | L .21 C .032 | L .27 C .065 | L .78 C .125 |

Hue stays within a few degrees across steps and modes. Step 9 is identical in both modes. Background steps are near-achromatic
in light mode and 2–3× more chromatic in dark mode. That is the "fixed hue, per-theme L/C" pattern, and it matches the gamut
ceilings in §2.4.

### 5.3 Contrast targets

- **WCAG 2.x SC 1.4.11 Non-text Contrast (AA):** user-interface components and graphical objects "required to understand the content"
  need 3:1 against adjacent colours ([Understanding 1.4.11](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html)).
  A decorative identity tint beside a text name is arguably not *required*, so 1.4.11 does not apply to it. If a mark (dot, chip, monogram)
  is the only way to see a grouping, aim for 3:1.
- **APCA** ([APCA in a Nutshell](https://git.apcacontrast.com/documentation/APCA_in_a_Nutshell.html)): Lc 15 is the "absolute minimum for
  non-semantic non-text … Designers should treat anything below this level as invisible". Lc 30 suits "large/solid semantic & understandable non-text
  elements". Lc 45 suits pictograms with fine details. Lc 60/75/90 are for text.
- *(computed)* WCAG contrast of OKLCH marks with C ≤ 0.10, across all hues:

| mark L | vs #1f1f1f | vs #f4f4f4 |
|---|---|---|
| 0.45 | 2.1–2.3 | **6.4–7.2** |
| 0.50 | 2.6–2.9 | 5.2–5.8 |
| 0.55 | 3.2–3.6 | 4.2–4.7 |
| 0.65 | **4.8–5.4** | 2.8–3.1 |
| 0.72 | 6.3–7.0 | 2.2–2.4 |

Only a narrow band (L ≈ 0.55–0.60) passes 3:1 on both default themes. A per-theme L rule passes on each theme with room
to spare, for example L_mark = 0.50 on light and 0.70 on dark, or more generally L_mark = L_bg ∓ 0.4 in a theme-relative form. In OKLCH the
spread across hues at fixed L is about ±0.3 in contrast ratio, so one L per theme works for every hue.

### 5.4 Arbitrary themes

Wheelhouse themes include saturated backgrounds (Far Manager #000080) and user overrides. A fixed light/dark switch is not
enough. The robust form is relative to the background: tint = OKLab(bg) + (ΔL, C_t·cos h, C_t·sin h). The mark's L is set by a contrast target against `background`, not by a light/dark flag.
Build these in OKLab and convert to linear for the renderer. Do not `mix_4f32` in linear light: §1.2 shows how unevenly that behaves between light and dark.
Primer's dark-mode labels use alpha over the background, which is the same idea in CSS.

---

## 6. Prior art

| Product | Set size and source | Assignment | Theme handling | Override |
|---|---|---|---|---|
| **Chrome tab groups** | 9: grey, blue, red, yellow, green, pink, purple, cyan, orange ([tab_group_color.h](https://chromium.googlesource.com/chromium/src/+/main/components/tab_groups/tab_group_color.h)) | Least-used, first in a fixed order (§4.3) | Separate dark/light (and chip/text) values per slot (§5.1) | User picks one of the 9 **[memory: picker UI]** |
| **Firefox containers** | 9 up to Firefox 152 (blue, turquoise, green, yellow, orange, red, pink, purple, toolbar). Firefox 153 renames turquoise→cyan and toolbar→gray and adds violet, giving 10 ([MDN getSupportedColors](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/contextualIdentities/getSupportedColors)) | User picks at creation **[memory]** | Named slots (`colorCode` hex per name) | Colour plus icon (second channel) |
| **GitHub labels** | Arbitrary hex | User or random on creation **[memory]** | Primer derives text/bg/border per mode from the one hex (§5.1) | Edit hex |
| **Linear** | Project `color: String!` ("Used in the UI to visually identify the project"), `icon` (emoji or decorative), Team `color` ([schema](https://github.com/linear/linear/blob/master/packages/sdk/src/schema.graphql)) | Preset picker plus custom hex **[memory]** | **[memory]** stored hex used as-is | Yes |
| **Slack** | User object `color`: "Used in some clients to display a special username color" ([docs](https://docs.slack.dev/reference/objects/user-object)) | Server-assigned hex **[memory]** | **[memory]** client adjusts | Not user-facing today **[memory]** |
| **VS Code / Peacock** | Any colour, a favourites list, or "surprise me" (`tinycolor.random()`) ([color-library.ts](https://github.com/johnpapa/vscode-peacock/blob/main/src/color-library.ts)) | Manual | Writes `workbench.colorCustomizations` (`titleBar.activeBackground`, etc.; [theme colour reference](https://code.visualstudio.com/api/references/theme-color)) into workspace settings. Foreground is chosen by `tinycolor.isLight()`; hover and inactive by lighten/darken | Yes, persisted per workspace |
| **Google Calendar** | Global palettes of `calendar` and `event` colours, each a `background`+`foreground` pair ([Colors](https://developers.google.com/workspace/calendar/api/v3/reference/colors)); **[memory]** 24 calendar and 11 event colours | Default from palette **[memory]** | Pairs carry their own foreground | `colorId`, or custom hex via `backgroundColor`/`foregroundColor` with `colorRgbFormat=true` ([calendarList](https://developers.google.com/workspace/calendar/api/v3/reference/calendarList)) |
| **macOS Calendar** | Presets plus Custom Color ([Apple Support](https://support.apple.com/guide/calendar/icl1030/mac)); Birthdays and Siri Suggestions calendars can't be recoloured | Default per account **[memory]** | **[memory]** system adapts | Yes |
| **Tableau 10/20, ColorBrewer, Glasbey** | 10/20; ≤12 qualitative; generated up to 256 | Order of appearance | None (data-vis) | Per-chart |
| **Material You / HCT** | Source colour → tonal palettes | Derived | Tones per mode (§5.1), harmonized custom colours | Source colour |
| **Radix** | 25 hue scales × 12 steps × light/dark | Designer picks a scale | Per-step roles (§5.1) | Custom palette generator **[memory]** |
| **Apple system colours** | 12 + grey | Designer picks | 4 variants (light, dark, increased contrast × 2) | — |
| **GitHub identicons** | Continuous hue | Hash of the user ID with "hash-determined color values" ([GitHub blog](https://github.blog/news-insights/company-news/identicons/)). A port by GitHub engineer David Graham takes a 12-bit hue from the MD5 hash, HSL S 45–65%, L 55–75%, on #F0F0F0 ([dgraham/identicon](https://github.com/dgraham/identicon/blob/master/src/lib.rs)) | None (fixed light bg) | Upload an avatar |
| **Gravatar** | `identicon` "geometric pattern based on an email hash"; `initials`/`color` (beta) with generated colours ([docs](https://docs.gravatar.com/sdk/images/)) | Hash; colour algorithm undocumented | — | Upload |
| **WezTerm / tmux** | `format-tab-title` handler returns Background/Foreground per tab ([docs](https://wezterm.org/config/lua/window-events/format-tab-title.html)); tmux `window-status-style` **[memory]** | No automatic per-tab identity colour | User config | Script |

Pattern: **products with growing user-made sets (Chrome, Firefox, Linear, Calendar) use a small named palette of 9–24, auto-assign by
least-used or default, persist, and let the user override.** Hash-to-hue appears only for avatars, where collisions are harmless and the shape
carries most of the identity.

---

## 7. Options for Wheelhouse

Shared building block: **OKLCH → linear sRGB in C.** `linear_from_oklab` already exists, so the remaining pieces are a polar-to-Cartesian step,
an in-gamut check and a chroma clamp. The roles derived from the theme `background` add a few more lines:

```c
// ~35 lines in total, including roles; renderer wants linear RGB.
internal Vec3F32 linear_from_oklch(F32 L, F32 C, F32 h_turns)
{ return linear_from_oklab(v3f32(L, C*cos_f32(h_turns*tau32), C*sin_f32(h_turns*tau32))); }
internal B32 linear_in_gamut(Vec3F32 c){ return c.x>=-1e-4f&&c.x<=1.0001f&&c.y>=-1e-4f&&c.y<=1.0001f&&c.z>=-1e-4f&&c.z<=1.0001f; }
internal Vec3F32 linear_from_oklch_mapped(F32 L, F32 C, F32 h) // CSS 4 style: hold L,h; bisect C
{ F32 lo=0, hi=C; Vec3F32 c=linear_from_oklch(L,C,h); if(linear_in_gamut(c)) return c;
  for(int i=0;i<16;i+=1){ F32 m=(lo+hi)*.5f; if(linear_in_gamut(linear_from_oklch(L,m,h))) lo=m; else hi=m; }
  return linear_from_oklch(L,lo,h); }
// Roles from the theme: bgL = oklab_from_linear(bg).x; dark = bgL < 0.5
//   tint: OKLab(bg) + (dark?+0.03:-0.02, Ct*cos h, Ct*sin h), Ct ~ dark?0.035:0.015
//   mark: L = dark?0.72:0.50, C = 0.11 (then check >= 3:1 vs bg, nudge L)
//   text-on-mark: theme text or bg, whichever has the higher contrast
```

Pure bisection (no ΔE_OK-tolerant clip) is fine here, because the chosen chromas are near the all-hue ceiling.

### Option A: designed fixed palette, hashed, per-theme roles (fix what exists)

- **Mechanism:** replace the 6 linear literals with K = 8 hues hand-tuned in OKLCH (Tableau/Tol-style lightness variation for CVD).
  Keep `hash % K`. Render through tint/mark/text roles derived from `background` (§5.4).
- **Tradeoffs:** no storage, no core change, deterministic across machines. Collisions per §4.1: 59% with 4 visible projects, 92% with 6.
  Changing K reshuffles every project. Override impossible without storage.
- **Cost:** about 60 lines in `uishell_sidebar.c` plus the conversion above. A diagnostics test asserting ≥ 3:1 for marks on all 11 presets.
- **Growth / themes / CVD / override:** collides past about 3 visible; themes handled; CVD helped by lightness variation; no override.

### Option B (favoured): persisted slot assignment from a designed ring, least-used, with override

- **Mechanism:** identity = a slot index or hue angle stored per entity. On first sight, pick the least-used slot among *known* entities
  of that kind, in a fixed spread-out order (Chrome's `GetNextColor`). Seed ties with the entity hash so two machines usually agree.
  Store it as a local presentation fact. Override = the user picks a slot or a free hue, and that wins. Producer suggestions sit between override and default,
  per the andamento note. Render via per-theme roles. Use 8 slots designed in OKLCH; past 8, either reuse least-used slots or fall to
  golden-angle hues at the same L/C (§4.2).
- **Tradeoffs:** no collisions until K entities exist, then evenly spread. Stable, because the assignment is stored, not recomputed. Needs a store
  and a policy for entities seen on several hosts (whose choice wins). Assignments depend on order of first sight, so two machines can differ
  unless the store syncs.
- **Cost:** the roles and conversion (~100 lines C) plus storage. The core already persists local entities with facts
  (`wheelhouse_local_set`, kinds `.section`/`.group`/`.ref` with `.target` refs). A `.presentation` local (target ref, `hue` or `slot`)
  needs a small core change: `set_local` accepts only the closed `LOCAL_KINDS` list (`crates/wheelhouse-core/src/sidebar.rs`). A colour picker UI is extra
  (the ui layer already has `ui_hue_picker`/`ui_sat_val_picker` in `ui_basic_widgets.c`; an 8-swatch menu is simpler).
- **Growth / themes / CVD / override:** graceful growth; themes handled; CVD helped by slot lightness variation and a monogram (Option E); override built in.

### Option C: continuous hue, persisted (golden angle by creation order) or hashed

- **Mechanism:** identity = OKLCH hue in [0, 360). It comes either from `hash → hue` (stateless) or from `index × 137.5°` with a persisted creation index.
  Fixed per-theme L/C roles.
- **Tradeoffs:** unlimited and smooth. Hashed hues produce near-twins almost surely (§4.2: P(all gaps ≥ 20°) = 1.6% at N = 8). Golden-angle
  hues are only as good as an 8–10 colour palette at N ≤ 10, and an equal-L ring is the worst case for CVD (§3.4: 0.003–0.004 ΔE_OK
  for deuteranopes). Hues can't be named ("the teal one"), which Tableau's design and Colorgorical's name terms value.
- **Cost:** the same conversion; persisted index as in B.
- **Growth / themes / CVD / override:** grows indefinitely but at falling contrast; themes handled; CVD poor without a second channel; override = a stored hue.

### Option D: theme-supplied identity palette

- **Mechanism:** add `theme_color` tags `identity 0…7` (each with optional tint/mark/text variants) to `RD_ThemePresetTable`, like Chrome's
  per-mode slots or Apple's dynamic colours. Assignment as in A or B. Fall back to the derived roles when a user theme lacks the tags.
- **Tradeoffs:** theme authors get exact control, including Far Manager and Solarized, which derived values may serve poorly. That means 11 presets × 8 slots × roles
  to author and keep in sync, and a user theme that changes `background` without updating them breaks contrast.
- **Cost:** mdesk table edits plus a lookup. Small code, large curation.
- **Growth / themes / CVD / override:** same as A or B. Themes are best where a preset is authored. Works well as an *override layer on top of B*, not alone.

### Option E: monogram or icon chip as the primary channel, colour secondary

- **Mechanism:** each card gets a small chip (initials or a producer-suggested icon) filled with the mark colour (OKLCH, ≥ 3:1).
  The card tint only says "belongs to this card", and it can be identical for all projects. Colour comes from B (or A).
- **Tradeoffs:** survives collisions, CVD and pale themes. Costs horizontal space in a narrow sidebar and needs a monogram rule for similar names.
  It follows Okabe–Ito ("not only different colors") and WCAG 1.4.1.
- **Cost:** chip rendering plus initials extraction, about 50–80 lines of C. The colour source stays as in A or B.
- **Growth / themes / CVD / override:** unlimited; theme-safe; CVD-safe; override of icon and colour.

### Which the evidence favours

**B with E's chip, keeping D as a later theme hook.** Reasons:

1. §3 caps reliable colour identity at about 8, so a finite, *designed* ring beats continuous hues.
2. §4.1 shows any stateless hash collides in typical sidebar counts, and every growing-set product in §6 persists its assignment.
3. §1.2 and §3.3 show a faint tint cannot carry identity, so identity needs a small saturated mark. Szafir (§3.2) says it must not be a 3 px sliver;
   a chip or dot of several px with C ≈ 0.1 works.
4. Per-theme roles in OKLab (§5) are what Chrome, Apple, Radix, Material and Primer all do, and `base_math.c` already has the conversion.

Honest alternatives: A is the right *first step* if storage is not ready, because it fixes the linear-literal and tint maths at zero
architectural cost, and B later reuses the palette and roles unchanged. C only wins if the set routinely exceeds about 12 *visible*
identities, and then colour is no longer the identifier anyway (E is). D is worth adding once real users run non-default themes.
