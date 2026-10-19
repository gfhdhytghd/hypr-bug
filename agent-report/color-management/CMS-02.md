# CMS-02 get_preferred hands out the output's ICC description, which surfaces then decode as gamma 2.2

- Agent confirmation: `confirmed` (source review; code path traced end to end).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `b0136905e`. Found by the 2026-10-19 fork CM/HDR sweep (Codex agents, re-verified by Claude), see [sweep overview](_evidence/fork-cm-sweep-2026-10-19.md).

CMS-02 — Medium

On an output with an ICC profile, `get_preferred` (`src/protocols/ColorManagement.cpp`) returned the monitor's ICC image description as *ready*. The ICC description keeps the default `GAMMA22` transfer function and ICC-derived primaries; the renderer has no ICC *source* transform (the LUT is only applied when the ICC description is the *target*). A client that encodes per that profile and sets the description on its surface (which must be accepted, `color-management-v1.xml:747-748`) gets its pixels decoded as gamma 2.2 — e.g. a linear-TRC profile's 0.5 becomes 0.218. Hyprland doesn't support ICC surface descriptions at all (the ICC creator errors with `unsupported_feature`), so this was the only path.

`get_preferred_parametric` already substituted sRGB for ICC outputs.

Fork fix `57dc260a2`: `get_preferred`, `get_preferred_parametric` and `preferred_changed` all go through one `preferredForSurfaces()` substitute (sRGB for ICC outputs), keeping the preferred identity consistent. Residual: `wp_color_management_output_v1.get_image_description` still (correctly) describes the output with its ICC description; a client that applies that one to a surface still hits the missing ICC source transform.

Upstream `12f242511`: same code, not fixed.
