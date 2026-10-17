# REV-08 First direct-scanout HDR frame carries the composited metadata

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-08 — Low/Medium

In `src/render/Renderer.cpp` (upstream ~:2556) the per-surface HDR metadata path is only taken once `m_lastScanout` is set, but `m_lastScanout` is assigned only after `attemptDirectScanout` has already committed. The first DS frame therefore goes out with the monitor's composited HDR_OUTPUT_METADATA, and the surface's metadata follows one frame later: a metadata flip on every DS entry. Related to, but distinct from, CM-26 (mode-switch timing).

Fork status: **not fixed** — fork `Renderer.cpp:2457` still keys on `m_lastScanout` (set in `Monitor.cpp:2132-2228` after commit).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
