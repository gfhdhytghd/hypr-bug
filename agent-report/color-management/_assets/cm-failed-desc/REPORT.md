# Bug report — failed (not-ready) wp_image_description_v1 objects remain usable

(Extract from the color-management audit; see the overall verification table at [`../../_evidence/color-management-verification.md`](../../_evidence/color-management-verification.md).)

## Report 2: a failed (not-ready) wp_image_description_v1 is still usable via get_information / set_image_description → empty m_settings WP dereferenced

### 1. Summary and severity

`CColorManagementOutput::get_image_description` correctly sends failed(NO_OUTPUT) when the output is gone (ColorManagement.cpp:269-270), but that failed object:

- (a) was already emplaced into `m_imageDescriptions` at :259-260, before success/failure is even decided, and is not removed on failure;
- (b) keeps a default-constructed empty `m_settings` WP (assigned only in the else branch at :272);
- (c) was created with `allowGetInformation=true` (passed true at :260);
- (d) `CColorManagementImageDescription` has no ready/failed state bit at all, and `WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY` is never posted anywhere in the file.

Consequences: after failed, `get_information` → `m_settings->value()` at :767 is a null deref on the main thread (crash); or the failed object is attached to a surface via `set_image_description` → :323-324 copies the empty WP and sets `hasImageDescription=true` → the render loop's `isHDR()`/`imageDescription()` (:378/:347, via Renderer.cpp:2571/2581, Monitor.cpp:2016-2017/2402) null-derefs on the render thread (whole machine hangs).

**Severity: High.** A single malicious or buggy client connection reliably crashes the compositor.

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
