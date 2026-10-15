# HDR-COM-02 — Parametric EXT_LINEAR encoding range

This finding is folded into [CM-18](CM-18.md), not counted as an additional tracker issue.

- Agent confirmation: `confirmed` for this narrow subcase at official `e2ad066c9`.
- Human confirmation: `none`.
- Fork `3d52e94fd` contains corrective source; no new integration or physical-display acceptance is claimed.

**Community:** [#16498](https://github.com/hyprwm/Hyprland/discussions/16498), reporting a Godot parametric EXT_LINEAR surface. This is not a reason to change the 80-nit encoding of the protocol's explicit Windows-scRGB preset.

**Evidence:**

- [`set_luminances` stores the supplied values](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/protocols/ColorManagement.cpp#L674-L686).
- [`getTFMinLuminance` / `getTFMaxLuminance`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/helpers/cm/ColorManagement.hpp#L283-L318) instead return 0 and 80 for every EXT_LINEAR description, assuming Windows scRGB.
- [`getCMSettings`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/Renderer.cpp#L2145-L2153) obtains `srcTFRange` from these helpers; [`doColorManagement` / `toNit`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/shaders/glsl/cm_helpers.glsl#L158-L160) multiply linear signal values by that range.
- The protocol states that `set_luminances` overwrites the default luminances; only PQ has the specified maximum-luminance override. EXT_LINEAR's identity transfer function does not impose a universal Windows-scRGB scale.

**Executed counterexample:** with luminances `(0, 10000, 203)` and signal `0.0203`, the actual upstream GLSL helper returns **1.62400007 nit**, rather than **203 nit**. Supplying the declared 10000-nit range to the same helper returns **203.000015 nit**. A Windows-scRGB control with range 80 and signal 1.0 still returns **80 nit**.

**Deduplication:** old #18 was correctly downgraded to partially true because maximum/reference luminance can influence tonemapping. This result narrows the finding to the **encoding range of a parametric EXT_LINEAR surface**, rather than reinstating the overbroad original claim.

**Local status:** `src/helpers/cm/ColorManagement.cpp:324–333` uses the declared range for ordinary parametric EXT_LINEAR and reserves the 80-nit exception for `isWindows || isInternal`; `src/render/CMSettings.cpp:35` consumes that encoding range. This corrective source is present at the pinned fork. This run does not claim integrated Godot or physical HDR validation of that fix.

[Shared numerical reproduction and recorded evidence](_assets/hdr-community-numerics/).

[Complete validation context](_evidence/community-hdr-validation.md).
