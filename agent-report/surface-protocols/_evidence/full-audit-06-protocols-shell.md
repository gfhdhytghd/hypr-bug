# Shell protocol dossier verification

Baseline: official Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).
Dossier: [06-protocols-shell](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md).
Date: 2026-10-15.

Method: read-only inspection of the pinned upstream worktree, protocol XML, and installed dependency headers. Evidence marked **source-confirmed** describes static behavior, not a compositor reproduction. No desktop connections, IPC, full builds, network requests, or Git metadata changes. The two example reports named by the brief are absent from this checkout. The report was written incrementally. No compositor or GPU behavior was executed; the bounded region controls and binding generation are the only successful executions. The synthetic decoder control was unavailable before dispatch, as detailed under F-PROTO-10. All source locations below refer to the pinned upstream tree unless explicitly identified as installed dependencies or scratch-generated files.

## Summary

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
| --- | --- | --- | --- | --- |
| F-PROTO-01 | partially | Low (closed claim); High hang unproven | `src/protocols/XDGShell.cpp:106-118,605-627`; `src/desktop/view/window/Window.cpp:605` | Self-parenting is accepted, but the proposed hang chain lacks a desktop popup; duplicate ownership can instead abort, covered by F-PROTO-07. |
| F-PROTO-02 | confirmed | High | `src/protocols/XDGShell.cpp:520,970` | Destroy-before-role leaves a live popup with expired content; grab dereferences it. Reposition also lacks a content-lifetime guard. |
| F-PROTO-03 | partially | Medium | `src/protocols/XDGShell.cpp:79-82,970-988`; `src/managers/input/InputManager.cpp:561` | Cross-client grab allowlisting is confirmed; passive keylogging and permanent grab retention are not. Input delivery still needs hit-testing and focus. |
| F-PROTO-04 | confirmed | Medium | `src/protocols/core/Compositor.cpp:194-214,607-656`; `src/protocols/types/SurfaceStateQueue.cpp:17-18,93-109` | A valid unsignaled fence retains an unbounded state queue; scans amplify work. Raw eventfd is not a dma-buf fence. |
| F-PROTO-05 | partially | Medium | `src/protocols/LayerShell.hpp:64`; `src/render/Renderer.cpp:2801` | Margin sums use doubles, rejecting the cited overflow site; applyExclusive converts margins to integers and performs unchecked signed addition. |
| F-PROTO-06 | partially | High | `src/protocols/XDGShell.cpp:323-366` | Unbounded recursive descendant traversal can exhaust the stack; destructor unlinking defeats the claimed expired-child UAF path. |
| F-PROTO-07 | partially | High | `src/protocols/XDGShell.cpp:579-627`; `src/desktop/view/WLSurface.cpp:165` | Role checks are absent, but duplicate toplevel creation aborts on CWLSurface ownership before unbounded window accumulation. |
| F-PROTO-08 | partially | Low | `src/protocols/core/Compositor.cpp:71-72,258-269,537`; `src/protocols/LayerShell.cpp:132-143,195-202` | Complex regions and outstanding layer serials are unbounded; identical rectangles coalesce, and unchanged mapped sizes do not always produce configure events. |
| F-PROTO-09 | confirmed | Low | `src/protocols/XDGShell.cpp:630-634`; `src/desktop/view/window/WindowBackend.cpp:19-27` | Unsent and repeated serials are accepted; numeric ordering poisons later acknowledgments and mishandles serial wraparound. |
| F-PROTO-10 | rejected | None (hardening only) | `CMakeLists.txt:265,274`; `protocols/wlr-layer-shell-unstable-v1.xml:297`; `src/protocols/LayerShell.cpp:120-129` | Typed nonnullable arguments and resource lifetime prevent the alleged confusion/null input; libwayland older than 1.21 is outside supported dependencies. |

Finding-ID counts: **3 confirmed, 6 partially, 1 rejected, 0 inconclusive**. A partially verdict can still include a source-confirmed serious defect; it records which compound claims were not sustained. High severity is local-client compositor termination/stack exhaustion, never Critical and never remote-network compromise by itself.

The dossier's six unnumbered control records are included below without assigning new finding IDs. For these rows, the verdict evaluates the dossier's stated control conclusion, rather than asserting a new security finding.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
| --- | --- | --- | --- | --- |
| Unnumbered control: move/resize | confirmed | Informational | `src/protocols/XDGShell.cpp:193-237`; `src/managers/SeatManager.cpp:185-204` | Resize edges, seat ownership, and pointer-button serial provenance are checked before move/resize events. |
| Unnumbered control: positioner/version | confirmed | Informational | `src/protocols/XDGShell.cpp:165,453,685-700` | Positive positioner sizes and relevant protocol-version gates are present. |
| Unnumbered control: scale/damage | confirmed | Informational | `src/protocols/core/Compositor.cpp:25-36,226-239` | Nonpositive buffer scales are rejected; damage endpoints use int64_t and clipping. |
| Unnumbered control: subsurface ancestry | partially | Low functional; cycle-bypass severity unproven | `src/protocols/core/Subcompositor.cpp:119-150,160-190` | Entry guards and visited lists exist; both ancestry traversals use the wrong cursor. The claimed cycle-check bypass remains unproven. |
| Unnumbered control: deferred lifetime | confirmed | Informational | `src/protocols/XDGShell.hpp:170,227`; `src/managers/eventLoop/EventLoopManager.cpp:25-27` | Deferred configure callbacks have destruction-canceling ownership; no dangling callback is established by the cited paths. |
| Unnumbered control: foreign-toplevel | partially | Informational | `src/protocols/ForeignToplevelWlr.cpp:23-118`; `src/protocols/ForeignToplevel.cpp:5-33` | Window/output lifetime checks hold for cited paths, but the dossier omits activate, close, maximize, minimize, and manager stop requests. |

Control counts: **4 confirmed, 2 partially, 0 rejected, 0 inconclusive**. Across all 16 records: **7 confirmed, 8 partially, 1 rejected, 0 inconclusive**. These are record counts, not counts of independent exploitable vulnerabilities; F-PROTO-01's duplicate-ownership observation overlaps F-PROTO-07.

## F-PROTO-01

**Verdict: partially. Severity: Low for the closed claim; High hang unproven.**

The dossier claims a self-parented XDG popup reaches an endless offset traversal through later toplevel creation and reposition. Self-parenting and the loop are present, but the supplied end-to-end hang chain is not closed at this baseline.

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:605-627` resolves `parentXDG` without checking identity or existing XDG role, stores `m_popup` at `:617`, and appends it to the parent's popup list at `:625`. Self-parenting is possible because the XML permits a typed XDG surface object, and libwayland validates interface identity, not parent-graph relationships. The consumer has no cycle check:

```cpp
while (current) {                         // XDGShell.cpp:109
    // ...
    if (current->m_popup)
        current = current->m_popup->m_parent.lock(); // :111-113
    else
        break;
}
```

On a hypothetical call with that self-parented graph intact, traversal does not advance to a different object or terminate. That property alone does not prove that a client can reach the consumer in the claimed ordering.

**Caller countercheck — source-confirmed.** The dossier relies on `get_toplevel` replaying `newPopup` at `XDGShell.cpp:598-602`. Upstream forwards that signal through `src/desktop/view/window/WaylandBackend.cpp:202` to `src/desktop/view/window/Window.cpp:146-149`, which only creates a popup if `popupHead()` exists. Window creation (`Window.cpp:101-131`) initializes surface ownership but does not initialize its popup head. The head is created later by `CWindow::onMap` (`:552,601-605`), reached through `mapWindow` (`:1148,1218`). The initial replay therefore does not install the desktop popup or its reposition listener. The XDG surface mapping handler (`XDGShell.cpp:556-564`) does not replay pending popups after that head appears. A bare reposition event (`:69-76`) cannot itself instantiate the missing desktop popup.

If a popup view is instead created on a core surface that is already owned by the window, `CPopup::create` assigns it (`src/desktop/view/Popup.cpp:43-53`) before positioning; `src/desktop/view/WLSurface.cpp:165` aborts for duplicate ownership. This is the duplicate-role failure assessed in F-PROTO-07, not evidence of this hang. No alternate cyclic topology was executed or established as a complete offset-loop trigger.

**Reachability/severity.** Low for the independently established protocol graph-invariant omission; the dossier's High compositor hang remains unproven. No special permission is required for XDG objects, but desktop positioning additionally needs a created popup view and a monitor. The separate `CPopup` parent traversals at `Popup.cpp:406,450,537` walk desktop-view parents, not XDG protocol parents; `CPopup::create` attaches a new child to an existing parent (`:43-56,161-163`). F-PROTO-06 concerns another graph entirely. It does not establish a desktop-popup cycle.

**Correction/fix direction.** Do not retain the dossier's hang reproduction as verified. Reject self-parenting and conflicting roles at request entry; independently bound/detect cycles in protocol-parent traversal. A controlled isolated compositor test demonstrating a live positioning consumer on the cyclic graph would be required to promote the hang claim.

## F-PROTO-02

**Empirical reproduction — executed.** Nested harness (fork binary, 2026-10-15): client created an xdg_popup, destroyed its content xdg_surface while the role survived, then issued xdg_popup.grab — the nested compositor aborted (`COMPOSITOR_EXITED status=134`, SIGABRT), client exit 0. The same client also crashed the real XPS session (PID 600225 → SIGABRT → respawn).

**Verdict: confirmed. Severity: High.**

The dossier claims that popup requests dereference the popup's destroyed content `xdg_surface`. The lifetime defect is present; its assertion that destroying the content before the popup is protocol-legal is false.

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:520-531` emits unmap/destroy and removes the XDG surface without rejecting a live role object. `:958-960` removes its owning shared pointer. Popup `m_surface` is weak (`src/protocols/XDGShell.hpp:70`), as is the core surface's XDG role reference (`:184`), so neither retains the destroyed object. The popup itself remains strongly owned in the protocol's `m_popups` vector (`:301`). Its request callback at `XDGShell.cpp:79-82` calls `addOrStartGrab` without checking content, seat or serial:

```cpp
m_grab->add(popup->m_surface->m_surface.lock()); // XDGShell.cpp:975; repeated at :985
```

The chain is client popup request → retained popup resource → expired content weak pointer → unchecked member access. The outer weak pointer returns null after deletion, rather than preserving a freed object: installed `/usr/include/hyprutils/memory/WeakPtr.hpp:221-226` implements `operator->()` through `get()`. This is a null dereference, not demonstrated UAF. No mapping, rendering, or valid input serial is needed for the grab branch. The seat must still be a real, correctly typed client-owned object; ignoring a parameter does not bypass libwayland object decoding.

**Evidence — source-confirmed.** Reposition forwards through `XDGShell.cpp:69-76`, `src/desktop/view/popup/WaylandPopupBackend.cpp:16`, `src/desktop/view/Popup.cpp:362-379`, and backend `:66-69` to `XDGShell.cpp:94`:

```cpp
CBox constraint = box.copy().translate(m_surface->m_pending.geometry.pos());
```

This alternate branch requires a previously instantiated desktop popup and a usable monitor. The backend listens to the content's unmap/map/commit/newPopup signals (`WaylandPopupBackend.cpp:23-26`), but not its destroy signal; reposition does not call `valid()`. The content's unmap only unmaps the popup (`Popup.cpp:154,170-188,278`); popup-role destruction is what removes that desktop popup. The direct grab branch already closes the failure path independently.

**Reachability/severity.** High: a local connected client can cause a compositor null dereference using its own protocol objects. The request ordering violates the XDG protocol, which must be rejected safely. Installed XDG XML `xdg-shell.xml:476-480` explicitly requires a `defunct_role_object` error for this ordering. No compositor crash was executed.

**Correction/fix direction.** Reject content destruction while a role survives; independently lock/check popup content and its core surface at every request boundary. An expired XDG reference crashes before `CSeatGrab::add`, so the dossier's additional null-insertion consequence does not follow from this same path.

## F-PROTO-03

**Verdict: partially. Severity: Medium.**

The dossier alleges that an unrelated client's popup can join an existing popup grab and steal input. Cross-client membership is source-confirmed; unconditional keyboard theft and permanent grab ownership are overstated.

**Evidence — source-confirmed.** The popup `grab` callback at `src/protocols/XDGShell.cpp:79-82` ignores seat/serial semantics. When an owner is already present, `addOrStartGrab` has no client, parent-stack, or seat comparison:

```cpp
m_grabbed.emplace_back(popup);                    // :983
m_grab->add(popup->m_surface->m_surface.lock());   // :985
if (popup->m_parent)
    m_grab->add(popup->m_parent->m_surface.lock());// :987-988
```

A client can only name its own correctly typed objects through normal Wayland decoding. That does not prevent this issue: the protocol stores one global grab shared by all clients (`src/protocols/XDGShell.hpp:303-306`), so the added popup and parent are the requesting client's own objects. The code, rather than a forged resource ID, crosses the client-isolation boundary. `CSeatGrab::add` merely appends, and `accepts` only checks membership (`src/managers/SeatManager.cpp:878-884`).

**Evidence — source-confirmed.** Client grab request → shared grab list insertion → input hit-test → `src/managers/input/InputManager.cpp:561-578` no longer rejects that client's surface → normal pointer focus/motion (`:601-603,712-715`) and button delivery (`:910-911`). Keyboard focus additionally passes the grab membership check in `src/desktop/state/FocusState.cpp:238-241`; actual focus selection remains subject to window visibility, geometry, input regions, and focus policy (`InputManager.cpp:635-705`). The default `input:follow_mouse` is 1 (`src/config/values/ConfigValues.cpp:380`). A mapped, input-eligible attacker surface under the pointer can therefore receive events during another client's grab instead of being excluded by that grab. Joining alone does not immediately call `refocusGrab`, and events delivered to the victim are not copied to the attacker.

**Guards/corrections — source-confirmed.** First-grab installation checks the session lock (`SeatManager.cpp:755-758`); keyboard surface focus has a session-lock check too (`FocusState.cpp:235-236`). No lockscreen bypass is established. `XDGShell.cpp:991-998` ends the seat grab and clears grabbed entries when the owner is destroyed; the grab callback also dismisses/clears entries at `:928-933`. `m_grabOwner` is weak, so its failure to reset explicitly is a stale-bookkeeping concern, not a strong ownership leak or proof of perpetual input capture. The ignored seat still must decode as a live `wl_seat`; an arbitrary integer object ID is not sufficient.

**Severity.** Medium for cross-client grab isolation failure with hit-test/focus/user-interaction preconditions. No two-client input capture or keyboard observation was executed. This does not support passive keylogging of unrelated windows or a general privilege escalation.

**Fix direction.** Validate the input serial and seat, enforce a same-client valid popup-parent grab stack, and explicitly clear owner and membership on dismissal. Keep ordinary focus and session-lock guards.

## F-PROTO-04

**Empirical reproduction — executed (negative).** Real XPS session (Hyprland 0.56.2, 2026-10-15): a client issuing 2000 unsignaled acquire-point commits via an exported syncobj timeline did not grow Hyprland RSS (216 MB → 216 MB, +0); the unsignaled exported timeline was rejected at import ("Timeline failed importing"), so the unbounded-queue path was not reached. Triggering it needs a valid but unsignaled GPU dma-buf/sync_file fence, which this client could not construct — growth neither confirmed nor refuted for that harder vector.

**Verdict: confirmed. Severity: Medium.**

The per-surface state queue has no budget or progress timeout. A valid fence-blocked front state prevents later states from draining or merging, retaining client-controlled state and buffer references.

**Evidence — source-confirmed.** Core `wl_surface.commit` reaches `src/protocols/core/Compositor.cpp:193-214`, which copies pending state into the queue and subsequently schedules it. `src/protocols/types/SurfaceStateQueue.cpp:17-18` performs unconditional deque insertion. Processing first checks the front lock:

```cpp
if (front->isLocked())
    break;                       // SurfaceStateQueue.cpp:97-98
// merging following states happens only after that check (:100-105)
```

Thus the available merge optimization does not collapse states behind a blocked front. `SSurfaceState` retains a `CHLBufferReference` and texture; copying buffer references increments buffer locking (`src/protocols/types/Buffer.cpp:57-59,66-73`). Each enqueued state can also retain regions, callbacks, and presentation feedback. This is not merely an ever-growing count of wire requests after they have been processed.

**Fence path — source-confirmed.** Implicit synchronization exports genuine sync files from a valid imported dma-buf at `Compositor.cpp:200-203`, via `src/protocols/types/DMABuffer.cpp:94-120`. `scheduleState`/`drainSyncFds` (`Compositor.cpp:607-656`) registers a readable waiter and leaves the fence lock held while it waits. There is no timeout here. Explicit sync uses validated imported DRM timelines (`src/protocols/DRMSyncobj.cpp:118-132`), applies acquire/release-point preconditions (`:77-110`), and sets `LOCK_REASON_FENCE` at `:106`. Its waiter path is `:30-32` → `src/helpers/sync/SyncTimeline.cpp:67-92` → `Compositor.cpp:615-623`. On supported DRM backends, a registered acquire wait that remains unsignaled can therefore hold the front. Continued accepted surface commits retain later queue entries until the block resolves or the surface/client is torn down.

**Amplification — source-confirmed.** `SurfaceStateQueue.cpp:79-90` linearly searches for a state, while `:51-64` scans the whole queue on `unlockFence`. A succession of such calls over a retained growing queue can require quadratic aggregate scans. This is conditional on the event/commit pattern; a single wakeup is linear and `tryProcess` can coalesce ready adjacent entries once the head unlocks. Kernel/GPU costs and exact runtime scaling were not measured.

**Reachability/preconditions.** A connected client with usable buffer/fence facilities is required. Explicit sync is only advertised when renderer and DRM timeline support are available (`src/managers/ProtocolManager.cpp:239-255`); valid implicit dma-buf fences likewise depend on the buffer/driver path. An arbitrary raw eventfd is not accepted as an imported dma-buf or sync file. Hyprland's internal eventfd in `SyncTimeline.cpp:80-92` is registered with the DRM syncobj API; it does not validate the dossier's proposed eventfd-as-buffer-fence shortcut. A failed waiter registration is explicitly unlocked at `Compositor.cpp:621-622`, so failure to obtain a waiter is not a permanently locked-head trigger.

**Severity/corrections.** Medium for unbounded server-side resource retention and CPU amplification, not an executed compositor crash or universally available GPU-free DoS. FD/GEM growth requires retained distinct resources/waiters; repeatedly referring to one buffer does not create one new GEM object per state. The dossier's implicit/explicit-fence terminology and stale `603-612` locations are corrected above. Ready-state merging combines damage (`SurfaceState.cpp:135-138`), but a blocked queue does not merge on every enqueue. A null-buffer attach explicitly clears the queue (`Compositor.cpp:183-190`), and surface/client teardown reclaims owned state; the issue requires the client to keep the blocked surface alive.

**Fix direction.** Bound outstanding state/resources per surface and client, apply backpressure or a safe protocol error on budget exhaustion, and avoid full-queue scans for individual fence readiness. Preserve required buffer-release and presentation-feedback semantics; blindly dropping arbitrary states is not necessarily protocol-correct. No GPU access or fence reproduction was attempted.

## F-PROTO-05

**Verdict: partially. Severity: Medium.**

The dossier alleges signed overflow in layer margin sums and shared-layout corruption. The cited margin sums are floating point, but a separate signed addition in exclusive reservation is vulnerable to overflow.

**Evidence — source-confirmed.** Client setters store exclusive zone and margins at `src/protocols/LayerShell.cpp:100-107`. The actual state types are:

```cpp
int32_t exclusive = 0;                         // LayerShell.hpp:56
// ...
double left = 0, right = 0, top = 0, bottom = 0; // :64
```

Therefore the sums at `src/render/Renderer.cpp:2863,2873` are `double`, as are the box coordinates and sizes; the dossier's alleged `int32_t` overflow there is rejected. The size guard at `:2881-2884` skips nonpositive boxes.

**Evidence — source-confirmed.** The remaining path is layer request → `markPending` (`LayerShell.cpp:182-185`) → core-surface commit → layer state application (`:43-65,82`) → desktop layer commit (`src/desktop/view/LayerSurface.cpp:300-308,325-354`) → renderer arrangement (`Renderer.cpp:2813-2829,2888-2890`) → `applyExclusive`. That helper accepts `int32_t` margins (`:2755-2756`), stores each in `int margin` (`:2765-2797`), and evaluates:

```cpp
if ((exclusiveEdge == edges[i].singular_anchor ||
     anchor == edges[i].singular_anchor || anchor == edges[i].anchor_triplet)
    && exclusive + edges[i].margin > 0) {       // :2801
    *edges[i].positive_axis += exclusive + edges[i].margin; // :2803
    // negative axis similarly at :2806
}
```

Margins originate as representable `int32_t` values, so their conversion back from double is not itself the fault. Positive exclusive zone plus a sufficiently large positive matching-edge margin can exceed signed-int range. This expression is evaluated before the resulting amount is added to double geometry; floating-point destinations do not prevent that overflow. Positive desired width/height and a singular edge can satisfy the prior size and anchor checks. No buffer mapping is required for arrangement of an unconfigured layer; an available monitor and access to layer-shell are required. The global is normally visible to unsandboxed clients but filtered for security-context sandboxed clients (`src/Compositor.cpp:267-271`, `src/managers/ProtocolManager.cpp:348-403`); the dossier's arbitrary-client wording should not erase this distinction.

**Severity.** Medium for reachable UB and unchecked reservation arithmetic affecting shared usable area, not a proven crash or memory corruption. Even non-overflowing excessive positive reservations can shrink usable area below zero (`:2806`); the resulting area is used for subsequent layers and recorded at `:2916-2927`. Layer-shell intentionally grants layout authority, so ordinary large exclusive zones alone are not evidence of a privilege boundary violation.

**Corrections/fix direction.** The dossier's `TOP|LEFT` example is not a matching singular edge or supported anchor triplet without an explicit exclusive edge, so it does not establish this overflow branch. Negative/oversized `set_size` values are not categorically rejected: `LayerShell.cpp:57-64` permits nonpositive sizes when both opposite anchors are present; arrangement may subsequently skip them. Use wide arithmetic for the exclusive-plus-margin sum and bound the resulting reservation to sensible output geometry, preserving legitimate negative-margin semantics. No UBSan or compositor execution was performed.

## F-PROTO-06

**Verdict: partially. Severity: High.**

The dossier combines an unbounded recursive parent-validation traversal with a speculative expired-weak-pointer/UAF scenario. Recursion is present and reachable; the suggested destruction-based UAF is not established.

**Evidence — source-confirmed.** Client `xdg_toplevel.set_parent` resolves a typed parent and calls `setNewParent` at `src/protocols/XDGShell.cpp:301-304`. That method rejects self-parenting (`:329-333`), then recursively explores the requesting toplevel's descendants (`:335-348`):

```cpp
for (const auto& c : tl->m_children) { // :337
    if (c == target)
        return true;
    any = any || exploreChildren(c, target); // :341
    if (any)
        break;
}
```

The traversal is not tail-recursive: it must combine the return value, test it, and continue iteration. There is no depth cap. The parent-child graph is populated at `:354-356`. Distinct live toplevels can form an arbitrarily deep acyclic tree without mapping buffers or requiring user input; validation of a node with deep descendants visits that subtree. Thus client-controlled depth reaches compositor-thread call-stack consumption even though graph cycles are rejected. Protocol ownership of the toplevels is strong (`src/protocols/XDGShell.hpp:300`), while the parent/children edges are weak (`:157,165`). A finite process stack cannot support unbounded recursive depth. Exact failure depth and actual SIGSEGV are not measured.

**Counterevidence — source-confirmed.** Parent changes remove the old child edge first (`XDGShell.cpp:324-327`), and destruction unlinks the child:

```cpp
if (m_parent)
    std::erase_if(m_parent->m_children,
        [this](const auto& other) { return !other || other.get() == this; }); // :365-366
```

Installed hyprutils distinguishes `expired()` from `valid()` during destruction (`WeakPtr.hpp:154-166`); weak boolean conversion uses `valid()` (`:188-190`), and weak dereference obtains the live raw pointer (`:221-226`). Self-weak pointers can therefore remain valid during destructor unlinking, even though `lock()` refuses destruction-in-progress. Merely observing a weak pointer without an explicit lock is insufficient to claim UAF. Sequential requests in one batch do not run concurrently with this traversal. No normal-lifecycle route retaining a dead child in `m_children` was found.

**Severity.** High for the source-closed local-client stack-exhaustion DoS, revised from the dossier's Medium under the brief's compositor-crash rubric. This is not a claim that a measured number of toplevels fits every machine's resource limits.

**Corrections/fix direction.** The dossier's deep-chain count is an estimate, not reproduced evidence, and its UAF escalation is withdrawn. Traverse iteratively with a visited/depth budget; prune expired edges defensively. Prefer bounded ancestor validation where it can preserve protocol semantics.

## F-PROTO-07

**Verdict: partially. Severity: High.**

Repeated XDG role creation is not rejected at the request boundary. However, the claimed accumulation of indefinitely many desktop windows on one surface is interrupted by a compositor ownership assertion.

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:579-593` allocates and registers a new toplevel, overwrites `m_toplevel`, and creates a window without testing existing `m_toplevel` or `m_popup`. `:605-627` likewise overwrites `m_popup` without a role check. The earlier `get_xdg_surface` role guard (`:878-902`) applies when creating the XDG surface, not when choosing a role on an already created XDG surface. libwayland new-ID and interface validation cannot enforce these higher-level role invariants.

**Evidence — source-confirmed.** The caller chain closes at a different failure:

```cpp
PHLWINDOW createdWindow = CWindow::create(...);  // XDGShell.cpp:593
if (const auto SURFACE = m_backend->surface())
    wlSurface()->assign(SURFACE, SELF);          // window/Window.cpp:126-127
RASSERT(!m_resource->m_hlSurface,
        "Attempted to duplicate CWLSurface ownership!"); // WLSurface.cpp:165
```

The first window is retained by desktop window state (`src/desktop/view/window/Window.cpp:129`, `src/desktop/view/View.cpp:23-34`, `src/desktop/state/WindowState.cpp:14-20`), and its assigned `CWLSurface` remains associated with the core surface. Creating another window on that same core surface reaches `CWLSurface::assign` (`WLSurface.cpp:20-24`) and then `init()` (`:161-169`), which encounters the existing owner. `src/macros.hpp:41-53` implements this assertion using `raise(SIGABRT)`; it is not a debug-build-only `assert`.

**Reachability/severity.** High for compositor abort from a connected client's own duplicate role request. The assertion is reached during window creation, without mapping a buffer or receiving input. This is source-confirmed; no Hyprland process was run. The ordinary first role's desktop ownership is the necessary precondition.

**Corrections/fix direction.** Withdraw the one-surface infinite-window/RSS-growth mechanism and speculative UAF. Multiple role resources and weak parent lists are still accepted on paths that do not construct a duplicate view, but they do not prove those downstream consequences. Track role assignment at XDG-surface level and post the appropriate protocol role/state error before allocating resources or attaching desktop views. Do not replace the compositor assertion with silent inconsistent ownership.

## F-PROTO-08

**Verdict: partially. Severity: Low.**

The dossier combines unbounded region complexity with unbounded layer configure history. Both retain unbudgeted state, but repeated identical rectangles do not multiply storage and configuration generation has caller guards.

**Evidence — source-confirmed.** `src/protocols/core/Compositor.cpp:71-72` forwards region add/subtract directly to `CRegion`; there is no Hyprland rectangle-complexity budget. `set_input_region` copies the region when the request is handled (`:258-269`), not automatically on every commit. State copies retain it; hit-testing materializes a clipped copy through `src/protocols/types/SurfaceState.cpp:48-55` → `Compositor.cpp:537-539`. A mapped input-eligible surface with a genuinely fragmented region can therefore incur repeated copy/intersection work during normal pointer hit-testing. The region object itself can retain complexity before mapping; no special protocol or permission is required.

**Counterexample — executed (dependency-only).** The bounded pixman control in `dependency_controls.c` under the designated scratch directory performed 10,000 identical rectangle unions and observed **one** stored rectangle. A separate 128-disjoint-rectangle control observed 128 stored rectangles. A second control, `region_controls.cpp`, called the actual installed hyprutils `CRegion::add`/`getRects` methods and independently obtained the same counts. These use hyprutils 0.14.2/pixman 0.46.4, do not instantiate Hyprland, and do not connect to a desktop. `dependency_controls.log` and `region_controls.log` record the results. Hyprutils `Region.hpp:13-18,27-38,63-79` exposes/copies pixman regions. Consequently, the dossier's repeated same-coordinate add example does not establish its memory-amplification claim. Complex non-coalescing regions remain possible; exact whole-compositor CPU costs were not measured.

**Evidence — source-confirmed.** The layer queue is a vector of serial/size pairs (`src/protocols/LayerShell.hpp:89`). Each configure appends at `src/protocols/LayerShell.cpp:195-202`, and acknowledgment removes only the matching entry (`:132-143`), with no cap or age-based pruning:

```cpp
m_serials.push_back({serial, size_}); // LayerShell.cpp:200
// In the ack callback:
m_serials.erase(serialFound);        // :143
```
 The chain is layer state request/commit → `LayerShell.cpp:43-82` → `src/desktop/view/LayerSurface.cpp:300-308,325-354` → `src/render/Renderer.cpp:2892-2893` → `configure` → retained history.

There are important caller conditions. Renderer configure is normally sent only when arranged size differs from previous geometry (`Renderer.cpp:2892`); repeatedly setting an unchanged mapped size does not necessarily generate entries. However, unconfigured/unmapped layer commits reset `m_geometry` before arrangement (`LayerSurface.cpp:304-308`), so positive arranged geometry can keep generating configure entries without a mapped buffer. For a mapped layer, accepted changes of desired geometry can also generate entries without further acknowledgments. A buffer attached before any valid initial ack is rejected (`LayerShell.cpp:49-51`), but this does not cap the bufferless configuration path. The client must drain outgoing configure events to avoid ordinary output-buffer backpressure becoming the practical bound. Layer-shell additionally requires the global access and monitor conditions described under F-PROTO-05.

**Severity/corrections.** Low resource-budget weakness; no measured compositor-wide exhaustion or fixed amplification factor. Region complexity is canonical geometry, not the number of add requests. Layer history can grow even when a client acknowledges newer configurations because only the matched serial is erased, rather than retiring older entries. Do not claim a guaranteed configure per arbitrary size setter without following the commit/geometry guards.

**Fix direction.** Apply measured per-client region-complexity/resource budgets and bound outstanding configuration history, retiring obsolete serials according to issuance order. The dossier's suggested `WL_REGION_ERROR_INVALID_SIZE` is not a defined core protocol error; use an appropriate supported failure policy rather than inventing an enum.

## F-PROTO-09

**Verdict: confirmed. Severity: Low.**

The XDG surface accepts acknowledgments based only on unsigned numeric ordering, rather than outstanding configure identity. Upstream's additional window acknowledgment tracker also uses numeric ordering, so it does not repair this defect.

**Evidence — source-confirmed.** The client request reaches `src/protocols/XDGShell.cpp:630-635`:

```cpp
if (serial < m_lastConfigureSerial)
    return;
m_lastConfigureSerial = serial;
m_events.ack.emit(serial);
```

`m_lastConfigureSerial` starts at zero (`src/protocols/XDGShell.hpp:229`), is independent of the scheduled/sent serial (`XDGShell.cpp:660-673`), and accepts equality repeatedly. There is no membership test against sent configurations. Subsequent numerically smaller acknowledgments are silently ignored, including valid serials after wraparound. This is an unsigned comparison defect; the dossier's reference to unsigned subtraction is inaccurate because no subtraction occurs here.

**Evidence — source-confirmed.** The emitted event reaches `src/desktop/view/window/WaylandBackend.cpp:162,442-447`. The tracker chooses the latest queued entry with `configure.first <= serial` (`src/desktop/view/window/WindowBackend.cpp:19-27`), consumes entries through it, and returns its size. `WaylandBackend.cpp:318-324` places that size in pending core-surface state, and `src/protocols/types/SurfaceState.cpp:232-233` applies it on commit. Thus an unsent sufficiently large serial can consume existing pending window configurations. It cannot inject an arbitrary attacker-selected size: only a size already present in the pending tracker is returned. An acknowledgment before any tracked configure has no returned size, but still updates the XDG surface's numeric threshold.

**Protocol countercheck — source-confirmed.** Installed `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml:571-574,584-593` requires an `invalid_serial` error for never-sent, reused, or previously consumed serials. `wl_display_next_serial` at `XDGShell.cpp:666` supplies display-wide serials; they are not a monotone, unbounded per-window counter.

**Reachability/severity.** Any connected XDG client can send an arbitrary `uint32_t` acknowledgment on its own object; no special permissions or input serial is involved. Low: protocol/state negotiation corruption of that client's own surfaces, not a source-demonstrated cross-client compromise or memory-safety failure. Mapping/buffer commits are needed to observe the downstream visual scaling effects cited by the dossier (`src/desktop/view/WLSurface.cpp:43-77`); those visual effects were not tested.

**Correction/fix direction.** The tracker path and `SurfaceState` lines have changed from the dossier. Validate exact outstanding XDG serials, consume the acknowledged prefix by issuance order, and have the window tracker use the same identity/order rather than raw unsigned `<=`. Treat stale/repeated/unknown serials as the specified protocol error.

## F-PROTO-10

**Verdict: rejected. Severity: None (hardening only).**

The dossier treats unchecked `fromResource` casts and layer-popup dereference as an exploitable type/null confusion, primarily on an old or modified libwayland. No such trigger is established on supported upstream dependencies; local defensive checks would still be reasonable hardening.

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:120-122,369-371,655-657,715-717` and `src/protocols/core/Compositor.cpp:326-328` rely on generated binding user data and assume the incoming resource's interface. The layer handler at `src/protocols/LayerShell.cpp:120-129` indeed dereferences the returned popup without an additional null branch. Those observations are accurate, but do not establish attacker control over the C++ pointer type.

The relevant signatures are typed: pinned `protocols/wlr-layer-shell-unstable-v1.xml:287-297` declares a nonnullable `object interface="xdg_popup"`; installed `xdg-shell.xml:506-508` declares popup new-ID, nullable typed XDG parent, and nonnullable typed positioner. Header documentation `/usr/include/wayland-util.h:102-144` describes type/nullability metadata used by the demarshaler. Core `wl_buffer`/XDG-toplevel IDs do not satisfy an `xdg_popup` argument. IDs are resolved in the sending client's resource namespace, not as arbitrary cross-client pointers. New-ID handling creates a server resource and is not a client-supplied C++ object pointer.

**Generated-binding corroboration — executed generation, source-confirmed inspection.** The installed hyprwayland-scanner 0.4.6 generated bindings into the designated scratch directory only. Its `wlr-layer-shell-unstable-v1.cpp:262-264,286` retains the popup interface and nonnullable `"o"` signature; `xdg-shell.cpp:460-463,481` retains typed `"n?oo"`. This is a local dependency-generation check, not a pinned upstream generated-file citation. CMake uses the scanner at `CMakeLists.txt:498-513` and requires version 0.3.10 or later (`:294`). The installed libwayland 1.26.0 binary also contains diagnostics for wrong object type, nonnullable null objects, and invalid object IDs.

**Lifetime countercheck — source-confirmed.** Popup role resources are strongly owned by the protocol (`src/protocols/XDGShell.hpp:301`). Both destroy handlers remove that role (`XDGShell.cpp:54-66,966-967`); there is no independent code path here that makes a still-live role resource's C++ object null while keeping its wire ID dispatchable. Locally generated `xdg-shell.cpp:987-1025` installs the wrapper user data, clears/destroys its resource on wrapper destruction, and clears its resource pointer during resource destruction. A subsequent request naming a destroyed ID is rejected by resource lookup. Destroying just the content XDG surface leaves a valid popup role object with an invalid *content* reference: that is F-PROTO-02, not a null `fromResource(popup_)` result.

**Dependency boundary.** Pinned `CMakeLists.txt:265,274` requires `wayland-server >= 1.22.91`; the dossier's `< 1.21` premise is outside upstream's supported build. No untyped object argument on these reported paths was identified. Replacing the protocol library with one that omits its validation is a changed trust/dependency assumption, not evidence against this baseline. `wl_resource` is opaque in the public server headers; checking a hypothetical public `res->interface` field is not an appropriate suggested patch. Prefer `wl_resource_instance_of` or equivalent supported checks.

**Execution limitation.** The synthetic decoder control could not create even a toy `wl_client`: `wl_client_create` returned `EPERM` in this environment before dispatch. Therefore wrong-type/null rejection was **not runtime-tested** here. The saved harness/log explicitly record this limitation; zero executed callbacks is not treated as a passing guard test. The initial assertion-based attempt also stopped at client creation, and an optional `strace` diagnostic was unavailable. No permission changes or desktop access were attempted.

**Severity/fix direction.** None for the claimed upstream exploit; optional hardening is to validate user data/interface and handle null at adapter boundaries. A distinct, supported-runtime route producing a correctly typed live popup resource without its protocol object would be needed to reopen this claim.

## Unnumbered controls

### Move/resize validation — confirmed; Informational

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:17-31` enumerates permitted resize edges, and `:213-217` reports an invalid-resize-edge error before continuing. Move and resize both check decoded seat ownership against the requesting client (`:193-205,219-230`) and call `CSeatManager::pointerButtonSerialValid`. That function rejects absent seat/surface/zero serial, searches the seat container's recorded pointer-button serials, and validates the recorded surface's tree (`src/managers/SeatManager.cpp:185-204`). The client request therefore cannot reach the move/resize event emission solely by inventing an arbitrary serial or foreign seat. No failure or fix is established on these paths. This control does not apply to popup `grab`, whose callback omits analogous checks (F-PROTO-03).

### Positioner dimensions and version gates — confirmed; Informational

**Evidence — source-confirmed.** Positioner size and anchor-rectangle requests reject nonpositive dimensions at `src/protocols/XDGShell.cpp:685-700`:

```cpp
if UNLIKELY (x <= 0 || y <= 0) { /* error; return; */ } // :686
if UNLIKELY (w <= 0 || h <= 0) { /* error; return; */ } // :695
```

The WM capabilities event is gated on toplevel version 5 (`:165-173`), and adding SUSPENDED state requires version 6 (`:452-464`). These guards break the particular invalid-dimension/older-version paths listed by the dossier. No change is indicated by this control; it does not assert that every positioner enum/state invariant is validated.

### Buffer scale and damage arithmetic — confirmed; Informational

**Evidence — source-confirmed.** Core surface `set_buffer_scale` rejects `scale <= 0` (`src/protocols/core/Compositor.cpp:226-239`). Both surface- and buffer-damage requests call `addSafeDamage` (`:217-223`), which rejects nonpositive dimensions, widens before adding endpoints, clamps the far endpoints to `INT32_MAX`, and rejects empty results (`:25-36`):

```cpp
const int64_t x2 = std::min<int64_t>(sc<int64_t>(x) + w, INT32_MAX); // :29
```

Thus the cited damage endpoint path does not perform the claimed kind of unchecked `int32_t` addition, and a nonpositive scale does not reach later division/modulo. This does not validate independent viewporter/region coordinate conversions outside the control's scope. No fix is indicated for these guarded operations.

### Subsurface ancestry — partially; Low functional defect, unproven security escalation

**Evidence — source-confirmed.** `src/protocols/core/Subcompositor.cpp:160-190` checks live surface/parent, direct self-parenting, unassigned child role, and top-level-parent equality. Both `posRelativeToParent` and `t1Parent` have visited vectors (`:119-150`). However, both loop bodies select the role from the fixed initial parent rather than the advancing cursor:

```cpp
auto subsurface = sc<CSubsurfaceRole*>(m_parent->m_role.get())->m_subsurface.lock(); // :130 and :147
```

The condition tests `surf->m_role` (`:128,145`), while the body reads `m_parent->m_role`. On a normal sufficiently deep acyclic subsurface chain, advancing beyond the immediate parent causes the body to revisit that same parent's parent; the visited check eventually terminates with an intermediate ancestor instead of the actual top-level surface. The position traversal can likewise add the same offset more than once. This is a source-confirmed functional bug on valid nested client surfaces, not evidence that the visited lists correctly traverse the whole graph. `t1Parent` participates in parent admission (`:182-188`) and core commit bookkeeping (`src/protocols/core/Compositor.cpp:175-179`); offset traversal participates in surface-tree enumeration (`Compositor.cpp:455-463`).

**Boundary.** The dossier expressly left a cycle-check bypass unconstructed. No end-to-end bypass or compositor hang is promoted here, and this protocol-subsurface graph does not prove a `CPopup` parent cycle. Minimal fix: use the advancing `surf` role, validate locked parent/subsurface pointers at each hop, preserve visited detection, and add a bounded valid multilevel-ancestry regression test. No nested-surface client was run. The rating concerns wrong ancestry/geometry; any security escalation needs separate evidence.

### Deferred configure lifetime — confirmed; Informational

**Evidence — source-confirmed.** Toplevel state application and XDG configure scheduling use `doLaterLock` (`src/protocols/XDGShell.cpp:471-488,660-668`), owned by `UP<SEventLoopDoLaterLock>` members (`src/protocols/XDGShell.hpp:170,227`). Their captured `this` therefore has an associated cancellation owner. The lock's destructor calls `removeDoLater(seq)` (`src/managers/eventLoop/EventLoopManager.cpp:25-27`), which erases the queued entry and removes an empty idle source (`:240-250`). Resource destruction destroys the unique lock. A deferred callback surviving its resource is not established from these cited paths. No fix is indicated by this control; unrelated callbacks are outside this conclusion.

### Foreign-toplevel request inventory and lifetime — partially; Informational

**Evidence — source-confirmed.** `src/protocols/ForeignToplevel.cpp:5-33` registers handle destroy and manager destroy/stop. WLR foreign-toplevel registers activation (`src/protocols/ForeignToplevelWlr.cpp:23-33`), fullscreen/unfullscreen (`:35-65`), maximize/unmaximize (`:67-83`), minimize/unminimize (`:85-109`), and close (`:111-118`), as well as destroy (`:20-21`). This contradicts the dossier's statement that destroy/fullscreen/unfullscreen are the only client requests.

The state-changing WLR callbacks first lock/check `m_window` (`:24-27,36-39,59-62,68-71,77-80,86-92,99-105,112-115`). Fullscreen additionally handles nullable output, validates the resolved output resource, and checks monitor expiry (`:41-50`). For these cited request paths, a destroyed window stops processing rather than dereferencing a missing window. The WLR protocol intentionally exposes controls over other advertised windows to clients allowed to bind its global; that design alone is not a new isolation defect. Ordinary unsandboxed clients can see privileged globals, whereas security-context sandboxed clients are filtered (`src/Compositor.cpp:267-271`, `src/managers/ProtocolManager.cpp:348-403`), and foreign-toplevel/layer-shell are absent from the unprivileged whitelist.

No exploitable failure is established by this control, and no fix is indicated beyond correcting the request inventory in the dossier. This is a targeted verification of the cited handlers, not a blanket proof that all foreign-toplevel code is safe.

## Verification artifacts and limits

Scratch directory: `/home/wilf/data/.claude-scratch/agent-reports/work/06-protocols-shell/`.

- `dependency_controls.c` and `dependency_controls.log`: bounded pixman canonicalization controls succeeded; synthetic libwayland decoder setup was unavailable with `EPERM`, before any handler dispatch.
- `region_controls.cpp` and `region_controls.log`: actual installed hyprutils `CRegion` canonicalization controls succeeded with the same one/128 rectangle counts.
- `xdg-shell.cpp/.hpp` and `wlr-layer-shell-unstable-v1.cpp/.hpp`: generated with installed hyprwayland-scanner 0.4.6; inspection confirms preservation of XML object-interface/nullability metadata and wrapper destruction semantics for that generator.
- Installed versions checked: wayland-server 1.26.0, pixman 0.46.4, hyprutils 0.14.2. These describe inspection dependencies, not a running desktop's loaded libraries.

The upstream HEAD was verified as the exact baseline. No complete Hyprland build, compositor instance, Wayland desktop connection, input capture, GPU/fence test, or visual acceptance test was performed. High-impact conclusions are labeled source-confirmed and must not be represented as runtime reproductions. No repository source, README, dossier, other report, or Git state was changed by this verification.

