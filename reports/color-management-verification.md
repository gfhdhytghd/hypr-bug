# Hyprland HDR / Color Management — Crash Deep-Dives & Full Audit Verification

- Verification baseline: branch `cm/tonemap-ref-scaling`, worktree `/home/wilf/data/Hyprland-cm-tonemap`, tip `30e438818` ("renderer: fix on HDR render bug", based on main `6cc0822e2`)
- Spec: `/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml` (wp_color_manager_v1 v3)
- Note: the HEAD `b2586b63b` recorded in the audit doc (hyprland-hdr-cm-audit.md) does not exist in the local repo (the branch appears to have been rebased after the audit). All line numbers in this report were re-checked against the current tip.
- All verification was read-only; no repository files were modified.

## Full verification table (29 findings)

| # | Original status | Verdict | Notes (line corrections / fixes) |
|---|---|---|---|
| 1 | Live-tested | Confirmed | Lines 460/901 accurate; see Report 1 below |
| 2 | Live-tested | Confirmed | Lines 269-270/767/316-324 accurate; see Report 2 below |
| 3 | Verified | Confirmed | Handler starts at :303 |
| 4 | Verified | Confirmed | All lines accurate; units themselves are fine — the bug is failing to enforce the PQ rule min+10000 |
| 5 | Not re-checked | Confirmed | Line correction: hpp:414-431 (defaults :239-242), Renderer.cpp:2511-2515 |
| 6 | Not re-checked | Confirmed | :550 carries its own FIXME "actually check completeness" |
| 7 | Not re-checked | Confirmed | Repo-wide default protocol version is 1; extra finding: already_set checks entirely commented out (:710-736) |
| 8 | Not re-checked | Confirmed | :714 accurate |
| 9 | Not re-checked | Confirmed | :592-607 accurate; v1 accepts compound(14), v≥2 accepts srgb/ext_srgb, neither is rejected |
| 10 | Not re-checked | Confirmed | :620 accurate; 0=incomplete_set, while creator's unsupported_feature=2 |
| 11 | Not re-checked | Confirmed | The file never posts the SURFACE inert error |
| 12 | Not re-checked | Confirmed | Line correction: Monitor.cpp:652-664; the only notification point is applyCMType :612-613 |
| 13 | Verified | Confirmed | CPU-side file correction: src/helpers/cm/ColorManagement.cpp:222-225 + 300-302 (not protocols/); at V=0.05 shadows are ~15× too bright |
| 14 | High (values not re-checked) | Confirmed | grep -i ootf across the repo: zero hits; hand-computed 75% white = 265.0 nit, matching the doc's "≈265" exactly (with a correct BT.2100 OOTF γ=1.2 it should be ≈153 nit) |
| 15 | Verified | Confirmed | Path correction: src/render/shaders/glsl/tonemap.glsl (no pass/ subdir), lines 46/80 correct; FP16 buffer BT.709 evidence chain closed (Monitor.cpp:2692-2710 → hpp:432-438) |
| 16 | Verified | Confirmed | Table is byte-identical to KWin's pre-fix table (commit fad8a10a): H.273 lists primaries in G,B,R order and nobody reordered them; correct order R(0.681,0.319) G(0.243,0.692) B(0.145,0.049); white point was already correct |
| 17 | Not re-checked | Confirmed (mechanism corrected) | xy2xyz(0,0) does not divide by zero (both the library and the GLSL guard y==0); NaN actually comes from the singular-matrix inversion / white-point normalization in toXYZ()/convertMatrix; reachable path: protocols:644 set_primaries_named does not reject CIE1931_XYZ → all-NaN uniform |
| 18 | Not re-checked | **Partially true** | max/reference do participate in tonemapping (Renderer.cpp:2147-2149, :2168); only min never does. Line correction: srcTFRange is at 2164-2165 |
| 19 | Source-verified | Confirmed | HLG passthrough + empty metadata + flip-flopping code path confirmed (Renderer.cpp:2503 TODO, :2576-2587, :2599-2617) |
| 20 | Verified | Confirmed | maxLuminance(int defaultValue=80) implemented at Monitor.cpp:2364-2366; the guard condition is always true |
| 21 | Verified | Confirmed | Extra: mirror texture format is also hardcoded to fixed-point XRGB8888/XRGB2101010 (:140/:167), no FP16/EDR; consumer at Renderer.cpp:2212 |
| 22 | Not re-checked | Confirmed | DS eligibility is only two booleans, surfaceIsHDR and inHDR(); with CM_NS_IGNORE there is not even CTM compensation |
| 23 | Not re-checked | Confirmed | Line correction: Renderer.cpp:2510, :2534-2535; extra finding: the maxCLL() implementation prefers m_maxLuminance |
| 24 | Not re-checked | **Partially true** | The FP16 dependency only holds for work-buffer/mirror capture paths (tonemapShaderMaxLuminance linear→sRGB 630→248, TonemapPeak.cpp:44-49); the direct surface re-render screenshot path (ScreenshareFrame.cpp:414/456) keeps the client PQ description and takes the same PQ branch in both modes — no guaranteed difference. Lines: 2147-2149 |
| 25 | Not re-checked | Confirmed | The SDR uniforms only receive real values when workBufferImageDescription TF==ST2084_PQ; under FP16 the TF is EXT_LINEAR → always 1.0, no compensation point |
| 26 | Not re-checked | **Partially true** | "Committed one frame late" is not strictly accurate: the state change lands in the same commit — the real defect is content lagging one frame / mode-content mismatch for one frame; and if no new damage arrives after exiting fullscreen, the damage gate (:2280-2283) can delay the switch indefinitely. Line correction: :2702 |
| 27 | Previously live-tested | Confirmed; fix is correct | tonemapShaderMaxLuminance (:2091-2104) eliminates the unit mismatch; all 9 unit tests actually run and pass. Gaps: does not cover the getCMSettings call-site needsTonemap/tonemapMode==2 three-way selection, the linear-target headroom boundary (dstMax slightly above 203), dstMax default 10000, and the shader-side absolute-nit invariant has no integration test |
| 28 | — | Confirmed | Still no reference-white anchoring at the tip; the srcRefLuminance parameter of tonemap is never referenced inside the function body (dead parameter, worth cleaning up) |
| 29 | — | Confirmed | All three lines accurate (hpp:21, Monitor.cpp:582/596); no monitor rule can configure the output reference white; sdr_max_luminance defaults to 80 (MonitorRule.hpp:61) and is not linked to the 203 |

Secondary-item spot checks: a (tonemap applied after premultiplication) true; b (unset max_cll/fall reported as 0) **partially true** — when both are unset nothing is sent at all; when one is set, the other is sent as 0. The actual send site is the info constructor ColorManagement.cpp:856-858 (the "sendInformationEvent" named in the doc does not exist); c (ICC served via parametric, sendIccFile commented out :814-816) true; d (stale m_currentPreferredId causing repeated sends) true.

Additional findings (not in the original audit):
- already_set checks for `set_mastering_luminance` / `set_max_cll` / `set_max_fall` are commented out entirely (ColorManagement.cpp:710-736)
- the ICC branch of `get_preferred_parametric` only logs a FIXME instead of raising unsupported (:446-450)
- `shouldChangePreferredImageDescription()` always returns false (Compositor.cpp:1016-1019) + `onImagePreferredChanged(0)` hard-codes id 0 → the preferred-change notification mechanism is half-wired
- the ICtCpHLG matrix at tonemap.glsl:36-42 is commented out entirely; HLG ICtCp always uses the PQ matrix

---

