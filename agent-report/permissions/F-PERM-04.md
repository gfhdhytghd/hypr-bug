# F-PERM-04 Permission cache does not follow process lifetime

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Plugin permission cache uses requester PID, path and type without process-start identity or death cleanup; keyboard calls use a sentinel, not process PID.

**Claim.** String-keyed permission decisions can outlive a requester and be inherited after PID reuse. **Verdict: confirmed for plugin requests; the keyboard example is incorrect.**

**Evidence — source-confirmed.** `src/managers/permissions/DynamicPermissionManager.cpp:161-173` obtains the current executable path but then looks up the cache solely by the following fields:

```cpp
return e->m_keyString == str && pid && pid == e->m_pid && e->m_type == permission;
```

The executable lookup is not compared with the cached requester's identity. Matching entries return allow, deny, or pending at `DynamicPermissionManager.cpp:211-219`. `askForPermission(nullptr, ...)` creates a runtime rule, stores the key, and stores the requester PID (`232-238`). The rule constructor installs a destroy listener only if its `wl_client*` is non-null (`28-34`). `removeRulesForClient` only erases matching client pointers (`364-365`); config cleanup at `71-73` removes config-source rules, not these runtime string rules. No process death observer, process start time, or TTL is stored (`src/managers/permissions/DynamicPermissionManager.hpp:71-84`).

**Full chain and preconditions — source-confirmed.** A process able to reach the compositor's command socket supplies a plugin path; on Linux `src/ipc/s1/Unix.cpp:54-63,283-300` obtains the PID using `SO_PEERCRED`. `Unix.cpp:323-324` forwards that peer identity, and `src/ipc/s1/Commands.cpp:1770-1784` passes it to `CPluginSystem::loadPlugin`. `src/plugins/PluginSystem.cpp:16-25` calls `clientPermissionModeWithString(requesterPid, path, PLUGIN)`. With enforcement enabled and an ask decision, the rule is kept independently of the command-socket peer. After that requester exits, a later requester with a recycled positive PID and the same path/type matches the old rule without a new prompt. A denied decision can similarly be inherited. An allow that loads code still encounters the normal “already loaded” check (`PluginSystem.cpp:67-70`); a repeated actual load would require the plugin to have been unloaded or the earlier load not to remain present. No PID churn or plugin was executed.

**Severity and corrections.** Low as in the dossier: requires prior cached state, PID reuse, the same exact plugin path and permission type, and command-socket access. PID is kernel-derived, not an arbitrary field supplied in the command. Keyboard checks instead pass **-1** at `src/managers/input/InputManager.cpp:1226,1233`, and default to allow absent an ask/deny configuration rule (`DynamicPermissionManager.cpp:222-224`). Their names are not normal-process PID cache keys. This record does not demonstrate a Wayland client's ability to choose a positive PID or bypass a lifetime-tracked `wl_client*` permission cache.

**Minimal fix direction.** Bind positive-PID rules to process-start identity or requester connection lifetime and remove/revalidate on death. Treat special negative identities separately; a TTL alone only bounds the stale-decision window.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
