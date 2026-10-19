# CMS-06 Config reload never re-reads an unchanged ICC path or the VCGT setting

- Agent confirmation: `confirmed` (source review; source-confirmed).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-06 — Low

Rule equality compares only the ICC filename, so rewriting the profile at the same path, or toggling `render:icc_vcgt_enabled`, followed by a config reload, never reaches `fromICC()` (`src/config/shared/monitor/MonitorRuleManager.cpp:158`).

Fork fix `db14ca3b7`: a config reload (`scheduleReload(true)` from the config manager only, not hotplug) soft-reapplies monitors with an ICC rule; `fromICC()` still reuses the LUT when the content is unchanged.

Upstream `12f242511`: same code, not fixed.
