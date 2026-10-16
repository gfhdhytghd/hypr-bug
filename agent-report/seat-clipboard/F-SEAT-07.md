# F-SEAT-07 create_configuration 不校验 done-serial、不限制并发配置、stop 后仍可操作

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Stale serial and stopped-manager requests lack checks; concurrent objects alone are not a protocol violation or demonstrated DoS.

**Verdict: partially. Revised severity: Medium.**

**Claim.** Configuration freshness is unchecked, stopped managers remain usable, and unlimited concurrent configurations allegedly violate a one-configuration rule and cause memory DoS/stale-head races.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:22-35` receives `serial` but neither stores nor validates it. `sendDone` generates and immediately sends a serial without retaining it (`:86-88`). `COutputConfiguration` stores an owner and heads, not the requested state serial (`src/protocols/OutputManagement.hpp:132-147`).

```cpp
// OutputManagement.cpp:20, 87
m_resource->setStop([this](CZwlrOutputManagerV1* r) { m_stopped = true; });
m_resource->sendDone(wl_display_next_serial(g_pCompositor->m_wlDisplay));
```

`m_stopped` gates advertising new heads (`:51-53`), but does not gate create, configure, test, or apply. The protocol says the client must stop sending manager requests after stop (`protocols/wlr-output-management-unstable-v1.xml:108-114`); current code does not enforce that lifecycle. It also describes cancellation when output state changed and the client has outdated information (`:507-514`), which cannot be implemented correctly with the received serial discarded.

**Path and reachability — source-confirmed.** An allowed unsandboxed manager's configuration request allocates a protocol-owned object (`OutputManagement.cpp:25-34`), whose later apply directly saves monitor state and schedules reload (`:351-429`). No stored freshness check intervenes, including after a later done/hotplug event. For directly disappeared heads, monitor-destroy resets the head monitor (`:97-115`), and enable/disable reject an absent monitor (`:277-281`, `:306-310`); apply also skips an expired configuration-head monitor (`:370-373`). These guards refute treating every stale object as a dereference of freed memory. No concurrent thread race was established; event-driven stale configuration is the supported mechanism.

**Corrections and severity.** Medium for freshness/lifecycle violations that allow stale display-state mutations. The local protocol XML imposes complete-head and single-use transaction rules (`:414-435`, `:456-481`), not a blanket one-live-configuration-per-manager rule. Resource vectors grow while clients retain objects, but this alone is ordinary Wayland resource accounting, not a demonstrated special memory-amplification vulnerability; destruction erases configurations (`OutputManagement.cpp:610-611`). Actual resource exhaustion was not executed. As in F-SEAT-06, this global is filtered for security-context sandbox clients.

**Minimal fix direction.** Store a configuration snapshot/generation, cancel stale transactions on relevant output changes, enforce manager stop and configuration single-use states, and release child state with its transaction. Consider quotas separately from protocol correctness; do not invent a one-concurrent-configuration requirement.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
