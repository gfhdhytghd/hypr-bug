# F-SEAT-08 set_mode 不校验 mode 与 head 的归属 → 跨 head 模式注入

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Mode-to-head ownership is unchecked; foreign same-client mode dimensions reach the other head’s rule, with backend testing still present.

**Verdict: confirmed. Revised severity: Medium.**

**Claim.** A mode belonging to one output head can be assigned to another without the required invalid-mode rejection.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:438-459` resolves a mode by resource identity globally (`:635-641`), checks that its backend mode and destination monitor are live, checks duplicate property state, then stores it. There is no head-identity comparison:

```cpp
// OutputManagement.cpp:456-457
m_state.committedProperties |= OUTPUT_HEAD_COMMITTED_MODE;
m_state.mode = MODE;
```

`COutputMode` stores only the resource and weak backend mode (`src/protocols/OutputManagement.hpp:74-85`); it cannot directly prove head ownership. Mode creation comes from each head (`OutputManagement.cpp:214-226`), but that relationship is not consulted by `set_mode`. The XML explicitly defines `invalid_mode` as a mode not belonging to the head (`protocols/wlr-output-management-unstable-v1.xml:539-553`).

**Path and preconditions — source-confirmed.** An unsandboxed client permitted to bind output management and holding valid mode resources for two distinct heads can supply a mode through a different head's configuration. Wayland interface validation does not enforce this semantic ownership relation: both resources have the correct protocol interface and belong to that same client. Apply reads the foreign mode's size/refresh (`OutputManagement.cpp:383-387`) and stores them under the destination monitor name (`:424`), then `src/config/shared/monitor/MonitorRuleManager.cpp:55-59` puts them into that monitor's rule. Both mode and monitor must still be live for this ordinary path; this finding does not rely on expiration behavior.

**Severity and corrections.** Medium for a concrete protocol ownership/integrity defect. Actual output uses the destination's mode-selection and backend test/fallback machinery (`src/output/Monitor.cpp:786-807`, `:846-936`), so the foreign backend mode object is not simply passed through as another monitor's EDID capability. Forced no-signal behavior or a stable desktop DoS was not verified. The dossier's `src/protocols/core/OutputManagement` prefix is wrong; the file is directly under `src/protocols`.

**Minimal fix direction.** Associate each advertised mode with its owning head/monitor and compare that identity before accepting it. Reject foreign modes with the specified `invalid_mode` error; retain backend validation for legitimate custom modes.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
