# REV-07 ICC image descriptions never compare equal, so every reload rebuilds the 3D LUT

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-07 — Low

`CImageDescription` equality returns false whenever `icc.present`, so each monitor reload with an ICC profile (`src/output/Monitor.cpp:658`) interns a new description and builds a new 33³ LUT texture. Upstream additionally never frees it (see SIDE-07).

Fork status: partial — fork interning is weak/hashed so old LUTs are freed, but the LUT is still rebuilt on every reload.

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
