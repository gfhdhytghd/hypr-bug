# REV-10 Popups of unfocused non-floating windows are skipped by the popup pass

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-10 — Medium

`src/render/Renderer.cpp:487-507`: the non-floating main pass renders each window with `RENDER_PASS_MAIN` (which does not draw popups, `:670` vs `:796`) and then `w.reset()`s the entry in `windows`; only the focused window is kept (as `lastWindow`). The following popup pass (`:515-532`) does `if (!w) continue;`, so it only ever draws popups of the focused tiled window. Introduced by `54441e0c4` (Feb 2025). Largely masked when focus follows the mouse; visible with e.g. tooltips/menus of an unfocused tiled window (`follow_mouse = 0/2`, or popups that outlive focus changes). Runtime reproduction pending.

Fork status: **not fixed** (fork `Renderer.cpp:506`).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
