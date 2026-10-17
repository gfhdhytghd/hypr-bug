# REV-03 Tonemapping is applied to premultiplied-alpha colour

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-03 — Low

`src/render/shaders/glsl/cm_helpers.glsl:246-249` multiplies `pixColor.rgb` by alpha before calling the non-linear `tonemap()`. Translucent HDR pixels and anti-aliased edges are therefore under-compressed and get a different hue/brightness than the opaque interior. Previously noted only as an unnumbered spot-check item in the CM verification evidence.

Fork status: fixed — tonemap on un-premultiplied colour (`cm_helpers.glsl:334,358-365`).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
