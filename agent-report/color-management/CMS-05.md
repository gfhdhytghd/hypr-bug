# CMS-05 Changing only sdr_eotf is ignored on reload

- Agent confirmation: `confirmed` (source review; source-confirmed).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-05 — Medium

`CMonitorRule::compare()` (`src/config/shared/monitor/MonitorRule.cpp`) omits `m_sdrEotf` from the soft CM comparison, so an EOTF-only change (`sdr_eotf = srgb` → `gamma22`) is a full match and `ensureMonitorStatus()` skips the monitor. The old transfer function stays until another compared property changes or the monitor is recreated.

Fork fix `db14ca3b7` (unit test `SdrEotfChangeIsSoft`).

Upstream `12f242511`: same code, not fixed.
