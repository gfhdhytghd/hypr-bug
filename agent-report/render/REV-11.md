# REV-11 Per-monitor render state kept in function-local statics

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-11 — Low

`src/render/Renderer.cpp:2314-2320` `static bool zoomLock` toggles `lockSoftwareAll/unlockSoftwareAll` from each monitor's own zoom factor. With one zoomed monitor (live cursor-zoom gesture zooms one output), rendering monitors in turn flips the lock every frame, calling `updateCursorBackend()` repeatedly and flickering between SW/HW cursors. `static int damageBlinkCleanup` (`:2223`) is likewise shared across monitors.

Fork status: **not fixed** (fork `Renderer.cpp:2258`).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
