# CMS-03 ICC 3D LUT sampled uniformly in linear light crushes shadows

- Agent confirmation: `confirmed` (source review; numerically verified with LCMS).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-03 — Medium

`buildIcc3DLut()` (`src/helpers/cm/ICC.cpp`) builds a 33³ LUT from a *linear* sRGB source profile, sampled uniformly in linear light, and `applyIcc3DLut()` (`cm_helpers.glsl`) indexes it with linear values. The steep low end of the output TRC is interpolated from very few samples: with a standard sRGB ICC, input 0.10 came out as 0.0622 instead of 0.1000 (≈ −9.6 eight-bit codes); an identity profile visibly alters the image.

Trigger: `icc = /path/sRGB.icc` on any monitor.

Fork fix `ab41322e6`: the LUT source profile uses the piecewise IEC 61966-2-1 sRGB TRC and the shader sRGB-encodes before the lookup, so an sRGB display maps to the identity grid. Unit test `SRGBProfileLUTIsIdentityInEncodedDomain`; all 32 relevant shader variants validated with glslang.

Upstream `12f242511`: same code, not fixed.
