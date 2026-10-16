# F-PM-06 Plugin output paths are not validated

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unchecked output paths reach privileged copying; existence checks do not require read permission. Accessible metadata and root authorization are necessary.

**Claim.** A manifest output may escape the checkout, after which a privileged copy publishes its contents with mode 0755.

**Evidence — source-confirmed.** Both manifest formats copy output strings without confinement (`hyprpm/src/core/Manifest.cpp:40,102`), requiring only nonempty output/build-step collections (`58–61,121–124`). Add/update check `std::filesystem::exists` on a string concatenation (`PluginManager.cpp:344,848`), then retain that same path (`376,877`). This is not `path / absolute-output`: a leading slash alone does not replace the checkout string prefix. Parent traversal and filesystem links remain unconstrained.

The final guard and privileged copy are `hyprpm/src/core/DataState.cpp:106–107`:

```cpp
if (std::filesystem::exists(p.filename)) {
    if (!NSys::root::install(p.filename, (PATH / filename).string(), "0755"))
        // fail
}
```

`hyprpm/src/helpers/Sys.cpp:154–163` passes the source to elevated `install` with owner/group 0. Its punctuation filter does not check containment, source ownership, regular-file type, or link resolution. `exists()` establishes accessible metadata; it does not establish that the invoking user can read the file's contents. A file with protected contents beneath traversable directories can therefore pass both existence checks and subsequently be read by the privileged copy.

**Call chain and preconditions — source-confirmed.** User authorizes plugin add/update and elevation (`main.cpp:120–125,136–147`) → valid fetched manifest (`PluginManager.cpp:240–258` or `768–788`) → successful header checks → nonempty build step(s) and output (`Manifest.cpp:58,121`) → unprivileged build (`PluginManager.cpp:330–338` or `834–842`) → output existence check → source path in repository state object → `DataState::addNewPluginRepo` → second existence check → elevated install → readable/executable cached copy. This occurs before plugin enable/load, so ELF validity and compositor permission checks are irrelevant to the disclosure. The build need not create the referenced existing file.

**Limits and corrections.** High: a repository can influence a root-readable copy source during an otherwise authorized install, crossing the intended read-permission boundary. This is not an unauthenticated remote read, nor does it bypass a denied elevation request. Source ancestors must be searchable by the ordinary user for `exists()` to succeed; some other users' private home directories therefore block the dossier's generalized private-key example. The target must also be copyable by `install`; a directory or an inaccessible metadata path does not automatically qualify. The raw dossier's exact traversal depth is environment-dependent. No sensitive file was read, copied, or tested in this verification.

**Minimal fix.** Resolve outputs under the checkout through symlink-safe directory-relative APIs, require regular artifacts owned/readable by the invoking user, and stage/copy unprivileged before elevating only destination installation. Enforce the source boundary at the privileged operation too, avoiding a check/use race.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
