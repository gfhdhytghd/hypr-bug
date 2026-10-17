# REV-09 Tonemap shader inverts a uniform matrix per fragment; dead srcRefLuminance

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-09 — Low (perf / cleanup)

`src/render/shaders/glsl/tonemap.glsl:49` computes `inverse(dstXYZ)` for every fragment though it depends only on uniforms; it should be precomputed on the CPU. `srcRefLuminance` is uploaded (`OpenGL.cpp:1290`) and threaded through `doColorManagement`/`tonemap` but never read.

Fork status: **not fixed** — fork `tonemap.glsl:84`, `cm_helpers.glsl:323`, `OpenGL.cpp:1287`.

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
