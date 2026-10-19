# CMS-10 Groupbar gradient textures are untagged and follow the SDR EOTF policy

- Agent confirmation: `confirmed` (source review; source-confirmed).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-10 — Low

`CHyprGroupBarDecoration` rasterizes config gradient colors with cairo into an untagged texture; the renderer falls back to `getDefaultImageDescription()` (gamma 2.2 with `render:cm_sdr_eotf = gamma22`), while solid config colors are explicitly sRGB (`getConvertedColor`). The same color differs between gradient and solid indicators.

Fork fix `9171f2d62`: the texture is tagged sRGB.

Upstream `12f242511`: same code, not fixed.
