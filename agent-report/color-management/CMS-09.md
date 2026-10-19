# CMS-09 Clear colors (misc:background_color) are written unconverted into HDR / linear work buffers

- Agent confirmation: `confirmed` (source review; source-confirmed).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-09 — Medium

Found while verifying CMS-08. `CClearPassElement` colors are sRGB config colors but `CGLElementRenderer` passed them raw to `glClearBufferfv` on the work buffer, while every other config color goes through `getConvertedColor()`. On PQ or FP16-linear work buffers the background color is reinterpreted as PQ / linear values.

Fork fix `e04ff4074`: attachment 0 is cleared with `getConvertedColor()` for the current FB.

Upstream `12f242511`: same code, not fixed.
