# Bug report — feedback never becomes inert when its wl_surface dies

(Extract from the color-management audit; see the overall verification table at [`../../_evidence/color-management-verification.md`](../../_evidence/color-management-verification.md).)

## Report 1: feedback never becomes inert when its surface dies → null-deref SIGSEGV in onPreferredChanged

### 1. Summary and severity

`CColorManagementFeedbackSurface` holds a `WP<CWLSurfaceResource> m_surface` (src/protocols/ColorManagement.hpp:84). The spec requires the object to become inert once its associated wl_surface is destroyed, but the implementation never removes or inerts a feedback object when its surface dies: it stays in `PROTO::colorManagement->m_feedbackSurfaces`, and is only removed when the client explicitly destroys the feedback resource itself (src/protocols/ColorManagement.cpp:393-400 → destroyResource :921-923). Any later output-description change is broadcast to every feedback object, and `onPreferredChanged()` dereferences `m_surface` at :460 with no `expired()` guard; hyprutils `WeakPtr.hpp:221-226` makes `get()`/`operator->` return nullptr for an expired WP → SIGSEGV.

**Severity: High.** A single trivial client request sequence (create feedback → destroy your own surface) takes down the whole compositor on the next config/output change.

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

