# CMS-04 Soft ICC change or removal keeps the previous profile's VCGT

- Agent confirmation: `confirmed` (source review; call path traced).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-04 — Medium

`CMonitor::applyMonitorRuleSoft()` (`src/output/Monitor.cpp`) replaces the ICC image description, but only the hard `applyMonitorRule()` path called `updateVCGTRamps()`. Changing only `icc` (A → B) or removing it goes through the soft path (`MonitorRuleManager.cpp`), so B runs with A's calibration ramps, or A's VCGT stays active after the ICC is removed; `m_vcgtRampsSet` also stays stale.

Fork fix `db14ca3b7`: `applyMonitorRuleSoft()` updates the VCGT ramps (the hard path calls it).

Upstream `12f242511`: same code, not fixed.
