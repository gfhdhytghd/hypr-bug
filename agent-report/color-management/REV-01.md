# REV-01 GLSL saturate() uses the green primary's XYZ as luminance weights

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-01 — Medium

`passCMUniforms` (`src/render/OpenGL.cpp:1312-1318`) uploads `dstPrimaries2XYZ` untransposed, so in GLSL `primaries[1]` is **column** 1 = XYZ of the green primary. `saturate()` (`src/render/shaders/glsl/cm_helpers.glsl:15`) uses it as luminance weights; the correct weights are the Y **row** (`m[0][1], m[1][1], m[2][1]`). For sRGB the weights become 0.358/0.715/0.119 (sum ≈ 1.19) instead of 0.2126/0.7152/0.0722, so any `sdrsaturation` < 1 brightens greys by up to ~19% and shifts hue. The CPU path (`src/helpers/cm/ColorManagement.cpp:479`) uses the row, so CPU and GPU disagree. hyprgraphics `CMatrix3` is row-major (`m[row][col]`, `operator*(XYZ)`).

Fork status: fixed — fork passes explicit luminance coefficients (Y row) as uniforms (`cm_helpers.glsl:39`).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
