# CMS-01 Synchronized subsurface image descriptions take effect before the parent commit

- Agent confirmation: `confirmed` (source review; code path traced end to end).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-01 — Medium

`CWLSurfaceResource::commitState()` (`src/protocols/core/Compositor.cpp:657-700`) copies the whole queued state, including `imageDescription` (`src/protocols/types/SurfaceState.cpp:217`), into `m_current` immediately; for a synchronized subsurface it only suppresses the `commit` event. Rendering reads `m_current.imageDescription` (`src/render/OpenGL.cpp` `SOURCE_IMAGE_DESCRIPTION`), so any repaint shows the new description before the parent commits.

Trigger: map a synchronized subsurface with description A, `set_image_description(B)` (or `unset_image_description`), commit only the child, then cause an overlapping repaint.

Spec: `color-management-v1.xml` — image description and rendering intent are double-buffered state (see `wl_surface.commit`); `wayland.xml` requires the synchronized subsurface's cached state to be applied atomically with the parent's.

Residual of CM-03: fork commit `2db127690` claims the queued-state fix, and its `CachedSubsurfaceStatePreservesDescriptionAndUnset` test covers `mergeFrom()` only, not parent/child commit scheduling.

Root cause is broader than CM: Hyprland applies *all* synchronized-subsurface state (buffer, damage, CM) on the child commit. A correct fix is a sync-subsurface state cache for the whole surface model.

Fork status: **not fixed** (architectural; out of scope for a CM patch). Upstream `12f242511`: same behaviour.
