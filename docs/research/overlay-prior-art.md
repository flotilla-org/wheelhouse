# Prior art: baseline plus user overlay

Research for #293, which is part of map #290. Researched 2026-10-10 using primary sources: official docs, specs, and in one case source code. Each claim links to its source. Where a source says nothing on a question, that is stated rather than guessed.

Vocabulary is from `CONTEXT.md`. A **Suggested Layout** is the provider's baseline. A **Workspace Overlay** is the user's saved changes relative to it. **Overlay Sync** is the later sharing protocol, out of scope here.

Each system is checked against the four questions in the ticket:

1. **Edit.** What counts as an edit?
2. **Baseline recorded.** Are edits recorded against a baseline version?
3. **Conflicts on baseline change.** How do conflicts surface when the baseline changes?
4. **Upstream path.** Can edits be pushed back upstream?

---

## tmuxinator and tmuxp

- **Edit.** There are no overlays. A project is one whole YAML file. tmuxinator opens it in `$EDITOR` (`new`, `open`, `edit`) or duplicates it with `tmuxinator copy [existing] [new]` ([tmuxinator README][tmuxinator]). The only parameterisation is ERB: `@args` and `key=value` `@settings` passed at start, e.g. `root: ~/<%= @settings["workspace"] %>` ([README, "ERB"][tmuxinator]). tmuxp also loads whole files, from `$XDG_CONFIG_HOME/tmuxp/` or a project-root `.tmuxp.yaml` ([tmuxp configuration][tmuxp-config]). `tmuxp freeze` exports a running session to a new workspace file ([tmuxp freeze][tmuxp-freeze]). That is a snapshot, not a diff.
- **Baseline recorded.** No.
- **Conflicts on baseline change.** None. Files are chosen, never merged. tmuxinator uses a repo-local `./.tmuxinator.yml` only when no named project of that name exists in the user directory, and `--project-config` overrides both ([README][tmuxinator]). The tmuxp docs describe no inheritance or merging ([tmuxp configuration][tmuxp-config]).
- **Upstream path.** Manual: commit the shared `.tmuxinator.yml`, which the README says is "primarily intended to be used for sharing tmux configurations in complex development environments" ([README][tmuxinator]).

**Model:** whole-file selection with first-match precedence (fork-and-own).

## zellij layouts

- **Edit.** A layout is a KDL file. Reuse happens inside one file, through `pane_template` and `tab_template` with a `children` placeholder, `default_tab_template`, and `new_tab_template`. A relative `cwd` composes pane → tab → global → launch directory, and an absolute path overrides ([Creating a layout][zellij-layout]). A layout can also carry config, which "take[s] precedence over items in the loaded Zellij configuration" ([Layouts with config][zellij-layout-config]). The docs describe no way to import one layout file into another. Session resurrection serialises the live session into a layout every second and keeps it in the cache folder. These saved layouts are human-readable and can be edited, shared, or loaded with `--layout` ([Session resurrection][zellij-resurrect]).
- **Baseline recorded.** No. The docs don't link a resurrected layout back to the layout the session started from ([Session resurrection][zellij-resurrect]).
- **Conflicts on baseline change.** None. A resurrected session is an independent snapshot. `zellij setup --dump-layout default` starts a user layout as a copy ([Creating a layout][zellij-layout]).
- **Upstream path.** Manual file sharing only.

**Model:** templates inside a file, plus a snapshot of the live state (fork-and-own, with automatic capture).

## Dev Containers (devcontainer.json, Features, Templates)

Dev Containers has three layering mechanisms, each with different semantics.

1. **Image metadata plus `devcontainer.json`: a declarative per-property merge.** Each Feature and base image stores a `devcontainer.metadata` label, and those snippets "should then be merged with any local devcontainer.json file contents." Every property has a fixed merge rule ([spec, "Merge logic"][dc-spec]):
   - `init` and `privileged`: true if any source is true.
   - `capAdd`: union without duplicates.
   - `mounts`: collected list; on conflict "last source wins".
   - `containerEnv`: "per variable, last value wins".
   - `forwardPorts`: union.
   - `hostRequirements`: max wins.
   - `customizations`: "merging is left to the tools".
   - `id`: not merged.

   Where order matters, "the `devcontainer.json` is considered last", so user config wins.
   - **Edit.** Any property in the user's `devcontainer.json`.
   - **Baseline recorded.** No version is recorded for the image-metadata baseline.
   - **Conflicts.** None surface. The fixed rules resolve everything silently.
   - **Upstream path.** None.
2. **Features: version-pinned baselines with user options.** The user references a Feature by semver tag, e.g. `go:1`, and supplies an options object. Omitted options fall back to the Feature's defaults ([Features spec][dc-features]). The lockfile `devcontainer-lock.json` records each Feature's exact `version`, its `resolved` digest, and its `integrity` sha256 ([lockfile spec][dc-lock]).
   - **Edit.** The option values the user supplies.
   - **Baseline recorded.** Yes. The lockfile pins the exact version.
   - **Conflicts.** Not handled by a merge. Option names are the contract between the Feature and the user.
   - **Upstream path.** None.
3. **Templates: one-shot scaffolding.** Tools substitute options and drop the files into the project "as a starting point" ([Templates spec][dc-templates]).
   - **Edit.** Anything the user changes in the copied files.
   - **Baseline recorded.** No. The spec doesn't record the applied Template version ([Templates spec][dc-templates]).
   - **Conflicts.** None. The spec defines no update path, so later Template releases never reach the project.
   - **Upstream path.** None.

**Model:** fixed per-property merge rules (1); pinned baseline plus a parameter overlay (2); copy once and own (3).

## VS Code: layered settings and `.code-workspace` files

- **Edit.** A user edit writes a setting ID and value into the JSON file for the chosen scope ([Settings][vsc-settings]). So each scope file is a sparse set of overridden keys, not a full copy. The scope order is default → user → remote → workspace → workspace folder, then language-specific overrides, with policy last. "Later scopes override earlier scopes." Primitive and array values are replaced. Object values are merged key by key, and the higher scope wins on each key ([Settings][vsc-settings]). A `.code-workspace` file holds `folders`, `settings`, `extensions` recommendations, `launch`, and `tasks`. Folder settings override workspace settings, but in a multi-root workspace only resource-scoped settings apply per folder. Unsupported folder settings are shown greyed out ([Multi-root workspaces][vsc-multiroot]).
- **Baseline recorded.** No. A user override is keyed by setting ID, not by the version of the default it overrides. When an extension changes a default, every key the user did not override follows the new default, and every key they did override keeps the user's value ([Settings][vsc-settings]).
- **Conflicts on baseline change.** Baseline (default) changes never conflict, because overrides are keyed. The only conflicts are between devices, in Settings Sync. It "automatically merges your local and cloud data" and prompts only when it can't. The prompt offers Accept Local, Accept Remote, or Show Conflicts, which opens a diff editor ending in "Complete Merge". Settings with `machine` or `machine-overridable` scope, and those listed in `settingsSync.ignoredSettings`, never sync ([Settings Sync][vsc-sync]).
- **Upstream path.** Indirect. A team shares a baseline by committing workspace settings or a `.code-workspace` file to the repo. Per-key scope restrictions (resource vs window, machine) decide which keys can live at which layer.

**Model:** a layered key-value override with a scope order. Objects merge by key, and anything else is replaced whole. Settings carry scope metadata that decides where they may be overridden and whether they sync.

## Nix overlays

- **Edit.** An overlay is a function `final: prev: { … }` that returns attributes to add or replace in the package set. `prev` is the set before this overlay, used to reach what is being overridden. `final` is the fixed point, used for dependencies. Overlays apply in list order, and "the order of the overlays can be significant if multiple layers override the same package." User overlays load from `~/.config/nixpkgs/overlays.nix` or the `overlays/` directory, in lexicographic order. `overrideAttrs` rewrites a derivation's attributes as a function of `previousAttrs` ([Nixpkgs manual, Overlays][nix-overlays]).
- **Baseline recorded.** Not by the overlay. Pinning happens outside it: a flake's `flake.lock` records each input's exact `rev` and `narHash` ([nix flake][nix-flake]).
- **Conflicts on baseline change.** None are detected. An overlay is code that runs against whatever `prev` it receives. If the baseline renames or removes an attribute, evaluation fails or silently stops matching. The manual says nothing about upstream drift ([Nixpkgs manual][nix-overlays]).
- **Upstream path.** None in the mechanism. The overlay is a separate artifact, and the user upstreams it by hand as a nixpkgs change.

**Model:** the edit is a function of the baseline rather than a diff. It is powerful and composable, but it needs a stable baseline API, and nothing detects drift.

## Kubernetes kustomize

- **Edit.** An overlay is a directory whose `kustomization.yaml` references one or more bases and applies customisations ([Kubernetes docs, "Bases and Overlays"][k8s-kustomize]). A patch is either a strategic merge patch (a partial object, matched by kind and name) or an RFC 6902 JSON patch, which always needs a `target`. Targets can select by group, version, kind, name, namespace, label selector, or annotation selector. Patches apply in list order ([kustomize patches][kust-patches]).
- **Baseline recorded.** Optional. A remote base is a git URL that can carry `ref=v3.3.1` ([kustomize resource][kust-resource]). A local base is not versioned.
- **Conflicts on baseline change.** They surface as build errors, not merge conflicts. A name-matched strategic merge patch that doesn't find exactly one target fails with `failed to find unique target for patch` ([source: `api/resmap/reswrangler.go`][kust-src]). "A patch can refer to a resource by any of its previous names or kinds," which tolerates prefix and suffix transforms ([patches][kust-patches]). Changes to fields the patch doesn't touch flow through.
- **Upstream path.** None. "A **base** has no knowledge of an overlay" ([Kubernetes docs][k8s-kustomize]).

**Model:** addressed patches. Each edit names its target by stable identity (kind and name) or by selector. Edits to fields the baseline doesn't change flow through. A target that disappears is a hard error.

## The git rebase model (and the tooling around it)

- **Edit.** A commit: a diff with metadata.
- **Baseline recorded.** Yes, implicitly. The commit's parent is the baseline. `git format-patch --base` writes `base-commit:` and `prerequisite-patch-id:` lines so that receivers know "the exact state the patch series applies to" ([git-format-patch][git-fp]).
- **Conflicts on baseline change.** `git rebase <upstream>` lists the branch's commits that "do not have an equivalent commit in <upstream>" and replays each onto the new upstream like a cherry-pick. Commits already upstream, identified by patch-id, are dropped ([git-rebase][git-rebase]). On a conflict, rebase stops at the first failing commit with conflict markers. The user then resolves it, or runs `--skip` or `--abort` ([git-rebase][git-rebase]). `git rerere` records conflict resolutions and replays them on the same conflict later, which is aimed at "relatively long lived topic branches" ([git-rerere][git-rerere]). That matches a long-lived overlay rebased onto every new baseline.
- **Upstream path.** First-class. The overlay's commits are the contribution, via `format-patch` or a pull request. Once upstream accepts them, the next rebase drops them because their patch-ids already appear upstream ([git-rebase][git-rebase]). The overlay shrinks by itself as edits are upstreamed.

**Model:** an ordered edit log anchored to a baseline version, replayed on each baseline change. Conflicts stop the replay for a human. Resolutions can be memoised. Upstreamed edits are absorbed automatically.

## Copier (template update with a 3-way diff)

Copier wasn't in the ticket's list. It is the closest prior art for "provider baseline plus user edits, both evolving".

- **Edit.** Anything in the generated project.
- **Baseline recorded.** Yes. The answers file records the template version, `_commit: v1.0.0`, plus the user's answers ([Copier configuring][copier-config]). The docs say never to edit it by hand, because that "will trick Copier" ([Copier updating][copier-update]).
- **Conflicts on baseline change.** `copier update` works in four steps ([Copier updating, "How the update works"][copier-update]):
  1. Regenerate a fresh project from the *recorded* template version.
  2. Diff that fresh project against the current project. The diff is the user's overlay.
  3. Generate from the new version.
  4. Re-apply the diff.

  Hunks that don't apply become inline conflict markers (`--conflict inline`, the default) or `.rej` files. A path the user deleted is excluded from future updates. `copier recopy` is the escape hatch: it discards the user's diff but keeps their answers.
- **Upstream path.** None.

**Model:** the baseline is stored by reference (template plus version plus parameters). The overlay is computed rather than stored, as the diff between regenerated baseline and current state. Updating is a 3-way merge.

## Grafana provisioned dashboards (baseline file plus UI edits)

This is the closest dashboard-specific prior art.

- **Edit.** Saving in the UI. With `allowUiUpdates: true`, the change goes to Grafana's database, not the provisioning file. With `false`, saving is refused ([Grafana provisioning][grafana]).
- **Baseline recorded.** No. Grafana "ignores the `version` property" when the file changes ([Grafana provisioning][grafana]).
- **Conflicts on baseline change.** None surface. The file version overwrites the database version, and UI edits are lost unless exported first. With `disableDeletion: false`, removing the source can also delete a dashboard the user saved ([Grafana provisioning][grafana]).
- **Upstream path.** Manual export. "Save JSON to file" or "Copy JSON to Clipboard", then paste into the provisioning source ([Grafana provisioning][grafana]).

**Model:** the baseline wins and user edits are lost. This is a known failure mode to design against.

## Home Assistant auto-generated dashboards ("Take control")

- **Edit.** Choosing "Take control" turns the auto-generated dashboard into a user-owned one ([HA dashboards][ha]).
- **Baseline recorded.** No.
- **Conflicts on baseline change.** None, because the baseline stops arriving. The dashboard "is no longer automatically updated when new dashboard elements become available", and "you can't get this specific dashboard back to update automatically" ([HA dashboards][ha]).
- **Upstream path.** None.

**Model:** a one-way fork. The user can live on the baseline or own a copy, never both. This is exactly what a Workspace Overlay is meant to avoid. New provider items (new panes or targets) stop appearing once the user customises.

## CRDT-based tools (Automerge)

No CRDT-based dashboard product was found with documented provider-baseline semantics. The relevant prior art is the Automerge primitives a dashboard would be built on.

- **Edit.** An operation on a map, list, or text object.
- **Baseline recorded.** Yes. A document version is a set of *heads*, from `getHeads`. A user can `clone` (fork) a document, edit the fork, and `merge` it back. Afterwards the heads include both lines of history ([Automerge `changeAt`][am-changeat]). `changeAt` makes "a change to the document as it was at a particular point in history". That amounts to recording an edit against a specific baseline version without forking ([Automerge `changeAt`][am-changeat]).
- **Conflicts on baseline change.** Merges never fail. Concurrent writes to the same property pick a winner that is "arbitrary, but deterministic", and "the other values are not lost": `getConflicts` exposes them. Concurrent list and text inserts and deletes are all preserved, in a consistent order ([Automerge conflicts][am-conflicts]).
- **Upstream path.** Symmetric. Merging a user fork into the provider's document is the same operation as merging the other way, so upstreaming is a policy decision, not a mechanism.

**Model:** a shared operation history in which the baseline is just another replica. Conflicts never block. They are visible after the fact as multi-value registers. This fits Overlay Sync (later) better than the local overlay model, but it does show that heads give a cheap "edits since baseline version X".

---

## Menu of models for Workspace Overlay

Each model gives the answers it implies for the four questions.

**A. Fork and own** (tmuxinator, tmuxp, zellij, Dev Container Templates, Home Assistant "Take control" [ha])

- **Edit.** Anything, because the overlay is a full copy.
- **Baseline recorded.** No.
- **On baseline change.** Nothing arrives.
- **Upstream path.** Manual.

Simplest. It fails the CONTEXT.md requirement that an overlay modify "without replacing the provider's baseline".

**B. Baseline wins** (Grafana provisioning [grafana])

- **Edit.** UI saves.
- **Baseline recorded.** No.
- **On baseline change.** The baseline silently overwrites the user's edits.
- **Upstream path.** Manual export.

An anti-pattern to rule out explicitly.

**C. Keyed layered override** (VS Code settings [vsc-settings], Dev Container metadata merge [dc-spec], zellij layout-over-config [zellij-layout-config])

- **Edit.** Setting a value at a stable key path. Objects merge by key, and anything else is replaced.
- **Baseline recorded.** No. None is needed while keys are stable.
- **On baseline change.** Untouched keys follow the baseline. Removed keys become orphaned overrides with no error.
- **Upstream path.** Promote a key to a shared layer.

Cheap and robust for properties such as a pane's command, title, or size. It is weak for structure: inserting, removing, or reordering panes and splits.

**D. Addressed patches** (kustomize [kust-patches], Nix overlays [nix-overlays])

- **Edit.** A patch addressed to a stable identity or selector, applied in order.
- **Baseline recorded.** Optional, as a pinned ref [kust-resource] or lockfile [nix-flake].
- **On baseline change.** A target that vanished is a hard error, or silently matches nothing [kust-src].
- **Upstream path.** None.

Fits Wheelhouse if Suggested Layout nodes have stable IDs, which is the identity question in #290. Errors surface at "build" (materialise) time.

**E. Edit log anchored to a baseline version, replayed on change** (git rebase [git-rebase], rerere [git-rerere], format-patch `--base` [git-fp])

- **Edit.** An ordered list of operations, recorded together with the Suggested Layout version they were made against.
- **Baseline recorded.** Yes.
- **On baseline change.** Replay the operations. A failing operation stops the replay with a visible conflict. Resolutions can be memoised.
- **Upstream path.** First-class: the operations are the contribution, and upstreamed operations drop out by equivalence.

Strongest story for upstream contribution. Costs: storing operations, defining operation equivalence, and conflict UI.

**F. Recorded baseline plus computed 3-way diff** (Copier [copier-update], [copier-config]; the Dev Container lockfile [dc-lock] is the pinning half)

- **Edit.** Whatever differs between the regenerated baseline and the current state. Only the baseline reference (provider, version, parameters) and the current state are stored.
- **Baseline recorded.** Yes.
- **On baseline change.** A 3-way merge of the old baseline, the new baseline, and current state. Conflicts are marked inline. User deletions stick.
- **Upstream path.** The computed diff is the proposal.

Needs the provider to be able to reproduce the old Suggested Layout, or Wheelhouse to cache it. Note Copier's own escape hatch, `recopy`, for when it can't.

**G. CRDT shared history** (Automerge [am-conflicts], [am-changeat])

- **Edit.** CRDT operations.
- **Baseline recorded.** Yes, as heads.
- **On baseline change.** Merge never fails. Concurrent same-key writes resolve deterministically, with losers kept for inspection.
- **Upstream path.** Symmetric merge.

Best for Overlay Sync across devices later. Applied to the local overlay alone, it hides baseline conflicts instead of surfacing them.

### Observations for #298

- These models compose. A common pattern is to address structural edits by ID (D), override properties per key (C), and record the Suggested Layout version (as E and F do), so conflicts can be explained rather than silently orphaned. A CRDT (G) can sit underneath later for Overlay Sync without changing the overlay's meaning.
- Every model that handles baseline change gracefully needs a **stable identity** for baseline elements: keys in C, kind and name in D, line context in E and F. This ties #293 directly to the identity scheme in #290.
- Only E, and to a degree F, give a real "push edits upstream" path. If contributing back to a provider's Suggested Layout is a goal, store overlay edits as discrete operations rather than as a final-state snapshot.

[tmuxinator]: https://github.com/tmuxinator/tmuxinator/blob/master/README.md
[tmuxp-config]: https://tmuxp.git-pull.com/configuration/index.html
[tmuxp-freeze]: https://tmuxp.git-pull.com/cli/freeze.html
[zellij-layout]: https://zellij.dev/documentation/creating-a-layout.html
[zellij-layout-config]: https://zellij.dev/documentation/layouts-with-config.html
[zellij-resurrect]: https://zellij.dev/documentation/session-resurrection.html
[dc-spec]: https://containers.dev/implementors/spec/
[dc-features]: https://containers.dev/implementors/features/
[dc-templates]: https://containers.dev/implementors/templates/
[dc-lock]: https://github.com/devcontainers/spec/blob/main/docs/specs/devcontainer-lockfile.md
[vsc-settings]: https://code.visualstudio.com/docs/configure/settings
[vsc-multiroot]: https://code.visualstudio.com/docs/editing/workspaces/multi-root-workspaces
[vsc-sync]: https://code.visualstudio.com/docs/configure/settings-sync
[nix-overlays]: https://nixos.org/manual/nixpkgs/stable/#chap-overlays
[nix-flake]: https://nix.dev/manual/nix/stable/command-ref/new-cli/nix3-flake.html
[k8s-kustomize]: https://kubernetes.io/docs/tasks/manage-kubernetes-objects/kustomization/
[kust-patches]: https://kubectl.docs.kubernetes.io/references/kustomize/kustomization/patches/
[kust-resource]: https://kubectl.docs.kubernetes.io/references/kustomize/kustomization/resource/
[kust-src]: https://github.com/kubernetes-sigs/kustomize/blob/master/api/resmap/reswrangler.go
[git-rebase]: https://git-scm.com/docs/git-rebase
[git-rerere]: https://git-scm.com/docs/git-rerere
[git-fp]: https://git-scm.com/docs/git-format-patch
[copier-update]: https://copier.readthedocs.io/en/stable/updating/
[copier-config]: https://copier.readthedocs.io/en/stable/configuring/
[grafana]: https://grafana.com/docs/grafana/latest/administration/provisioning/
[ha]: https://www.home-assistant.io/dashboards/dashboards/
[am-conflicts]: https://automerge.org/docs/reference/documents/conflicts/
[am-changeat]: https://automerge.org/automerge/api-docs/js/functions/changeAt.html
