# F-IPC-05 IPC system information exposes configuration contents

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Lua configuration dumps return a fixed unsupported message; rolling logs remain accessible to socket-authorized peers, without demonstrated secret exposure.

**Claim.** The `c/systeminfo` request returns all Lua configuration text, including credentials; a separate follow request continuously exposes logs and window titles.

**Verdict: rejected** for the tracker entry's configuration-disclosure defect. Log access exists, but does not restore the claimed configuration dump or prove disclosure of a secret.

**Evidence / full chain — source-confirmed.** `src/ipc/s1/S1.cpp:67-80` recognizes the `c` flag and sets `includeConfig`; the exact `systeminfo` handler is registered in `src/ipc/s1/Commands.cpp:1982`. `Commands.cpp:1185-1198` appends config markers around `Config::mgr()->getConfigString()`. However, the only selected provider is Lua (`src/config/ConfigManager.cpp:29-35`) and its implementation is:

```cpp
// src/config/lua/ConfigManager.cpp:1083-1085
std::string CConfigManager::getConfigString() {
    return "Not supported under lua";
}
```

Thus an authorized socket request reaches a fixed placeholder, not source files, imported Lua modules, or embedded tokens. The original finding stops before following this virtual method to its implementation.

**Separate log behavior — source-confirmed.** `Commands.cpp:1012-1022` returns the rolling log; `1984-1987` selects follow mode when requested. `src/ipc/s1/Unix.cpp:364-388` starts follow delivery and polls at 100 ms; `src/debug/log/RollingLogFollow.hpp:23-29,44-48` queues generated log records. This is an explicit diagnostic feature (`hyprctl/src/Strings.hpp:47-48`). Sensitive strings could appear when generated: for example, `src/config/supplementary/executor/Executor.cpp:174` logs command text. Logging defaults to disabled (`src/config/values/ConfigValues.cpp:726`; `src/debug/log/Logger.hpp:43-49` and `Logger.cpp:51-60` gate later logging), so continuously collecting new private titles is not an unconditional default behavior. Startup records or previously generated records may still be present; none were inspected.

**Reachability / severity / corrections.** Both paths require access through the 0700 instance directory described in F-IPC-04. No different-UID exposure is established. Configuration disclosure is absent upstream; owner-accessible log diagnostics are informational and should not be represented as confirmed credential leakage. No actual user's config, log, or credential was read for this verification.

**Minimal fix direction.** No configuration-dump fix is required at this baseline. Keep future config export explicitly opt-in, omit secrets from diagnostic logs, and document logs as sensitive when users share them. Any stricter per-application log policy would require an IPC authorization model.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
