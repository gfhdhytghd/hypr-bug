# SIDE-09 Toggling blur off and on drops the precomputed blur's image description (tiled-window blur mis-converted)

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below plus a unit test against the framebuffer allocation path; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `5d9395bfa` (fixed in `6860bf7f3`).

SIDE-09 — Medium — Source-confirmed

Trigger: `decoration:blur:enabled` off → on (e.g. a keybind toggling blur) on an output whose work-buffer description is not the default SDR one (HDR, FP16, wide / EDID / ICC presets).

1. `CMonitorResources::refreshBlurFB()` (`src/output/MonitorResources.cpp:178-190`) reallocates the blur FB to 1×1 when blur is disabled and back to output size when it is re-enabled.
2. `IFramebuffer::alloc()` (`src/render/Framebuffer.cpp:9-25`) calls `m_tex.reset()` on any size or format change.
3. `CGLFramebuffer::internalAlloc()` (`src/render/gl/GLFramebuffer.cpp`) creates a fresh texture; `ITexture::m_imageDescription` defaults to null and the framebuffer's stored `m_imageDescription` is not copied onto it.
4. Nothing retags it: `CMonitorResources::setImageDescription()` and `CMonitor::resources()` only act when the description *value* changes, which a blur toggle does not do. `IFramebuffer::imageDescription()` returns the texture's (null) description whenever a texture exists, so the stored value is unreachable.

Effect: `preBlurForCurrentMonitor()` still writes work-buffer-encoded data (target falls back to `WORK_BUFFER_IMAGE_DESCRIPTION`), but when a window samples it (`CHyprOpenGLImpl` blurred-background path, `renderTextureInternal(ctx, data.blurredBG, …)`), `SOURCE_IMAGE_DESCRIPTION` falls back to the *window's* surface description (`ctx.m_data.surface`) or `getDefaultImageDescription()`. The precomputed blur is therefore converted from the wrong colour space, and the error varies with each window's own description.

Only windows using the precomputed blur are affected: `shouldUseNewBlurOptimizations()` returns true for non-floating, non-special-workspace windows with `new_optimizations` (or with `xray`). Floating windows use live blur from a correctly tagged work buffer and look correct. The state persists until the output's image description changes or the monitor resources are rebuilt.

Possibly related community reports (not verified against this mechanism): [#13918 Blur renders extremely dark with HDR](https://github.com/hyprwm/Hyprland/discussions/13918), [#13771 HDR + blur](https://github.com/hyprwm/Hyprland/discussions/13771).

Fork fix `6860bf7f3`: `IFramebuffer::alloc()` copies the stored `m_imageDescription` onto the (re)created texture, covering resize, format change and `release()` + `alloc()` for every framebuffer. Unit tests `tests/render/Framebuffer.cpp` (3 of 4 fail without the fix).

Upstream status: same code at `12f242511`; not fixed.
