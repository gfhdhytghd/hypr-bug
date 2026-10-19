# CMS-13 CTM interop fallback disables required shader CM

- Agent confirmation: `confirmed` (source review; traced through aquamarine).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-13 — Medium

In `handleFullscreenSettings()`, when `canNoShaderCM()` fails (e.g. SDR fullscreen app on a PQ output) and `non_shader_cm_interop` forbids combining with hyprsunset, the fallback stages an identity CTM and sets `m_noShaderCTM = true`. `doesNoShaderCM()` returned that same flag, and `OpenGL.cpp` skips *all* shader CM when it is set, so SDR pixels reach the connector unconverted while `BT2020_RGB` + PQ metadata are signalled.

Trigger: HDR output, fullscreen SDR app, non-identity hyprsunset CTM, `cm_auto_hdr = 0`, `non_shader_cm = always`, `non_shader_cm_interop = 0`, `use_fp16 = 0`, no direct scanout.

Fork fix `5bcfb19b9`: separate `m_noShaderCM` (hardware performs the CM) from `m_noShaderCTM` (CTM staged, restore needed).

Upstream `12f242511`: same code (`doesNoShaderCM()` returns `m_noShaderCTM`), not fixed.
