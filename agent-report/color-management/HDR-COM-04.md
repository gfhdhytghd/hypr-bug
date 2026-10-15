# HDR-COM-04 SDR presets advertise the panel's EDID HDR luminances as target headroom

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no explicit human validation record is supplied.

## Agent report

**Community:**
- [#13418](https://github.com/hyprwm/Hyprland/discussions/13418): Chromium/Electron white rendered at 187/255 next to GTK's 255 with `cm = edid` or `auto` on a 10-bit output.
- [#13221](https://github.com/hyprwm/Hyprland/discussions/13221): the same symptom, closed as fixed upstream.
- [PR #16525](https://github.com/hyprwm/Hyprland/pull/16525) (open, unmerged at the time of writing) states that #13221 still reproduces on main.

**Evidence:**
- [`applyCMType`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/output/Monitor.cpp#L508-L516) reads the EDID mastering luminances, maxCLL and maxFALL once. It attaches them to every preset except sRGB: [`CM_WIDE` … `CM_EDID`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/output/Monitor.cpp#L524-L573), all of which use the SDR transfer function.
- [`CColorManagementImageDescriptionInfo`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/protocols/ColorManagement.cpp#L851-L858) sends `target_luminance` from the mastering luminances whenever their maximum is set. It also sends `target_max_cll` / `target_max_fall`.
- The resulting SDR output description advertises, for example, `luminances(0.2, 80, 80)` alongside `target_luminance(0.001, 1060)`. Chromium reads that as 13.25× headroom above SDR white and dims its SDR content to leave room for highlights. A gamma-2.2 output cannot encode anything above its reference white, and the compositor does not dim other content to match. The net effect is only darker Chromium/Electron content.

**Side effect:** that false headroom is also what makes Chromium request HDR and trip `cm_auto_hdr` on SDR presets.

**Scope:**
- **Upstream:** the effect is client-visible only. `getCMSettings` takes its destination peak from `luminances.max` ([`Renderer.cpp:2134`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/Renderer.cpp#L2134)), so upstream's own tonemapping is not affected.
- **Infoframes:** the SDR path commits `NO_HDR_METADATA`, so nothing changes in what is sent to the display.

**Verification:** source-confirmed at `e2ad066c9`. No Chromium measurement was repeated here; the 187/255 figure and the 1060-nit EDID are the PR author's.

**Local status:** fixed in fork `06304f5d7`, which only attaches the luminances for `CM_HDR` / `CM_HDR_EDID`, matching PR #16525. In the fork the defect was worse than upstream: the fork's CM takes the target peak from the mastering luminances when they are set (`src/render/CMSettings.cpp:23`, `Monitor.cpp` scanout compatibility, hardware CM). On an SDR preset it therefore tonemapped HDR content for the panel's HDR peak instead of the SDR output.

[Shared validation context](_evidence/community-hdr-validation.md).

## Tracker notes

PR #16525 is by a third party (open). Fork fixed in `06304f5d7`.
