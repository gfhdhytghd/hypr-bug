# Upstream render + color-management review (2026-10-17)

- Official baseline: `origin/main` `25f09b26f` (2026-10-16).
- Fork comparison: `main` `e5a03b6a5` (merge of `CM-issue-fix`).
- Method: static source review by three parallel agents (CM protocol, HDR pipeline, renderer architecture) plus spot checks of each high/medium claim by the orchestrating agent. No runtime or physical-display testing unless a report says otherwise.
- Purpose: record defects that are **not already tracked** in this list. Items found again that were already tracked (CM-01..29, HDR-COM-*, SIDE-04..08, HDR-LIM-*) are only cross-referenced below.

## New entries

| ID | Area | Severity | Summary | Fork status @ `e5a03b6a5` |
|---|---|---|---|---|
| REV-01 | CM shader | Medium | `saturate()` uses the green primary's XYZ column as luminance weights | fixed (Y row, `cm_helpers.glsl:39`) |
| REV-02 | CM tonemap | Medium | Default tonemap curve has no knee and compresses reference white (~203 → ~106 nit) | fixed (BT.2390 EETF) |
| REV-03 | CM tonemap | Low | Tonemap runs on premultiplied colour | fixed |
| REV-04 | CM protocol | Low (latent) | `wp_image_description_reference_v1` resolved as wrong type; `setData` never called | fixed |
| REV-05 | CM protocol | Low | Deprecated `preferred_changed(0)` sent to v2 feedbacks; `m_currentPreferredId` only tracked by `get_preferred_parametric`; uninitialised `m_hdrMetadata` | fixed |
| REV-06 | ICC | Low/Medium | `cmsCreateXYZProfile()` leaked; ICC primaries derived with relative intent + BPC (white adapted toward D50) | **not fixed** |
| REV-07 | ICC | Low | ICC image descriptions never compare equal → every reload rebuilds the 3D LUT (upstream also retains it forever, cf. SIDE-07) | partial (no leak, still rebuilt) |
| REV-08 | HDR DS | Low/Medium | First direct-scanout HDR frame carries the monitor/composited metadata instead of the surface's | **not fixed** |
| REV-09 | CM shader perf | Low | Per-fragment `inverse(dstXYZ)` in tonemap; dead `srcRefLuminance` uniform/parameter | **not fixed** |
| REV-10 | Renderer | Medium | Popups of unfocused non-floating windows are skipped by the popup pass | **not fixed** |
| REV-11 | Renderer | Low | `static` `zoomLock` / `damageBlinkCleanup` shared across monitors | **not fixed** |
| REV-12 | Renderer debug | Low | Input-region debug overlay draws the whole monitor box | **not fixed** |
| REV-13 | Shaders | Low | Shader compile/link failures leak GL objects; a failed variant (program 0) is cached forever | **not fixed** |
| REV-14 | GL init | Low | Missing EGL fence procs are a hard `RASSERT` though explicit sync is otherwise optional | **not fixed** |
| REV-15 | Renderer | Info | Architecture / performance observations (not defects) | n/a |

## Re-found, already tracked (not duplicated)

CM-01, CM-02 (incl. `get_information` on failed descriptions), CM-03, CM-04–11, CM-13, CM-14, CM-15, CM-16, CM-17 (incl. missing primaries range/degeneracy checks), CM-19, CM-21, CM-22, CM-23, CM-26, CM-27, CM-28/29 (80-nit SDR white), HDR-COM-03 (SDR mod after encoding), SIDE-07 (static caches; upstream additionally keeps `OpenGL.cpp` `primariesConversionCache`, never pruned), HDR-LIM-02.

Not recorded as defects: PQ black floor 0.005 nit (follows the protocol's PQ default / min+10000 rule); sRGB-tagged v1 surfaces decoded piecewise and emitted as gamma 2.2 under `auto` (policy question).
