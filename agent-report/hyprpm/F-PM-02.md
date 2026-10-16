# F-PM-02 Repository names are not confined to the install directory

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Repository name escapes the cache during privileged directory creation and installation; filenames and serialized state constrain writes. Root authorization remains required.

**Claim.** An unchecked manifest repository name can direct privileged cache installation outside the user's cache tree.

**Evidence — source-confirmed.** `hyprpm/src/core/Manifest.cpp:64` assigns `m_repository.name` directly, whereas plugin keys pass `validManifestName` at `91–94`. `hyprpm/src/core/PluginManager.cpp:370,378` retains the supplied repository name and sends the repository to DataState. `hyprpm/src/core/DataState.cpp:85–90,103–107,122` uses:

```cpp
const auto PATH = getDataStatePath() / repo.name;
// ...
const auto filename = std::format("{}.so", p.name);
// privileged install to PATH / filename, then writeState to PATH / "state.toml"
```

There is no component/containment check. Absolute right-hand paths replace the base under `std::filesystem::path` composition; relative parent components are also accepted. `hyprpm/src/helpers/Sys.cpp:87–89,124–133,154–163` rejects certain punctuation but not path separators or parent components. Directory creation and copies use argument vectors, including `install -o 0 -g 0`; they do not neutralize path traversal.

**Call chain and preconditions — source-confirmed.** User authorizes add and privileged operations (`main.cpp:111–125`; `PluginManager.cpp:178–189`) → repository clone and good manifest (`217–258`) → manifest repository name (`Manifest.cpp:64`) → header validation (`PluginManager.cpp:304–310`) → plugin build/output bookkeeping (`317–376`) → `DataState::addNewPluginRepo` (`378`) → privileged directory creation, plugin copies, and state-file copy (`DataState.cpp:85–122`). A same-machine user without the necessary privilege authorization cannot use these helpers to bypass authentication. The vulnerability is the privileged destination being delegated to repository data after authorization for plugin management.

**Impact limits and corrections.** High: an authorized install can create directories and place/replace root-owned files outside its intended cache, exposing a real privilege boundary. The dossier's claim of fully arbitrary file writing is too broad: ordinary destinations are `<validated-plugin-name>.so` and `state.toml`; state content is TOML serialization (`DataState.cpp:93–122`), not arbitrary bytes. Manifest author text is not the persisted repository author: `PluginManager.cpp:371` derives that field from the input URL. TOML serialization also does not imply textual configuration injection. A cross-user state-corruption scenario additionally requires a victim-discovered state location and a later update, its header checks, dependency availability, and privileged authorization (`DataState.cpp:48–61,217–245`; `main.cpp:136–158`). Unconditional root execution or a concrete system-configuration overwrite is not established here.

**Minimal fix.** Require a nonempty repository name containing only approved single-component characters, validate URL-derived fallback names too, and enforce destination containment in the privileged implementation using trusted directory descriptors and symlink-safe operations. Lexical string-prefix checks alone are insufficient.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
