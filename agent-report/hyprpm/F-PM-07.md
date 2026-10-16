# F-PM-07 Plugin load paths trust stored state

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Stored load paths lack confinement; normal state is root-protected, permission and ABI gates apply, and compositor-supplied names reach unload only.

**Claim.** Repository-name/filename fields loaded from state can point outside the cache and ultimately reach `dlopen`; the dossier also treats names reported by the compositor as a loading input.

**Evidence — source-confirmed.** `hyprpm/src/core/DataState.cpp:217–245` reads repository names and filenames directly from parsed state. `hyprpm/src/core/PluginManager.cpp:997–1005` maps plugin names back to a stored repository name. The enabled-plugin loop at `1025–1039` forms:

```cpp
loadUnloadPlugin(HYPRPMPATH / repoForName(p.name) / p.filename, true)
```

There is no confinement check on this composed path; absolute state components can discard its base. `loadUnloadPlugin` first checks global-vs-running ABI (`1052–1059`) and then sends `/plugin load` (`1062–1063`). `src/ipc/s1/Commands.cpp:1778–1794` extracts the path and invokes `CPluginSystem::loadPlugin` with the IPC requester's PID. `src/plugins/PluginSystem.cpp:20–59` applies dynamic plugin permissions; only an allowed request reaches the internal loader and `dlopen` (`67–77`).

**Reachability and guards — source-confirmed.** A prepared state entry must be discovered by `DataState::getPluginStates` (`48–61`), be enabled and nonfailed (`PluginManager.cpp:1028–1034`), pass header validation (`953–956`), and satisfy the running ABI (`1056`). Unless forced, an already-listed plugin is skipped (`1036–1037`). Permission enforcement defaults false (`src/config/values/ConfigValues.cpp:717`); when enabled, denial or pending confirmation blocks immediate loading (`PluginSystem.cpp:21–56`; `DynamicPermissionManager.cpp:152–156,194–206,226–229`). There is no claim here of bypassing that permission gate.

The missing initial attacker capability matters. Normal cache directories are created with mode 755 (`DataState.cpp:65–78`) and state is installed root-owned mode 644 (`28–37`; `Sys.cpp:154–163`). Manifest plugin names are checked (`Manifest.cpp:28,91`), and DataState generates their stored filenames as `<name>.so` (`DataState.cpp:103,112`). A repository does not directly choose an arbitrary state filename during an ordinary valid installation. A different unprivileged UID cannot simply rewrite this state under normal ownership. Existing writable/symlinked cache trees would require separately established preconditions; no source shows header installation making cache state writable to ordinary users.

**What holds and what does not.** Unconfined stored load paths are present. A preexisting privileged-state compromise or F-PM-02 destination violation can make them relevant, but this is not a separate demonstrated Medium privilege bypass. Names from `j/plugins list` feed the **unload** loop (`PluginManager.cpp:1013–1017`), not the enabled-plugin load path. `src/ipc/s1/Commands.cpp:1795–1805` looks up an already-loaded module for unload; it does not `dlopen` that name. The dossier conflates the two operations. A usable native module and permitted load are also required; an arbitrary copied file is not automatically executable plugin code.

**Severity and fix.** Informational as a dependent sink; count the actual destination/state corruption at F-PM-02 rather than multiplying its impact. Validate stored components, ownership, and final descriptor-relative containment before load. ABI/version matching is a compatibility guard, not a content-authentication mechanism. No module was loaded or IPC sent during verification.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
