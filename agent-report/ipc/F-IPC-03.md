# F-IPC-03 IPC plugin loading accepts arbitrary paths

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Arbitrary native paths load when permission enforcement is off by default; enabled enforcement can deny or require user approval before dlopen.

**Claim.** Any IPC peer can load an arbitrary shared-library path without checks, obtaining native execution in the compositor; unload may leave dispatcher registrations behind.

**Verdict: partially.** Arbitrary readable paths are accepted in the default permission-disabled configuration. The dossier misses an explicit permission gate and overstates the unloaded-dispatcher claim.

**Evidence / complete chain — source-confirmed.** `src/ipc/s1/Commands.cpp:1770-1794` parses the operation and path and passes the real socket peer PID to:

```cpp
g_pPluginSystem->loadPlugin(PATH, SPECIAL_PID_TYPE_NONE, pid)
```

`src/plugins/PluginSystem.cpp:20-64` first calls `clientPermissionModeWithString(..., path, PERMISSION_TYPE_PLUGIN)`. A pending decision waits for a promise; errors and denial reject the request (`25-42`), and only an allow decision reaches `loadPluginInternal` (`46` or `59`). `src/helpers/defer/Promise.hpp:98-107` executes the promise body immediately and delivers already-resolved results to a later `then`; the use of a promise is not a bypass of this check.

`src/managers/permissions/DynamicPermissionManager.cpp:152-156` returns allow if `ecosystem:enforce_permissions` is false. That option defaults to false at `src/config/values/ConfigValues.cpp:717`. With enforcement enabled, configured/cached allow and deny rules are applied (`DynamicPermissionManager.cpp:172-219`); an unmatched plugin request asks and returns pending (`222-229`). Thus default socket-authorized peers can reach the loader, but a deny rule or denied prompt blocks this direct chain before native loading. This is a source statement about control flow, not validation of the permission dialog subsystem under all failure conditions. It is also only a gate on this plugin-loading route: the unrestricted evaluation interface in F-IPC-01 means enabling plugin permission checks alone is not an authorization boundary for the entire administrative socket.

`src/plugins/PluginSystem.cpp:67-104` checks duplicate paths, then calls `dlopen(path.c_str(), RTLD_LAZY)` at line 77. There is no directory allowlist or signature verification there. Export/API checks come afterward; they do not protect against library initialization that happens during `dlopen`. No library was compiled or loaded in this review.

**Reachability / severity.** The peer needs traversal of the owner-only instance directory plus a library path readable by the compositor; default permission enforcement does not add a prompt. This is an intentional same-user native-extension capability, with no demonstrated different-UID or sandbox boundary crossing. Revised severity is informational for the standalone claim. If an integration exposes this socket as a restricted or read-only API, native loading violates that integration's assumption, but that deployment is not shown here.

**Corrections — source-confirmed.** The claim that no authorization exists is false. Default-disabled authorization and absent authorization are distinct facts. ABI validation is present but follows `dlopen`. The statement that dispatcher registrations may remain after unload is unsupported for the tracked list: `PluginSystem.cpp:164-166` iterates `m_registeredDispatchers` and calls `HyprlandAPI::removeDispatcher`; `168-175` also unregisters IPC commands and invokes provider cleanup. This does not prove the safety of every conceivable plugin-owned callback, and that broader audit is outside this dossier.

**Minimal fix direction.** Document that the IPC socket can load native extensions, keep it private, and enable permission enforcement where approval is required. If stronger policy is desired, use a correctly enforced trusted-library source policy and test the deny/pending/allow paths. Disabling the entire feature or calling an arbitrary writable-directory allowlist a security boundary is not justified by this report.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
