# F-BOOT-05 Environment-toggle parsing accepts overly broad values

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Nonempty values except exactly zero intentionally enable these switches. Setting another process’s launch environment is an unmet prerequisite, not a demonstrated security bypass.

**Claim.** Accepting any nonempty value except exactly `"0"` lets an injected environment variable disable crash reporting or systemd integration and therefore constitutes a security defect.

**Evidence — source-confirmed.** `src/helpers/env/Env.cpp:5–7` explicitly implements the rule:

```cpp
const auto ret = getenv(env);
return ret && ret[0] != '\0' && !(ret[0] == '0' && ret[1] == '\0');
```

`src/Compositor.cpp:295–298` conditionally installs the reporter; lines 582–583 and 824–825 gate notifications. Lines 795–815 gate session-target/environment management on a backend session, `HYPRLAND_NO_SD_VARS`, `MANAGERPID`, build-time systemd support, and `HYPRLAND_NO_SD_TARGET`.

**Parser check — executed, isolated.** The baseline `Env.cpp` was included directly in `checks.cpp`. Empty and `"0"` returned false; `"1"`, `"false"`, and `"00"` returned true (`checks-results.txt`). This confirms the implementation without establishing an injection route.

**Input chain and guard — source-confirmed.** A session launcher supplies the environment → `getenv` returns that process's own value → the helper deliberately evaluates it → the caller skips an optional integration. Empty/unset and exactly `"0"` leave the switches disabled. There is no client-controlled argument that changes the compositor's environment on this path and no authorization check being bypassed. Even strict boolean parsing would still permit an environment controller to use `"1"`; it would not close the dossier's proposed trust boundary.

**Severity.** None as a security finding. Unexpected values such as `"false"` can surprise an operator, but the intended feature permits disabling these facilities. Notification failure is relevant only for a supervisor/unit that actually depends on a readiness notification, not every systemd session. Disabling the handler means normal fatal-signal behavior; it does not by itself prove a particular system core was generated or that all external evidence disappeared.

**Corrections.** The proposed replacement `truthy()` is not a strict parser either: `src/helpers/MiscFunctions.cpp:465–478` accepts case-insensitive prefixes of `true`, `yes`, and `on`. Changing this established toggle convention would be a compatibility decision rather than a fix for environment injection.

**Minimal fix direction.** No security fix required for this claim. Document the values and, if desired, log explicitly disabled diagnostic/integration facilities. A change in accepted values needs an intentional compatibility policy.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
