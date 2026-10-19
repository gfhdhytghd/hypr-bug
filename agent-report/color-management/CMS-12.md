# CMS-12 Withdrawing HDR support leaves PQ HDR_OUTPUT_METADATA over SDR pixels

- Agent confirmation: `confirmed` (source review; traced through aquamarine).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-12 — Medium

Applying `supports_hdr = -1` (or `supports_wide_color = -1`) to an output signalling HDR makes the HDR preset fall back to sRGB, but `handleFullscreenSettings()` (`src/render/Renderer.cpp`) only touched the metadata inside `if (supportsHDR())`. Aquamarine only sends `HDR_OUTPUT_METADATA` when it is staged, so the previous PQ blob (EOTF = 2) stays while Colorspace returns to Default. KMS gives the metadata EOTF precedence, so the display decodes SDR pixels as PQ. Violates the project's "metadata blob 0 on return to SDR" requirement.

Fork fix `9736f2fe6`: when HDR is unsupported and the current metadata isn't the SDR one, stage SDR metadata.

Upstream `12f242511`: same code, not fixed.
