# HDR-LIM-01 Capture targets remain sRGB without HDR / wide-gamut negotiation

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: historical evidence at the revisions below; current-version and physical-display acceptance must be established separately.

## Agent report

Official baseline: `e2ad066c9`; fork comparison: `3d52e94fd`.

[#16412](https://github.com/hyprwm/Hyprland/discussions/16412) and [#16542](https://github.com/hyprwm/Hyprland/discussions/16542) identify a real implementation limitation. [`ScreenshareFrame.cpp:414,456,508`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/managers/screenshare/ScreenshareFrame.cpp#L400-L456) explicitly tags capture framebuffers as sRGB. [`OpenGL.cpp:1462–1475`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/OpenGL.cpp#L1462-L1475) gives that explicit target priority, so `cmBackToSRGB=false` does not make the target HDR. The local fork retains these capture-target assignments.

This extends old #21's mirror-path observation, but **does not prove a wlr-screencopy protocol violation**. Merely binding `wp_color_manager_v1` does not negotiate an HDR capture encoding; a client cannot assume captured bytes use the output's description. Explicit capture-color negotiation and HDR/wide-gamut preservation are missing functionality. Empty/stale capture frames in #14931 are a different problem.

[Full validation, exclusions, baselines and limitations](_evidence/community-hdr-validation.md).
