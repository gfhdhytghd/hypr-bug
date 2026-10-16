# F-PERM-05 Informational: enforcement is disabled by default

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Enforcement defaults off and both permission APIs immediately allow; this is an explicit default policy, not an independent privilege-escalation defect.

**Claim.** Permission enforcement is disabled by default, so permission checks do not prompt. **Verdict: confirmed as an informational policy/control record.**

**Evidence — source-confirmed.** `src/config/values/ConfigValues.cpp:717` registers:

```cpp
MS<Bool>("ecosystem:enforce_permissions", "whether to enable permission control.", false),
```

`src/managers/permissions/DynamicPermissionManager.cpp:81-84` and `153-156` both return `PERMISSION_RULE_ALLOW_MODE_ALLOW` immediately when the setting is zero, before consulting configured or cached rules. This also means an explicit deny rule is ineffective while enforcement is disabled.

**Caller chain and reachability — source-confirmed.** A valid screencopy frame request enters `src/protocols/Screencopy.cpp:64-65,135`, reaches the screenshare frame machinery, and checks permission in `src/managers/screenshare/ScreenshareFrame.cpp:162,367`. Input capture checks on activation, after its enabled-state prerequisite, at `src/protocols/InputCapture.cpp:212-218`. Plugin loading checks at `src/plugins/PluginSystem.cpp:16-21`; keyboard configuration checks at `src/managers/input/InputManager.cpp:1226`. All receive allow from the permission API under the default. Protocol availability, valid buffers/resources, IPC access, input-capture session state and other independent caller restrictions still apply.

**Severity and corrections.** Informational: this is the declared configuration default, not proof that every client can perform every listed operation, evade session-lock rendering protections, or execute a plugin through Wayland. The other dossiers' crash and lock-screen claims were not verified here. Keyboard prompting additionally requires an applicable ask rule even when enforcement is enabled (`DynamicPermissionManager.cpp:222-224`).

**Minimal direction.** Document the default and the requirement to enable enforcement. Any change to the default needs compatibility review and a separate wiki update; it is not justified as an unnoticed implementation fix by this record alone.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
