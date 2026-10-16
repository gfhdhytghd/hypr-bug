# F-PM-09 File-operation races in plugin management

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Fixed work/state paths permit same-user concurrency races; normal private runtime directories block cross-user placement. Socket environment trust is not a race.

**Claim.** Work-directory recreation, a shared temporary state file, privileged removal, and environment-derived IPC paths collectively permit file-operation races and socket impersonation.

**Evidence — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:123–134` removes an existing path, tests existence again, then calls `mkdir(..., S_IRWXU)`. The plugin work path is predictable and shared by add/update for the same username (`208,697`); the header checkout is also predictable (`521`). There is no interprocess lock. `updatePlugins` even discards `createSafeDirectory`'s boolean result (`720`). Concurrent invocations can erase or reuse each other's trees.

`hyprpm/src/core/DataState.cpp:28–37` uses one file for all state updates:

```cpp
std::ofstream of(std::format("{}.temp-state", getTempRoot()), std::ios::trunc);
// write, close
return NSys::root::install(std::format("{}.temp-state", getTempRoot()), to, "644");
```

The pathname is reopened by the privileged copy after closing the ordinary user's stream. A source-level interleaving is sufficient to establish the correctness race: writer A closes its state; writer B truncates/replaces the same file; A's install then consumes B's bytes or partial state. These are not unique temporary files or an atomic transaction. No such race was executed against hyprpm.

**Reachability and privilege limits — source-confirmed.** Two same-user add/update/enable/disable operations using the same runtime directory can enter `writeState` (`DataState.cpp:122,195,287`); their shared staging file and work paths produce a closed concurrency trigger. The cache copy still needs the selected privilege tool to authorize it (`Sys.cpp:154–163`). New runtime subdirectories use 0700 (`DataState.cpp:21–22`; `PluginManager.cpp:198–200,133`); newly created cache directories use 755 and root-owned state uses 644 (`DataState.cpp:65–78`; `Sys.cpp:161`). With a properly private XDG runtime directory, an unrelated UID cannot place entries there. `DataState::getTempRoot` does not verify an existing directory's ownership, symlink status or mode (`12–24`); this is hardening debt, not proof that the normal runtime parent is attacker-writable. Same-UID malicious code is a materially stronger precondition than a different local user.

**Other subclaims — source-confirmed, with limits.** `hyprpm/src/helpers/Sys.cpp:136–151` canonicalizes before invoking elevated removal and checks a textual cache prefix. This is not an atomic directory-relative removal, but the dossier supplies no cross-user attacker capability to replace the normally root-owned ancestors. A general exploitable root-removal race is not confirmed. `HyprlandSocket.cpp:38–44,55–69` accepts runtime/signature environment strings and composes the socket pathname without a signature-character or peer-owner check. That is source-confirmed environment trust, not a timing race. It requires control of the caller's environment or an accessible selected socket; the default private runtime directory is again relevant. Furthermore, as F-PM-03 explains, a forged running-version reply does not supply the installed-version fields used for header checkout.

**Severity and fix.** Low for demonstrated same-user concurrency/state-corruption risk; no cross-user escalation, root deletion, or compositor compromise is demonstrated. Use a per-user operation lock, unique private workspaces, exclusive no-follow temporary files, and atomic bounded state installation. Validate existing runtime directories and selected socket identity separately from the race fixes.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
