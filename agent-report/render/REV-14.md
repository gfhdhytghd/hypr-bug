# REV-14 Missing EGL native-fence procs abort the compositor

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-14 — Low

`src/render/OpenGL.cpp:315-317` hard-`RASSERT`s `eglCreateSyncKHR`, `eglDupNativeFenceFDANDROID` and `eglWaitSyncKHR`, although explicit sync / native fences are otherwise feature-checked and have `glFinish` fallbacks (`GLRenderer.cpp`). Drivers without `EGL_ANDROID_native_fence_sync` crash at startup instead of degrading. GPU reset handling is likewise a `RASSERT` (`:735-746`).

Fork status: **not fixed**. Needs confirmation that all fence users tolerate missing procs before downgrading the assert.

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
