# HDR-COM-03 SDR brightness / saturation applied after PQ encoding

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: historical evidence at the revisions below; current-version and physical-display acceptance must be established separately.

## Agent report

Official baseline: `e2ad066c9`; fork comparison: `3d52e94fd`.

**Community:** [#16220](https://github.com/hyprwm/Hyprland/issues/16220). Its Issue was automatically closed because reports must use Discussions; that is not evidence of a fix.

**Evidence:** [`cm_helpers.glsl:257–260`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/shaders/glsl/cm_helpers.glsl#L257-L260) calls `fromLinearNit` before saturation/brightness. Consequently the multiplier acts on PQ code values when the target is PQ. [`Monitor.cpp:2692–2709`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/output/Monitor.cpp#L2692-L2709) shows the reachability distinction: without FP16 or ICC, the work buffer can use the PQ output description. The default FP16 intermediate can avoid this particular PQ-target case; it must not be described as affecting every HDR configuration.

**Executed counterexample:** 80-nit neutral white encodes to PQ **0.485857099**. Multiplying the encoded value by 1.5 produces **0.728785634**, which the upstream PQ inverse decodes to **809.320496 nit**, rather than the **120 nit** produced by a 1.5 multiplier in linear light. This confirms the nonlinear behavior described by the issue; actual panel luminance remains unmeasured. The saturation-order concern is source-confirmed, but this run does not claim a numeric reproduction of the issue author's specific purple text example.

**Deduplication:** the raw original audit already lists post-encoding SDR adjustment as a minor item. This expands its evidence rather than counting a new independent defect.

**Local status:** `src/render/shaders/glsl/cm_helpers.glsl:353–366` moves SDR adjustments before tonemapping/output encoding. The fork's reference-white anchoring can change absolute expected white levels, so the upstream numbers above must not be reused as a fork regression oracle.

[Full validation, exclusions, baselines and limitations](_evidence/community-hdr-validation.md).

[Shared numerical reproduction and recorded evidence](_assets/hdr-community-numerics/). This is an isolated shader calculation, not physical HDR validation.
