# HDR-LIM-02 Incompatible direct scanout allowed by non_shader_cm=ignore

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: historical evidence at the revisions below; current-version and physical-display acceptance must be established separately.

## Agent report

Official baseline: `e2ad066c9`; fork comparison: `3d52e94fd`.

The behavior in [#16219](https://github.com/hyprwm/Hyprland/discussions/16219) is supported by source: [`Monitor.cpp:2016–2025`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/output/Monitor.cpp#L2016-L2025) skips the SDR/HDR compatibility rejection when `non_shader_cm=ignore`. With no conversion, a gamma-encoded SDR buffer can reach a PQ output. This is consistent with the explicit ignore policy, so it is recorded as a conditional correctness limitation, not a new unconditional regression.

[`ConfigValues.cpp:652–663`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/config/values/ConfigValues.cpp#L652-L663) defaults `non_shader_cm` to ignore but **direct_scanout to disabled**. A report must include the enabling setting. The local fork at `Monitor.cpp:2052–2059` rejects incompatible encodings even under ignore unless a hardware conversion exists. No actual scanout was exercised. This is related to old #22, whose narrower example was HDR-to-HDR primaries mismatch.

[Full validation, exclusions, baselines and limitations](_evidence/community-hdr-validation.md).
