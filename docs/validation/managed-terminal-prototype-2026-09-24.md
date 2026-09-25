> Superseded by [managed primary content](../design/managed-primary-content.md).
> The scratch code was absorbed into `--managed_content_diagnostics`; the notes
> below record the earlier standalone experiment.

# Managed terminal replacement prototype

Question: can Wheelhouse replace the backing terminal of a managed slot without
replacing its workspace, undoing a panel move, or removing a user-added view?

The native scratch diagnostic passed on macOS on 24 September 2026. This is
throwaway code on `prototype/managed-terminal-replacement`, not a production
reconciliation implementation. The daily driver has not been restarted.

Run with isolated settings:

```sh
bash build.sh wheelhouse
mkdir -p /tmp/wheelhouse-managed-prototype
./build/wheelhouse --user:/tmp/wheelhouse-managed-prototype/user --project:/tmp/wheelhouse-managed-prototype/project --managed_content_diagnostics
```

## Observed

The diagnostic uses the production terminal view renderer and Cleat in-process
provider. The original shell prints A and waits for input. The production split
command moves its view to a right panel, leaving a user-added fixture view behind.
A candidate shell prints B while the old attachment still reports A. The native
commit tears down the old attachment and caches, updates the saved command,
removes a seeded obsolete session ID, installs the prepared candidate, and builds
the terminal view again. All nine checks passed:

- Original terminal emits A.
- Split command moves the managed view beside the user view.
- Candidate emits B before commit.
- Original attachment survives preparation.
- The same view receives B after commit.
- Workspace ID, view ID and moved panel are preserved.
- User-added view remains in its panel.
- Saved command is updated and obsolete session ID is absent.
- Managed resource_id remains primary.

This checks terminal snapshots and native state, not screenshot appearance.

## Lifecycle findings

Terminal runtime resources currently have no unified teardown hook. The
prototype explicitly unregisters its owned provider callback, destroys the
session, closes its owned provider, releases cached image textures and cache
arenas, and zeros terminal state before installing the candidate. That cleanup
must become a normal terminal lifecycle operation before production replacement.

Do not apply owned-provider cleanup to daemon providers: those are shared. The
prototype rejects that case. It does not verify daemon detach semantics, image
traffic during replacement, asynchronous GPU draw lifetime, persistence across
an actual restart, or cross-host Flotilla attach.

Existing session IDs are resolution-specific. Updating only the command leaves
restoration able to attach to the old session. A production resolved-content
update must explicitly replace or clear session binding and related parameters,
including cwd/backend, according to the new descriptor.

## Combined conclusion with Andamento probe

The Andamento worktree `~/dev/andamento-worktrees/governor-intent-prototype`
contains the real-core identity probe and a simulated reconciliation reducer.
It keeps the project-role bound to one workspace, presents it inline, rejects
stale candidates before host mutation, and avoids restarts on unchanged targets.
This native probe establishes that the in-process terminal attachment can change
without reconstructing the layout.

The two probes are not connected. The next production slice is a shared
Andamento desired/applied managed-content model and typed host update effects,
followed by a Wheelhouse adapter using an explicit terminal replacement lifecycle.
Keep intent identity, stable slot identity and resolved target identity separate.
Compare resolved descriptors rather than the global sidebar revision. Validate
workspace binding and request revision immediately before host commit. Report
workspace presence separately from current/held/unavailable backing content.

Flotilla #1908 supplies authoritative role resolution after that consumer
contract is proven. Wheelhouse #48 consumes the result on project rows. Input
policy during a held interval, historical attachments, protected core content,
and workspace-relative new-terminal/file actions remain later decisions.

Remove or absorb both prototype shells when implementing that slice. Neither is
part of CI or a new public interface.
