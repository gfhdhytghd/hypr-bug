# Community HDR / color-management validation — 2026-10-15

## Scope and result

This report validates the preceding community search against pinned source. It adds **one new defect** (HDR-COM-01), expands **two existing findings** (HDR-COM-02/03), and records **two confirmed implementation limitations** separately. A community report, an open discussion, and a merged patch are not physical-display acceptance.

| Baseline | Revision | Verification |
|---|---|---|
| Official Hyprland `main` | `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` | API HEAD queried on 2026-10-15; source exported from the matching local Git object |
| Official Aquamarine `main` | `52ea11f06dd492f34b3cc0b39a9c805beecafcff` | API HEAD queried; matching source archive inspected |
| User's local Hyprland `main` | `3d52e94fd9f70e152fb5555dce8e5e675e87fb59` | Tracked source inspected read-only; unrelated untracked files preserved |
| Protocol reference | local wayland-protocols `color-management-v1.xml` | `set_luminances`, lines 1216–1254; EXT_LINEAR definition, lines 308–312 |

GitHub access was read-only. Only this bug-list repository was edited. No official Issue, Discussion, PR, source file, or branch was changed. No compositor reload, monitor configuration, KMS modeset, or Wayland crash client was used.

The search returned 392 unique candidates and retained 184 related/historical/configuration/feature records. [The per-record ledger](community-hdr-triage.json) preserves all 184 dispositions. Most remain community-only evidence and **are not admitted as confirmed bugs**. Source validation below concentrates on the mechanisms that can be independently established here; remaining hardware-specific cases are listed under pending validation.

## Confirmed defects

> Addendum (same day): HDR-COM-04 was added after this run, from PR #16525 / #13418, with source confirmation at the same baseline.

| ID | Finding | Official source | Local fork | Deduplication |
|---|---|---|---|---|
| HDR-COM-01 | CPU-copy hardware cursor bypasses output color conversion | Present; source-confirmed | Same path still present | **New** relative to the existing 29-item list |
| HDR-COM-02 | Parametric EXT_LINEAR ignores its declared luminance range | Present; source + executed shader counterexample | Corrective encoding-range implementation present; no new fork integration run | Subcase clarifying old #18 and the EXT_LINEAR minor item |
| HDR-COM-03 | SDR brightness is applied after PQ encoding | Present; source + executed shader counterexample | Adjustment moved to linear light; no new fork integration run | Expands the original post-encoding SDR-adjustment minor item |

### HDR-COM-01 — CPU-copy hardware cursor has no CM step

**Scope:** hardware cursors on a PQ output when `cursor:use_cpu_buffer=1`, or automatic CPU-buffer selection on NVIDIA. Software cursors and the GPU-rendered hardware-cursor path are not the finding.

**Community:** [Hyprland #14419](https://github.com/hyprwm/Hyprland/discussions/14419), [#14028](https://github.com/hyprwm/Hyprland/discussions/14028), [hyprcursor #95](https://github.com/hyprwm/hyprcursor/issues/95). The ordinary hardware-cursor fix [PR #15905](https://github.com/hyprwm/Hyprland/pull/15905) was merged, but does not cover the CPU-copy branch. The remaining branch is also unchecked in [the official tracker](https://github.com/hyprwm/Hyprland/issues/9064).

**Closed source chain:**

1. [PointerManager.cpp:549–551](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/pointer/PointerManager.cpp#L549-L551) selects CPU buffers explicitly, or automatically for NVIDIA.
2. [Lines 617–693](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/pointer/PointerManager.cpp#L617-L693) obtains cursor bytes, applies a Cairo geometric resampling, copies the ARGB bytes into the scanout buffer, and returns. There is no source/output image-description conversion in this branch.
3. [Lines 702–726](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/pointer/PointerManager.cpp#L702-L726) tags the framebuffer with the output image description and renders the cursor through the color-managed renderer, but runs only after the CPU branch has returned. Its comment explicitly identifies raw sRGB cursor data being interpreted in a PQ output as the problem.

**Numeric implication, not a panel measurement:** opaque sRGB white copied unchanged remains code 1.0. If that plane is interpreted in the output PQ encoding, code 1.0 denotes 10,000 cd/m²; 80 cd/m² would encode to approximately 0.485857. The panel/driver's realized brightness and blending are unmeasured, so no measured nit value or burn-in claim is made.

**Local status:** the pinned fork has the same selection, early return, and later framebuffer-tagging lines at `src/pointer/PointerManager.cpp:549–726`. Its other HDR fixes do not cover this branch. No live cursor-plane test was performed.

**Fix direction:** color-convert the CPU buffer to the output encoding, or use a correctly managed fallback when that conversion cannot be provided. Preserve the distinction between CPU and GPU buffer paths when testing a future fix.

### HDR-COM-02 — explicit EXT_LINEAR luminances are discarded during decoding

**Community:** [#16498](https://github.com/hyprwm/Hyprland/discussions/16498), reporting a Godot parametric EXT_LINEAR surface. This is not a reason to change the 80-nit encoding of the protocol's explicit Windows-scRGB preset.

**Evidence:**

- [`set_luminances` stores the supplied values](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/protocols/ColorManagement.cpp#L674-L686).
- [`getTFMinLuminance` / `getTFMaxLuminance`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/helpers/cm/ColorManagement.hpp#L283-L318) instead return 0 and 80 for every EXT_LINEAR description, assuming Windows scRGB.
- [`getCMSettings`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/Renderer.cpp#L2145-L2153) obtains `srcTFRange` from these helpers; [`doColorManagement` / `toNit`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/shaders/glsl/cm_helpers.glsl#L158-L160) multiply linear signal values by that range.
- The protocol states that `set_luminances` overwrites the default luminances; only PQ has the specified maximum-luminance override. EXT_LINEAR's identity transfer function does not impose a universal Windows-scRGB scale.

**Executed counterexample:** with luminances `(0, 10000, 203)` and signal `0.0203`, the actual upstream GLSL helper returns **1.62400007 nit**, rather than **203 nit**. Supplying the declared 10000-nit range to the same helper returns **203.000015 nit**. A Windows-scRGB control with range 80 and signal 1.0 still returns **80 nit**.

**Deduplication:** old #18 was correctly downgraded to partially true because maximum/reference luminance can influence tonemapping. This result narrows the finding to the **encoding range of a parametric EXT_LINEAR surface**, rather than reinstating the overbroad original claim.

**Local status:** `src/helpers/cm/ColorManagement.cpp:324–333` uses the declared range for ordinary parametric EXT_LINEAR and reserves the 80-nit exception for `isWindows || isInternal`; `src/render/CMSettings.cpp:35` consumes that encoding range. This corrective source is present at the pinned fork. This run does not claim integrated Godot or physical HDR validation of that fix.

### HDR-COM-03 — post-PQ SDR brightness has a nonlinear physical effect

**Community:** [#16220](https://github.com/hyprwm/Hyprland/issues/16220). Its Issue was automatically closed because reports must use Discussions; that is not evidence of a fix.

**Evidence:** [`cm_helpers.glsl:257–260`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/shaders/glsl/cm_helpers.glsl#L257-L260) calls `fromLinearNit` before saturation/brightness. Consequently the multiplier acts on PQ code values when the target is PQ. [`Monitor.cpp:2692–2709`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/output/Monitor.cpp#L2692-L2709) shows the reachability distinction: without FP16 or ICC, the work buffer can use the PQ output description. The default FP16 intermediate can avoid this particular PQ-target case; it must not be described as affecting every HDR configuration.

**Executed counterexample:** 80-nit neutral white encodes to PQ **0.485857099**. Multiplying the encoded value by 1.5 produces **0.728785634**, which the upstream PQ inverse decodes to **809.320496 nit**, rather than the **120 nit** produced by a 1.5 multiplier in linear light. This confirms the nonlinear behavior described by the issue; actual panel luminance remains unmeasured. The saturation-order concern is source-confirmed, but this run does not claim a numeric reproduction of the issue author's specific purple text example.

**Deduplication:** the raw original audit already lists post-encoding SDR adjustment as a minor item. This expands its evidence rather than counting a new independent defect.

**Local status:** `src/render/shaders/glsl/cm_helpers.glsl:353–366` moves SDR adjustments before tonemapping/output encoding. The fork's reference-white anchoring can change absolute expected white levels, so the upstream numbers above must not be reused as a fork regression oracle.

### HDR-COM-04 — SDR presets advertise the panel's HDR headroom

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

## Confirmed limitations — not additional defect claims

### HDR-LIM-01 — capture targets remain sRGB

[#16412](https://github.com/hyprwm/Hyprland/discussions/16412) and [#16542](https://github.com/hyprwm/Hyprland/discussions/16542) identify a real implementation limitation. [`ScreenshareFrame.cpp:414,456,508`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/managers/screenshare/ScreenshareFrame.cpp#L400-L456) explicitly tags capture framebuffers as sRGB. [`OpenGL.cpp:1462–1475`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/render/OpenGL.cpp#L1462-L1475) gives that explicit target priority, so `cmBackToSRGB=false` does not make the target HDR. The local fork retains these capture-target assignments.

This extends old #21's mirror-path observation, but **does not prove a wlr-screencopy protocol violation**. Merely binding `wp_color_manager_v1` does not negotiate an HDR capture encoding; a client cannot assume captured bytes use the output's description. Explicit capture-color negotiation and HDR/wide-gamut preservation are missing functionality. Empty/stale capture frames in #14931 are a different problem.

### HDR-LIM-02 — `non_shader_cm=ignore` allows incompatible direct scanout upstream

The behavior in [#16219](https://github.com/hyprwm/Hyprland/discussions/16219) is supported by source: [`Monitor.cpp:2016–2025`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/output/Monitor.cpp#L2016-L2025) skips the SDR/HDR compatibility rejection when `non_shader_cm=ignore`. With no conversion, a gamma-encoded SDR buffer can reach a PQ output. This is consistent with the explicit ignore policy, so it is recorded as a conditional correctness limitation, not a new unconditional regression.

[`ConfigValues.cpp:652–663`](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/config/values/ConfigValues.cpp#L652-L663) defaults `non_shader_cm` to ignore but **direct_scanout to disabled**. A report must include the enabling setting. The local fork at `Monitor.cpp:2052–2059` rejects incompatible encodings even under ignore unless a hardware conversion exists. No actual scanout was exercised. This is related to old #22, whose narrower example was HDR-to-HDR primaries mismatch.

## Rejected, corrected, or deferred community claims

| Community record / family | Decision | Evidence or missing check |
|---|---|---|
| #16320 — HDR fade snapshots lose range | Do not add as a current defect | Current `Renderer.cpp:3307–3308` chooses FP16; window/layer/popup call sites use it. PR #16321 merged. |
| #16244 — initial SDR retains HDR metadata | Do not add as a current defect | Current `Renderer.cpp:2591–2600` explicitly handles `INITIAL_SDR` and clears metadata. A closed proposal was not enough evidence; the current code resolves that uncertainty. |
| Aquamarine #371 / Hyprland #15773 — parser only walks top-level CTA | Old implementation claim obsolete; specific panel still unvalidated | Current AQ `DRM.cpp:1956–1970` uses high-level `di_info_get_hdr_static_metadata` and signal-colorimetry APIs after #270. A failing EDID plus exact libdisplay-info build is needed before claiming a current nested-block failure. |
| #16420 / #16097 — Firefox YUV red/orange | Pending, not admitted | The YUV-specific texture branch is commented out, but EGL drivers can perform YUV conversion during supported imports. `OpenGL.cpp:504–519` filters external-only modifiers; implicit modifiers are separately exposed. These gates and the actually imported format/driver need a controlled end-to-end case. The commented branch alone does not prove the reported red tint's cause. |
| #14752 — rotated SDR secondary blur breaks beside HDR primary | Pending, not admitted | Old-version screenshots and a setting workaround do not establish the current transform/damage path. Requires the reported mixed-orientation layout and a current render capture. Old #25 remains only a related item. |
| #13784 — laptop brightness control in HDR | Pending, not admitted | A monitor/driver/platform-specific backlight versus HDR luminance control problem; this host is not the reported internal laptop panel. No live brightness changes made. |
| #13153 / #13036 / #14491 / AQ #319/#424 — resume or GPU switching leaves output state wrong | Pending, not admitted | Need synchronized pre/post DRM state and driver/backend logs on affected hardware. No suspend, mode switch, or physical output state test was performed. |
| #16180 — Wine scRGB alpha / force_rgbx | Pending, not admitted | Needs actual buffer alpha, image description, and comparison with the producing Wine build. Source presence of an alpha override does not prove a compositor bug. |
| #15671 — ICC/VCGT changes after DPMS/reboot | Pending, not admitted | Needs before/after ICC description and gamma-LUT state; no DPMS or reboot performed. |
| #15781 — DOOM red/blue channels | Workaround reported, current root cause unvalidated | Format-selection and direct-scanout conditions need to be isolated. No channel-swap bug added from an answered post alone. |
| #16237 — compositor crash when Firefox closes | Pending, not merged with old #1 | Developer could not reproduce. Actual crash stack and object lifetime must match before treating it as feedback-inert. No new crash client executed. |
| #14931 — empty/stale HDR screenshots | Community reports resolution; not reintroduced as a current defect | Closed as resolved on 2026-10-02. Separate from capture encoding and old #24's brightness dependence. |
| #14504 — red shift | Withdrawn root cause | Author attributed it to hyprsunset. |
| #15195 — elevated SDR black proves a renderer bug | Withdrawn root cause | Author later identified the SDR black-floor setting. This does not invalidate separately established reference-white defects. |
| #15864 — HDR blur noise | Configuration/effect report | Author confirmed `blur.noise=0` addressed their case. No independent driver dithering defect established. |
| #13221 / #14631 | Historical resolution reported; #13221 superseded by HDR-COM-04 | #13221's symptom is admitted as [HDR-COM-04](#hdr-com-04--sdr-presets-advertise-the-panels-hdr-headroom) on source evidence (PR #16525 reports it still reproducing). #14631 is unchanged: authors report fixes; not newly admitted without a current counterexample. |
| #15747 — CTM precision and negative coefficients | Verified protocol/design limitation, not admitted as a new bug | `CTMControl.cpp:35–41` uses wl_fixed values and rejects negative coefficients. That establishes the restriction, not a violation of the existing interface contract. |
| #16541 / #16544 / #16329 | Existing records; cross-reference only | Correspond to old #27/#1/#29. First two are the user's own upstream reports and not independent community confirmation. |
| Remaining retained records | Community-only / historical evidence | Kept in the per-record ledger, excluded from the confirmed defect count. |

## Executed validation

[`repro/hdr-community-numerics/verify.py`](../repro/hdr-community-numerics/verify.py) compiles the **actual pinned upstream GLSL helpers** into a standalone surfaceless GLES compute shader, with independent PQ arithmetic checks and EXT_LINEAR/Windows-scRGB controls. The supplied ranges isolate helper behavior; this does not instantiate `getCMSettings`, a Wayland client, a capture session, or the compositor.

Observed renderer: NVIDIA RTX PRO 6000 Blackwell Workstation Edition, OpenGL ES 3.2, driver 615.71.09. This was an isolated EGL computation, **not a software-renderer run and not a physical HDR display measurement**. No output/window was created. [Recorded values and source hashes](../repro/hdr-community-numerics/evidence.json) accompany the reproducer.

No new actual-Godot, Firefox/YUV, cursor-plane, multi-monitor blur, suspend/resume, screenshot, scanout, or local-fork integration acceptance is claimed.
