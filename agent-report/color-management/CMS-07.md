# CMS-07 Downscaled built-in wallpaper loses its encoding and can clip

- Agent confirmation: `confirmed` (source review; writer and reader traced).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-07 — Medium

When the wallpaper is larger than the output, `getBackground()` (`src/render/Renderer.cpp`) scales it into an untagged, default-format (8-bit) framebuffer. Writing falls back to the work-buffer description (`OpenGL.cpp` target fallback), so the copy is encoded in e.g. linear / PQ space and clipped to [0,1]; the cached texture is reused untagged and decoded as default SDR. HDR / FP16 / wide outputs get a double conversion and wrong brightness. Same class as SIDE-09.

Fork fix `e04ff4074`: source texture and copy are both tagged with the default SDR description, making the scale a pure SDR→SDR copy.

Upstream `12f242511`: same code, not fixed.
