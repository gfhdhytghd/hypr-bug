# Hyprland HDR / Color Management — Crash Deep-Dives & Full Audit Verification

- Verification baseline: branch `cm/tonemap-ref-scaling`, worktree `/home/wilf/data/Hyprland-cm-tonemap`, tip `30e438818` ("renderer: fix on HDR render bug", based on main `6cc0822e2`)
- Spec: `/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml` (wp_color_manager_v1 v3)
- Note: the HEAD `b2586b63b` recorded in the audit doc (hyprland-hdr-cm-audit.md) does not exist in the local repo (the branch appears to have been rebased after the audit). All line numbers in this report were re-checked against the current tip.
- All verification was read-only; no repository files were modified.

## 0. Full verification table (29 findings)

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

## Report 1: feedback never becomes inert when its surface dies → null-deref SIGSEGV in onPreferredChanged

### 1. Summary and severity

`CColorManagementFeedbackSurface` holds a `WP<CWLSurfaceResource> m_surface` (src/protocols/ColorManagement.hpp:84). The spec requires the object to become inert once its associated wl_surface is destroyed, but the implementation never removes or inerts a feedback object when its surface dies: it stays in `PROTO::colorManagement->m_feedbackSurfaces`, and is only removed when the client explicitly destroys the feedback resource itself (src/protocols/ColorManagement.cpp:393-400 → destroyResource :921-923). Any later output-description change is broadcast to every feedback object, and `onPreferredChanged()` dereferences `m_surface` at :460 with no `expired()` guard; hyprutils `WeakPtr.hpp:221-226` makes `get()`/`operator->` return nullptr for an expired WP → SIGSEGV.

**Severity: Critical.** A single trivial client request sequence (create feedback → destroy your own surface) takes down the whole compositor on the next config/output change (remote DoS).

### 2. Object lifetimes involved

- **Created**: CColorManager `setGetSurfaceFeedback` handler (ColorManagement.cpp:115-135; emplaced into `m_feedbackSurfaces` at :125-126, `m_self` backfilled at :134); ctor :386-457 stores the WP `m_surface` (:387) and registers enter/leave listeners (:455-456).
- **Held by**: the protocol-singleton vector `m_feedbackSurfaces` (hpp:213), holding SPs.
- **Referred object CWLSurfaceResource**: created by `wl_compositor.create_surface`, held by `CWLCompositorProtocol::m_surfaces` (SP); client `wl_surface.destroy` → core/Compositor.cpp:94-95 → `CWLSurfaceResource::destroy()` (core/Compositor.cpp:306-317) → :315 destroyResource → :914-916 removed from m_surfaces → once the last SP drops, the object dies and the feedback's WP goes expired. **The feedback itself has no surface-death handling whatsoever.**
- **Corroborating evidence**: `get_preferred`(:405) / `get_preferred_parametric`(:428) both have `if (m_surface.expired())` → ERROR_INERT (:406/:429) — proving expiry is expected as a matter of course. Only `onPreferredChanged` is missing the same guard.

### 3. Trigger sequence and call chain

Client: bind wp_color_manager_v1 (render:cm_enabled=1, ProtocolManager.cpp:234-235) → create_surface S (never mapped, so no other SP holds it) → `get_surface_feedback(S)`=F → `wl_surface.destroy(S)` → roundtrip. From then on nothing ever touches F again.

Compositor side: any operation that changes the monitor image description (e.g. `hyprctl keyword monitor eDP-1,max_luminance 600`) → `CMonitor::applyMonitorRuleSoft` → `applyCMType` (Monitor.cpp:648) → :604-609 overwrites with minLuminance()/maxLuminance() → :610 `oldImageDescription != m_imageDescription` holds → :612-613 `PROTO::colorManagement->onMonitorImageDescriptionChanged(m_self)`.

Call chain:

```
onMonitorImageDescriptionChanged (ColorManagement.cpp:895)
  → :896-899 outputs receive image_description_changed (fine)
  → :901-902 unguarded iteration over ALL feedback objects
  → feedback->onPreferredChanged() (:902)
  → onPreferredChanged (:459)
  → :460 m_surface->m_enteredOutputs.size()
  → WP expired, operator-> returns nullptr → member read on null this → SIGSEGV
```

Note: after the surface dies, the enter/leave listeners die with its signals, so the :901 broadcast is the only — and inevitable — trigger entry point.

### 4. Crash-site code

ColorManagement.cpp:459-461:

```cpp
void CColorManagementFeedbackSurface::onPreferredChanged() {
    if (m_surface->m_enteredOutputs.size() == 1) {
        const auto newId = m_surface->getPreferredImageDescription()->id();
```

Dead pointer: `CColorManagementFeedbackSurface::m_surface`, type `WP<CWLSurfaceResource>` (protocols/ColorManagement.hpp:84). `WeakPtr.hpp:221-226`: `get() = (impl_ && impl_->dataNonNull()) ? m_data : nullptr`; `operator->` returns it directly.

### 5. Spec gap

color-management-v1.xml L795-799: *"If the wl_surface associated with this object is destroyed, the wp_color_management_surface_feedback_v1 object becomes inert."*; L826-827: *"If this protocol object is inert, the protocol error inert is raised."* (error enum inert=0, L809-810). The implementation has no object-level inert transition; only two requests have lazy expired() checks, and the internal broadcast path is entirely unguarded and crashes.

### 6. Fix proposal and reproduction

- **Minimal fix**: at the entry of `onPreferredChanged` (first line of the body at :459) add `if (m_surface.expired()) return;`
- **Recommended fix (spec semantics)**: next to the ctor's listener registrations (:455-456), also listen to `m_surface->m_events.destroy` (emitted by `CWLSurfaceResource::destroy()` at core/Compositor.cpp:313) and call `PROTO::colorManagement->destroyResource(this)` in the callback, so a feedback is removed the moment its surface dies; this also fixes the secondary issue at :890-892 where `onImagePreferredChanged` sends events to objects that should already be inert; keep the :405/:428 checks.
- **Reproduction** (~15-line wayland-client C program):
  1. bind wp_color_manager_v1
  2. `create_surface` S; `get_surface_feedback(S)`=F; roundtrip
  3. `wl_surface_destroy(S)`; roundtrip (F survives)
  4. in a terminal: `hyprctl keyword monitor <monitor>,max_luminance 600`
  5. hyprland main thread SIGSEGVs, backtrace `onMonitorImageDescriptionChanged → onPreferredChanged`, fault address ≈ 0x0 + offsetof(m_enteredOutputs)

---

## Report 2: a failed (not-ready) wp_image_description_v1 is still usable via get_information / set_image_description → empty m_settings WP dereferenced

### 1. Summary and severity

`CColorManagementOutput::get_image_description` correctly sends failed(NO_OUTPUT) when the output is gone (ColorManagement.cpp:269-270), but that failed object:

- (a) was already emplaced into `m_imageDescriptions` at :259-260, before success/failure is even decided, and is not removed on failure;
- (b) keeps a default-constructed empty `m_settings` WP (assigned only in the else branch at :272);
- (c) was created with `allowGetInformation=true` (passed true at :260);
- (d) `CColorManagementImageDescription` has no ready/failed state bit at all, and `WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY` is never posted anywhere in the file.

Consequences: after failed, `get_information` → `m_settings->value()` at :767 is a null deref on the main thread (crash); or the failed object is attached to a surface via `set_image_description` → :323-324 copies the empty WP and sets `hasImageDescription=true` → the render loop's `isHDR()`/`imageDescription()` (:378/:347, via Renderer.cpp:2571/2581, Monitor.cpp:2016-2017/2402) null-derefs on the render thread (whole machine hangs).

**Severity: Critical.** A single malicious or buggy client connection reliably crashes the compositor.

### 2. Object lifetimes involved

- **Created**: the setGetImageDescription lambda in the `CColorManagementOutput` ctor (:253-276): :256-257 destroys the previous description first; :259-260 emplaces into `CColorManagementProtocol::m_imageDescriptions` (protocols/ColorManagement.hpp:216, holding an SP; the constructor's second argument true = allowGetInformation); :268 backfills `m_self`; :269-270 the failed branch only calls sendFailed.
- **m_settings**: `NColorManagement::PImageDescription` (protocols/ColorManagement.hpp:166); `using PImageDescription = WP<const CImageDescription>` (src/helpers/cm/ColorManagement.hpp:383); default-constructed = expired.
- **Destroyed**: only via destroy/onDestroy (:757-758 → destroyResource :933-935); sendFailed neither removes nor cleans up.
- **Consumers**: the get_information lambda (:760-774, which only checks `m_allowGetInformation` at :762); the set_image_description find_if (:316-317, matching purely on `wl_resource*` identity, never checking ready); render/output-side users Renderer.cpp:2570-2581, Monitor.cpp:2016-2017, 2402.
- **Same-source contamination**: the reference-copy failure path in `CColorManager::get_image_description` (:219-223 sendFailed(UNSUPPORTED)) likewise leaves an object with an empty `m_settings`, still matchable by :316-317 for set_image_description (its allowGetInformation=false blocks get_information, but the set path has no defense at all).

### 3. Trigger sequence and call chain

**Path A (get_information — the most direct crash, needs no surface)**:

1. bind wp_color_manager_v1 + wl_output → get_output → cmout (`m_output` is a `WP<CWLOutputResource>`, hpp:41)
2. compositor removes the output: `hyprctl disable <id>` / unplug / mirror switch (ProtocolManager.cpp:120-123 retireOutputGlobal) → `m_output` expired, or its `m_monitor` WP dead
3. `get_image_description()` → :259-260 RESOURCE enters the list (allowGetInformation=true) → :269 condition true → :270 `sendFailed(CAUSE_NO_OUTPUT)`. Per spec the object may only be destroyed at this point
4. `get_information(id)` → :760 lambda → :762 passes → :767 `m_settings->value()`: empty WP, `operator->` returns nullptr → `CImageDescription::value()` called with this=nullptr → SIGSEGV

```
wl_event_loop dispatch(get_information)
  → setGetInformation lambda (ColorManagement.cpp:760)
  → :762 m_allowGetInformation passes
  → :767 m_settings->value() → null this → SIGSEGV
```

**Path B (set_image_description — crash deferred to the render loop)**:

1. Steps 1-3 of Path A yield failed object D
2. create_surface S → `get_surface(S)`=cmsurf → `cmsurf.set_image_description(D, perceptual)`: :306-310 user_data valid, passes; :311-314 intent legal, passes; :316-317 find_if hits D (failed, never cleaned up); :323 `setHasImageDescription(true)`; :324 `m_imageDescription = D's empty m_settings`
3. attach+commit; next frame after the window goes fullscreen: Renderer.cpp:2565 FULLSCREEN_WINDOW branch → :2567 findWithCM → :2570 `hasImageDescription()==true` → :2571 `isHDR()` → ColorManagement.cpp:377-378 `m_imageDescription->value().transferFunction` → empty-WP deref → SIGSEGV (render thread)
4. Without fullscreen: Monitor.cpp:2016-2017 (isHDR/isWindowsScRGB) and Monitor.cpp:2402 (workBufferImageDescription→imageDescription→:347) deref it just as nakedly

Why it gets there: the server places the ready/failed state entirely in the events it sends and records nothing itself; after failed, the object's identity, list membership, and request dispatch all continue as if nothing happened.

### 4. Crash-site code

Path A, ColorManagement.cpp:760-767 (excerpt):

```cpp
m_resource->setGetInformation([this](CWpImageDescriptionV1* r, uint32_t id) {
    if (!m_allowGetInformation) { r->error(WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION, ...); return; }
    auto RESOURCE = makeShared<CColorManagementImageDescriptionInfo>(
        makeShared<CWpImageDescriptionInfoV1>(r->client(), r->version(), id),
        m_settings->value());   // :767 — m_settings was never assigned on the failed branch
```

Path B, :323-324 introduces the empty value:

```cpp
setHasImageDescription(true);
m_imageDescription = imageDescription->get()->m_settings;
```

Crash at :377-378 `isHDR()` (`m_imageDescription->value().transferFunction ...`) or :341-347 `imageDescription()`.

### 5. Spec gap

- XML L1412-1418: *"An object which is not ready is illegal to use, it can only be destroyed. Any other request in this interface shall result in the 'not_ready' protocol error. Attempts to use an object which is not ready through other interfaces shall raise protocol errors defined there."*
- L1435-1441: error enum not_ready=0 / no_information=1 (not_ready is never posted)
- L745-747 (set_image_description): *"Image descriptions which are not ready … are forbidden in this request, and in such case the protocol error image_description is raised."*
- L1460-1474 (failed event): *"Once this event has been sent, the wp_image_description_v1 object will never become ready and it can only be destroyed."*
- The failed(no_output) at :269-270 itself is compliant (L686-688); the gap is exclusively the missing defenses after failed.

### 6. Fix proposal and reproduction

1. Add `bool m_ready = false;` to `CColorManagementImageDescription` (protocols/ColorManagement.hpp:154-175); set it in the `sendMaybeReady()` success branch (:794-808), clear it at every sendFailed call site (:221/:270/:511/:566/:803) — ideally via a single `markFailed(cause, msg)` helper
2. In `get_information` (:760), after the `m_allowGetInformation` check, add `if (!m_ready) { r->error(WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY, "description is not ready"); return; }`
3. In `set_image_description`, after the :316-321 match and before :323, add a ready check; if not ready, `r->error(WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION, ...)` (per L745-747); failed entries may also be removed from m_imageDescriptions at that point
4. Defense in depth: in `imageDescription()`/`isHDR()`/`isWindowsScRGB()` or at Renderer.cpp:2570, treat `!valid()` as "no description" and fall back to defaults, so the render thread can never crash on the empty WP
5. hyprtester cases: disable monitor → get_image_description → failed → get_information must assert a not_ready protocol error instead of a crash; set_image_description must assert the image_description error

**Reproduction A** (~10 lines of C, no surface needed):

1. `render:cm_enabled=1`; bind wp_color_manager_v1; get a wl_output from the registry → get_output=cmout; roundtrip
2. in a terminal: `hyprctl disable <monitor id>` (or unplug the cable)
3. `wp_color_management_output_v1_get_image_description(cmout)` → roundtrip yields `failed(cause=no_output)`
4. `wp_image_description_v1_get_information(desc, ...)` → roundtrip → hyprland SIGSEGV (ColorManagement.cpp:767, fault addr ≈ 0x0)

**Reproduction B**: after step 3, instead: create_surface S → get_surface(S) → set_image_description(desc, 0=perceptual) → attach buffer + commit → make the window fullscreen → next frame, Renderer.cpp:2571 → ColorManagement.cpp:378 SIGSEGV.
