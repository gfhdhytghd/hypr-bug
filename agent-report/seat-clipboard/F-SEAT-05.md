# F-SEAT-05 OutputManagement set_custom_mode 无尺寸/刷新率上限，test 为 stub 恒真 → 巨幅 framebuffer OOM / 整数溢出

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Test is a success stub and dimensions reach swapchain setup; backend checks exist, so OOM and overflow remain unverified.

**Verdict: partially. Revised severity: Medium.**

**Claim.** Custom-mode dimensions/refresh are insufficiently bounded, protocol test always succeeds, and applying large modes necessarily causes huge allocation or integer overflow.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:462-484` rejects non-positive dimensions and negative refresh only; zero refresh uses the current rate. `:351-355` is explicitly a test stub:

```cpp
if (test) {
    LOG(Log::WARN, "TODO: STUB: applyTestConfiguration for test not implemented, returning true.");
    return true;
}
```

`test` sends succeeded from that result (`:326-332`). Custom size/rate are saved at `:388-391`; `src/config/shared/monitor/MonitorRuleManager.cpp:55-59` copies them into the monitor rule, converting mHz to Hz correctly.

**Path and backend boundaries — source-confirmed.** Configuration input reaches the saved override, scheduled reload (`OutputManagement.cpp:422-424`), monitor rule application (`MonitorRuleManager.cpp:133-173`), and requested-mode selection (`src/output/Monitor.cpp:786-807`). Mode application calls `applyModeWithSwapchain` or `applyCustomModeWithSwapchain` (`:846-865`, `:887-905`). Both update the swapchain (`:2643-2650`); `updateSwapchain` passes mode size to `swapchain->reconfigure` (`:2621-2641`). Thus a requested size reaches allocation-related setup; it is not simply rejected by an EDID-membership guard before that setup.

However, `CMonitorState::test` also checks swapchain update and calls the backend's output test (`Monitor.cpp:2612-2618`). The mode-selection loop rejects failed tests and tries fallbacks (`:852-865`, `:908-936`). Protocol `test` success does not bypass these later tests, and clients are not required to call protocol `test` before `apply` in the first place.

**Preconditions and impact.** The output-management global is unavailable to security-context sandbox clients, but ordinary unmarked socket clients can bind it (`src/Compositor.cpp:267-271`; `src/managers/ProtocolManager.cpp:348-403`). Medium for false validation success and missing resource-policy bounds before swapchain setup. Exact allocation success, memory pressure, backend limits, and any arithmetic overflow are **not verified**; resolving those requires an isolated backend/allocator test with bounded resources or a separately traced concrete arithmetic sink. No universal wlroots 8192-pixel/1000-Hz rule was established from available source; the protocol XML specifies no such constants (`protocols/wlr-output-management-unstable-v1.xml:556-566`). The dossier's unconditional High OOM/UB conclusion is not supported.

**Minimal fix direction.** Implement real side-effect-free protocol testing, validate complete configuration against backend capabilities and allocation budgets, and check dimensions/derived allocation sizes before swapchain setup. Report failure consistently and retain backend fallback behavior.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
