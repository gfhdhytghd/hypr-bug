# Hyprland HDR / Color Management — raw audit list (pending evaluation)

- Code version: branch `cm/tonemap-ref-scaling` (HEAD `b2586b63b`, based on main `6cc0822e2`)
- Spec: wayland-protocols 1.49, `staging/color-management/color-management-v1.xml` (wp_color_manager_v1 v3)
- Totals: 29 substantive findings (protocol 12, shader 6, render pipeline 8, known 3), plus ~10 minor items / interpretation disagreements

> This is the original agent-generated audit list, kept as-is for later
> triage. Per-finding verification verdicts against the current tip
> (`30e438818`) — including three partial-true corrections and line-number
> fixes — are in [`../reports/color-management-verification.md`](../reports/color-management-verification.md);
> the two crash deep-dives live with their clients under `repro/`.

Verification levels used below:
- **live-tested**: actually run in a nested session and observed
- **verified**: source and spec text personally re-read and confirmed
- **not re-checked**: subagent conclusion, not independently reviewed

## I. Protocol layer (12)

| # | Issue | Location | Severity | Verification |
|---|-------|----------|----------|--------------|
| 1 | feedback object never becomes inert after its surface is destroyed; the next output-description change makes `onPreferredChanged` access the dead surface → compositor SIGSEGV. Spec: feedback "becomes inert" after surface destruction | `src/protocols/ColorManagement.cpp:460`, `:901` | High | live-tested (SIGSEGV, top of stack `onPreferredChanged` ← `onMonitorImageDescriptionChanged`) |
| 2 | An image description created while the output is invalid sends failed, yet `get_information` / `set_image_description` remain allowed; empty `m_settings` → SIGSEGV. Spec: a not-ready object is "illegal to use", must raise a protocol error | `:269-270`, `:767`, `:316-324` | High | live-tested (SIGSEGV, top of stack `CColorManagementImageDescriptionInfo` ctor) |
| 3 | `set_image_description` / `unset` apply immediately instead of as double-buffered state at commit; one-frame flash when switching SDR/HDR | `:312-330` | Medium | verified |
| 4 | PQ `luminances.max` does not satisfy "min + 10000": output and preferred description advertise (0, peak, 203); client-provided max_lum stored instead of ignored | `src/output/Monitor.cpp:604-609`, `ColorManagement.cpp:685`, `src/Compositor.cpp:1003-1005` | Medium | verified (live capture showed preferred `luminances(0, 630, 203)`) |
| 5 | Windows BT.2100 / scRGB presets only set reference; max inherits 80 and scRGB min is 0.2; passthrough infoframe mastering max becomes 80 | `src/helpers/cm/ColorManagement.hpp:427-443`, `Renderer.cpp:2512` | Medium | not re-checked |
| 6 | `create` only checks that something was set; incomplete parameter sets not rejected with incomplete_set | `:550` | Low | not re-checked |
| 7 | No max_fall ≤ max_cll check; v1 also does not check they fall inside the mastering range | `:546-584` | Low | not re-checked |
| 8 | `set_mastering_luminance` validation has a hole: (0,0) and (min>0, 0) accepted | `:714` | Low | not re-checked |
| 9 | `set_tf_named` accepts transfer functions not advertised at the current version; should raise invalid_tf | `:592-607` | Low | not re-checked |
| 10 | `set_tf_power` posts a wp_color_manager_v1 error code whose value does not correspond to the creator interface's unsupported_feature | `:620` | Low | not re-checked |
| 11 | Inert wp_color_management_surface_v1 never gets an inert error | `:303`, `:326` | Low | not re-checked |
| 12 | ICC outputs replacing their description emit no image_description_changed / preferred_changed | `Monitor.cpp:653-663` | Low | not re-checked |

## II. Shaders & transfer functions (6)

| # | Issue | Location | Severity | Verification |
|---|-------|----------|----------|--------------|
| 13 | "bt1886" actually implements the inverse BT.709 OETF (1/0.45 power + 4.5 linear segment), not the BT.1886 EOTF (power 2.4); shadows lifted | `src/render/shaders/glsl/constants.h:25-28`, `cm_helpers.glsl:71`; CPU side `ColorManagement.cpp:222-225` | High | verified (spec: "Rec. ITU-R BT.1886 is the display transfer characteristic") |
| 14 | HLG has no OOTF (system gamma); mapped as scene-linear to 1000 nit. Spec requires "the HLG OOTF" | `cm_helpers.glsl` HLG inverse + `toNit` | High | spec requirement verified; the number (75% white ≈ 265 nit) not re-checked |
| 15 | Tonemap variant clamps colors to ≥0; the FP16 working buffer is BT.709-gamut ext_linear, out-of-gamut colors live as negatives and get truncated — wide gamut lost | `tonemap.glsl:46`, `:80` | High | source verified; visuals not live-tested |
| 16 | generic film primaries misordered (red = 0.243/0.692 is actually green), contradicting H.273 | `ColorManagement.hpp:146-151` | Medium | verified |
| 17 | CIE 1931 XYZ primaries have y=0; `xy2xyz` divides by zero, conversion matrix becomes NaN | `ColorManagement.hpp:160-165` | Medium | subagent tested against libhyprgraphics; not re-checked |
| 18 | Client set_luminances min/max do not participate in pixel mapping; srcTFRange only takes transfer-function defaults | `src/render/Renderer.cpp:2165-2168` | Medium | not re-checked |

## III. Render pipeline (8)

| # | Issue | Location | Severity | Verification |
|---|-------|----------|----------|--------------|
| 19 | HLG surfaces are allowed to passthrough, but `createHDRMetadata` returns no HDR metadata for HLG (TODO); display may drop out of HDR and flip-flop. | `Monitor.cpp:2016-2025`, `Renderer.cpp:2503-2505`, `:2576-2587` | High | source verified; switching not live-tested |
| 20 | Forced HDR + no EDID luminance + no max_luminance set → `maxLuminance()` defaults to 80; output peak advertised as 80 nit | `Monitor.cpp:604-608`, `Monitor.hpp:332` | Medium | verified |
| 21 | HDR→HDR mirroring passes through an sRGB intermediate texture; highlights clipped at SDR white | `src/output/MonitorResources.cpp:151-157` | Medium | verified |
| 22 | HDR passthrough never checks that surface and output descriptions match (different-gamut PQ content comes out oversaturated) | `Monitor.cpp:2022-2025` | Medium | not re-checked |
| 23 | Infoframe uses container gamut instead of mastering gamut; when client MaxCLL/MaxFALL are unknown (0), monitor EDID values are substituted | `Renderer.cpp:2511`, `:2535-2536` | Low | not re-checked |
| 24 | HDR→SDR peak depends on whether FP16 is enabled; screenshot/capture brightness changes with use_fp16 | `Renderer.cpp:2148-2150` | Low | not re-checked |
| 25 | FP16 blur skips sdr_brightness / sdr_saturation | `src/render/gl/blur/Kawase.cpp:133-142`, `:253-262` | Low | not re-checked |
| 26 | Exiting auto-HDR fullscreen commits the HDR switch one frame late | `Renderer.cpp:2703` | Low | not re-checked |

## IV. Known issues (3, previously live-tested)

| # | Issue | Location |
|---|-------|----------|
| 27 | The tonemap peak handed to the shader is scaled by dstRef/srcRef while pixels (absolute nit after toNit) are not; PQ clients with reference white ≠ 203 get highlights hard-clipped (the issue fixed by this PR; weston-color `-r 446` measured: 700/1000 nit both clipped to display peak) | `Renderer.cpp` getCMSettings, `tonemap.glsl:45` |
| 28 | Pixels are not anchored to reference white, violating the anchoring requirement of set_luminances (KWin implements this in `ColorDescription::toOther`) | `cm_helpers.glsl:245` |
| 29 | Output reference white hardcoded to 203 (`HDR_REF_LUMINANCE`), inconsistent with sdr_max_luminance used for SDR content mapping (upstream issue #16329) | `ColorManagement.hpp:21`, `Monitor.cpp:582`/`:596` |

## V. Minor items & interpretation disagreements

- tonemap operates on premultiplied-alpha colors; translucent HDR pixels under-compressed (`cm_helpers.glsl:247-249`)
- SDR brightness/saturation applied after encoding; multiplier distorted on non-FP16 PQ targets (`cm_helpers.glsl:259-260`)
- tonemap uses ICtCp I instead of luminance Y and does not scale chroma with I (`tonemap.glsl`)
- mirror outputs always use piecewise sRGB encoding (`cm_helpers.glsl:254-255`)
- ext_linear default luminances 0/80/203 are self-invented; spec gives no TF-implied values — should follow sRGB defaults
- preferred-changed events: `get_preferred` never updates `m_currentPreferredId` → repeats; multi-output case unchecked
- unset max_cll / max_fall reported as 0 in info
- on ICC outputs `get_preferred_parametric` returns the ICC description; info never sends icc_file
- no wl_output.done guaranteed after image_description_changed
- debug-only (full_cm_proto): non-perceptual intent advertised yet rejected; ICC creator missing already_set/bad_fd checks and leaks fd; unknown named primaries accepted

## VI. Suggested triage order

1. #1, #2: tiny fixes (`m_surface.expired()` guard; protocol errors for post-failed requests), unrelated to color semantics
2. #13, #16, #17: plain formula/constant errors
3. #4, #14, #15, #27–#29: semantic level — worth discussing together with pixel anchoring and #16329
