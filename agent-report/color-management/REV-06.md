# REV-06 ICC loader leaks the XYZ profile and adapts primaries toward D50

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-06 — Low/Medium

`src/helpers/cm/ICC.cpp:248` passes `cmsCreateXYZProfile()` directly into a transform and never `cmsCloseProfile`s it (leak per ICC load). `:245-257` derive the monitor primaries/white through a relative-colorimetric transform with black-point compensation, so the reported white point is the PCS white (~D50) instead of the panel's native white (media white / `chad`-inverted), and the primaries are chromatically adapted accordingly.

Fork status: **not fixed** (fork `ICC.cpp:246-250` unchanged in substance).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
