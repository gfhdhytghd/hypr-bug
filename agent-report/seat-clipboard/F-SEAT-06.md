# F-SEAT-06 OutputManagement 无会话锁/权限门控 → 锁屏期间任意客户端禁用或重配所有显示器

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: No lock gate for unsandboxed output-management clients; sandbox visibility is restricted, and authentication bypass is unproven.

**Verdict: partially. Revised severity: Medium.**

**Claim.** Any client can reconfigure or disable displays while locked because output management lacks all permission/session-lock checks.

**Evidence — source-confirmed.** The global is registered unconditionally at `src/managers/ProtocolManager.cpp:195`; `src/protocols/OutputManagement.cpp:586-595` binds without a session-lock check. The configuration handlers and apply path likewise have no lock check (`:269-344`, `:351-429`). In particular:

```cpp
// OutputManagement.cpp:319-323
newState.enabled = false;
Config::monitorRuleMgr()->scheduleReload();
m_owner->m_monitorStates[PMONITOR->m_name] = newState;
```

**Path and guards — source-confirmed.** A client allowed to bind the global can change saved output state during an existing lock. The override sets `rule.m_disabled` (`src/config/shared/monitor/MonitorRuleManager.cpp:43-59`), reload applies the rule (`:133-186`), and `src/output/Monitor.cpp:699-711` sets output disabled and commits. No intervening session-lock authorization appears on that path. Notably disable is saved and reload scheduled **in the disable handler itself**, before apply; this is stronger evidence of non-transactional behavior than the dossier's apply-only description. Existing overrides from earlier managers can take precedence because `getOutputStateFor` returns the first matching manager (`OutputManagement.cpp:644-649`), so success is configuration-dependent.

The claim of no permission boundary is false: `src/Compositor.cpp:267-271` filters globals for clients recorded as sandboxed, and output management is absent from the allowlist (`src/managers/ProtocolManager.cpp:360-403`). Sandbox classification is an explicit tracked-client list (`src/protocols/SecurityContext.cpp:221-222`), not a claim that every externally sandboxed application is automatically recognized. An ordinary unmarked local connection can access this privileged display-control protocol.

**Impact and corrections.** Medium for lock-time display-availability/policy exposure by an unsandboxed client. Physical black-screen behavior and inability to unlock were not tested; fallback-monitor machinery exists (`src/state/FallbackState.cpp:27-39`, `:100-139`). This does not prove authentication bypass, password capture, or exposure of unlocked application content. Display control is the protocol's intended authority (`protocols/wlr-output-management-unstable-v1.xml:42-70`), so availability policy should be distinguished from an arbitrary-client memory-corruption flaw.

**Minimal fix direction.** Establish explicit policy for display changes while locked, enforce it at bind and mutation/commit as appropriate, and stage disable changes until an authorized atomic apply. Preserve legitimate trusted display-management clients and backend hotplug handling.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
