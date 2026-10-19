# CMS-08 Background clears never update the SDR mirror attachment

- Agent confirmation: `confirmed` (source review; attachment/copy path traced).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-08 — Medium

With `render:keep_unmodified_copy = 1` (or SDR mods) the work buffer carries the SDR mirror texture as `GL_COLOR_ATTACHMENT1` (`GLFramebuffer.cpp`), which capture and mirroring read (`OpenGL.cpp` `saveBufferForMirror`, `ScreenshareFrame.cpp:190`). `CGLElementRenderer::draw(CClearPassElement)` cleared attachment 0 only, so background-color areas in captures / mirrors stayed black, stale or undefined.

Fork fix `e04ff4074`: clears also clear attachment 1 with the (sRGB) color.

Upstream `12f242511`: same code, not fixed.
