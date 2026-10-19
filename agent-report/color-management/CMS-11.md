# CMS-11 Border gradients re-encode with gamma 2.2 what was decoded as piecewise sRGB

- Agent confirmation: `confirmed` (source review; numerically verified).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-11 — Low

Gradient stops enter OkLab through hyprgraphics' piecewise sRGB decode (verified: 0.02 → 0.001548), but `okLabAToSrgb()` (`gradient.glsl`) re-encoded with gamma 2.2 and the border shader decoded with the SDR EOTF policy description. With `cm_sdr_eotf = srgb` a 0.02 stop renders 2.71× too bright in linear light (≈1.05 vs 0.51 nit at 203-nit reference); without CM the gamma-2.2 values were written into an sRGB buffer.

Fork fix `9171f2d62`: encode with sRGB and describe border colors as `DEFAULT_SRGB_IMAGE_DESCRIPTION`, like solid colors. Shader variants validated with glslang.

Upstream `12f242511`: same code, not fixed.
