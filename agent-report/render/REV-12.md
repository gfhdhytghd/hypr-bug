# REV-12 Input-region debug overlay draws the whole monitor

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-12 — Low (debug only)

`src/render/pass/Pass.cpp:349-351` computes `BOX = hlSurface->getSurfaceBoxGlobal()` but draws `.box = box` (the outer full-monitor box), so the pointer-focus input-region overlay tints the entire screen instead of the surface.

Fork status: **not fixed** (file identical).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
