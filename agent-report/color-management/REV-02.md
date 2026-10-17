# REV-02 Default tonemap curve compresses reference white (no knee)

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-02 — Medium

`src/render/shaders/glsl/tonemap.glsl:68-72` (non-ALT path) is an extended-Reinhard curve on `L/dstRef` with no linear segment: `L' = L(1+L·v)/(1+L)`. At reference white (L = 1) output is `(1+v)/2`. Example PQ 10000-nit source on a 600-nit target (dstRef 203): srcScale 49.26, dstScale 2.956, v ≈ 0.041 → 203 nit maps to ≈ 106 nit; all mid-tones are darkened whenever tonemapping is active. BT.2390 EETF (and the ALT path) leave content below the knee untouched. Numbers hand-computed and re-checked.

Fork status: fixed — BT.2390 EETF in ICtCp, identity below the knee (`tonemap.glsl:57-105`).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
