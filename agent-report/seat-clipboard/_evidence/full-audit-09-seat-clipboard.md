# Seat, clipboard, drag-and-drop, and output-management verification

- Baseline: official upstream `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`), verified with `git rev-parse HEAD`.
- Dossier: [09-seat-clipboard](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md).
- Date: 2026-10-15.
- Method: read-only upstream source review, local protocol XML and installed dependency headers. Evidence is **source-confirmed** unless explicitly marked **executed**. No compositor, live-session client, desktop IPC, full build, or network operation was run. An isolated library metadata probe was executed; a separate unnamed-socketpair guard control was blocked before dispatch. Source paths and line numbers below refer to the pinned upstream tree. No exploit clients or flooding tests were constructed.
- Scope: all 15 dossier records, including the informational control. Runtime failure, physical display behavior, victim-application behavior, and backend allocation effects are not implied by static evidence.

## Summary

Verdict totals: **5 confirmed, 9 partially, 1 rejected, 0 inconclusive**. The confirmed total includes the informational control F-SEAT-15. High availability/memory-safety concerns are F-SEAT-03 and the conditional CPU-path portion of F-SEAT-11; neither was run against a compositor.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-SEAT-01 | confirmed | Medium | `src/protocols/core/DataDevice.cpp:248`; `src/protocols/PrimarySelection.cpp:114` | Background selection replacement confirmed; this does not itself disclose the previous clipboard contents. |
| F-SEAT-02 | confirmed | Medium | `src/protocols/core/DataDevice.cpp:264` | Drag activation ignores implicit-grab serial and origin focus; downstream drop still depends on input and target behavior. |
| F-SEAT-03 | confirmed | High | `src/protocols/core/DataDevice.cpp:737`; `src/managers/SeatManager.cpp:438` | Post-drop source retention keeps dndActive true and suppresses client pointer buttons until explicit cleanup. |
| F-SEAT-04 | partially | Low; victim DoS unproven | `src/protocols/core/DataDevice.cpp:44`; `src/protocols/core/DataDevice.cpp:187` | Repeated forwarding exists; repeat receives are legal, MIME validation exists, and victim exhaustion is unproven. |
| F-SEAT-05 | partially | Medium | `src/protocols/OutputManagement.cpp:351`; `src/output/Monitor.cpp:2612` | Test is a success stub and dimensions reach swapchain setup; backend checks exist, so OOM and overflow remain unverified. |
| F-SEAT-06 | partially | Medium | `src/Compositor.cpp:267`; `src/protocols/OutputManagement.cpp:298` | No lock gate for unsandboxed output-management clients; sandbox visibility is restricted, and authentication bypass is unproven. |
| F-SEAT-07 | partially | Medium | `src/protocols/OutputManagement.cpp:20`; `src/protocols/OutputManagement.cpp:351` | Stale serial and stopped-manager requests lack checks; concurrent objects alone are not a protocol violation or demonstrated DoS. |
| F-SEAT-08 | confirmed | Medium | `src/protocols/OutputManagement.cpp:438`; `src/protocols/OutputManagement.hpp:74` | Mode-to-head ownership is unchecked; foreign same-client mode dimensions reach the other head’s rule, with backend testing still present. |
| F-SEAT-09 | partially | Low; numeric robustness | `src/protocols/OutputManagement.cpp:489`; `src/state/MonitorPositionController.cpp:40` | Overlap prohibition and signed-16-bit limits are unsupported; extreme accepted coordinates can exceed integer extents during automatic arrangement. |
| F-SEAT-10 | partially | Low | `src/managers/SeatManager.cpp:678`; `src/protocols/core/Seat.cpp:119` | Serial validation is disabled, but current client focus is enforced; focus-loss cursor injection is unsupported. |
| F-SEAT-11 | partially | High (conditional CPU-path memory safety) | `src/protocols/core/Compositor.cpp:788`; `src/pointer/PointerManager.cpp:648` | C8/RGB565 explanation rejected; CPU cache mutation confirmed, with an unchecked padding tail in channel swapping. |
| F-SEAT-12 | partially | Medium | `src/managers/SeatManager.cpp:507`; `src/protocols/core/Seat.cpp:27` | Shared touch focus and destroy listeners can misroute contacts; physical multi-touch is required, and permanent lock underflow is unproven. |
| F-SEAT-13 | rejected | None; single-seat design | `src/protocols/core/DataDevice.cpp:369`; `src/managers/ProtocolManager.cpp:360` | Seat is required by typed wire validation; ignored seat storage is single-seat routing, while privileged clipboard-manager delivery is intentional. |
| F-SEAT-14 | partially | Medium | `src/managers/SeatManager.cpp:134`; `src/protocols/XDGActivation.cpp:35` | Latest enter serial bypasses consumption for repeated token creation; normal serials are consumed, motion adds none, and move/resize use stronger validation. |
| F-SEAT-15 | confirmed | Informational | `src/protocols/core/DataDevice.cpp:187`; `src/managers/SeatManager.cpp:697` | Pure Wayland selection transfer forwards file descriptors without caching payload; XWayland buffering remains a separate audit boundary. |

## F-SEAT-01

**Verdict: confirmed. Revised severity: Medium.**

**Claim.** Ordinary selection setters accept requests without checking sender focus or the triggering serial.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:248-262` declares `uint32_t serial` but calls `g_pSeatManager->setCurrentSelection(source)` without using either serial or requester identity; the null-source branch also clears selection without authorization. `src/protocols/PrimarySelection.cpp:114-136` has the same omission, behind `misc:middle_click_paste`. Its default is `true` (`src/config/values/ConfigValues.cpp:594`).

```cpp
// DataDevice.cpp:259-261
source->markUsed();
g_pSeatManager->setCurrentSelection(source);
```

**Path and reachability — source-confirmed.** A connected client's ordinary data-device selection request reaches the setter above, then `src/managers/SeatManager.cpp:697-723`: the old source is cancelled, the global weak selection reference is replaced, and each selection protocol is notified. Core delivery selects the current keyboard-focus client (`DataDevice.cpp:505-520`), rather than authorizing the sender. Primary replacement follows `SeatManager.cpp:726-752` and is distributed using pointer focus (`PrimarySelection.cpp:292-307`). No additional permission or focus guard occurs in these paths. Core and primary globals are available even to security-context sandbox clients (`src/managers/ProtocolManager.cpp:361-387`; `src/Compositor.cpp:267-271`). Valid typed resources still belong to the requesting connection; cross-client object forgery is unnecessary.

**Impact and corrections.** Medium: cross-client clipboard integrity and availability loss, with downstream paste behavior controlled by the receiving application and user. A source's `send` callback receives a destination descriptor for its own offered data; replacement does **not** return the previous owner's clipboard data. The dossier's direct sniffing claim and implied command execution are unsupported. The primary protocol describes keyboard-focus delivery (`/usr/share/wayland-protocols/unstable/primary-selection/primary-selection-unstable-v1.xml:45-50`), so the dossier's assertion that pointer-focus authorization is universally the reference requirement is too simple. No runtime paste was tested.

**Minimal fix direction.** Authorize ordinary selection changes against the appropriate seat interaction/focus and validate serial freshness/order with protocol-compatible rules, including clear requests. Keep explicitly privileged clipboard-manager behavior separate; avoid a universal consume-once rule for unrelated serial uses.

## F-SEAT-02

**Verdict: confirmed. Revised severity: Medium.**

**Claim.** Drag initiation fails to validate the active implicit grab and its origin/serial.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:264-279` resolves the supplied source, warns on reuse, marks it as DnD, and invokes `initiateDrag`; `serial` is unused. There is no focus, active-button, or origin-grab check. The core protocol requires an active implicit grab matching the origin and serial (`/usr/share/wayland/wayland.xml:945-947`).

```cpp
// DataDevice.cpp:276-278
source->m_dnd = true;
PROTO::data->initiateDrag(source, icon ? CWLSurfaceResource::fromResource(icon) : nullptr,
                          CWLSurfaceResource::fromResource(origin));
```

**Path and reachability — source-confirmed.** An ordinary client request with live same-client typed resources reaches `initiateDrag` (`DataDevice.cpp:557-664`), installs a cursor override, assigns `currentSource`, and clears pointer focus while retaining DnD focus. `updateDrag` resolves the actual DnD-focused surface/device and offers data (`:666-712`). No implicit-grab validation is inserted downstream. A missing target prevents offer delivery (`:673-680`), but does not authorize initiation. Input release listeners (`:595-611`) call `dropDrag`; success additionally depends on an accepted/received offer (`:737-778`).

**Impact and corrections.** Medium for unauthorized global input/drag-state interference; combine with F-SEAT-03 only when separately assessing that availability defect. Unsolicited state activation is source-confirmed; arbitrary file reads, copying protected files, or application command execution are not. A client cannot place a normal surface arbitrarily using `set_cursor`; target selection still follows compositor hit testing. No input or target application was exercised. The existing button-serial facility (`src/managers/SeatManager.cpp:156-204`, recorded/cleared by `src/protocols/core/Seat.cpp:239-245`) is useful evidence that a suitable validation mechanism exists, not evidence that drag currently calls it.

**Minimal fix direction.** Validate the matching origin-bound active implicit grab before any DnD state mutation, covering supported pointer, touch, and tablet initiation. Validate icon role and legitimate null-source semantics independently; do not require pointer-only focus for every input modality.

## F-SEAT-03

**Verdict: confirmed. Revised severity: High.**

**Claim.** After a successful Wayland drop, a destination that never finishes can keep the compositor's ordinary pointer-button delivery disabled.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:737-766` sends drop and leave, removes the visual override, then calls `cleanupDndState(false, false, false)`. That function removes input listeners but preserves both current source and focused device (`:715-734`). The activity predicate is solely `return !!m_dnd.currentSource` (`:852-854`).

```cpp
// SeatManager.cpp:438-440
if (!m_state.pointerFocusResource || (PROTO::data && PROTO::data->dndActive()))
    return;
```

**Path and preconditions — source-confirmed.** An established drag with a live source and Wayland destination passes `wasDragSuccessful` when a non-dead matching offer has either `m_recvd` **or** `m_accepted` (`DataDevice.cpp:769-778`). On the ordinary release-driven drop path, the retained source keeps the activity predicate true; subsequent `CSeatManager::sendPointerButton` calls return before any client button event. `offer.finish` normally invokes complete/abort (`:71-76`); `completeDrag` resets state (`:789-798`). There is no timeout in this state machine. Source destruction aborts (`:478-480`), and the references are weak (`src/protocols/core/DataDevice.hpp:175-179`, `m_dnd.currentSource`); the source must remain alive. X11 destinations explicitly clear state (`DataDevice.cpp:751-758`) and are excluded.

**Recovery and corrections — source-confirmed.** `abortDndIfPresent` calls `abortDrag` (`:856-859`); Escape handling calls it at `src/keybinds/Manager.cpp:212-213`, before ordinary hotkey handling; keyboard eligibility/translation guards precede it (`:198-205`). Starting another drag first aborts the old one (`:559-562`). Merely changing DnD focus calls `updateDrag` (`:547-554`), which does not clear the source, so the dossier's suggestion that focus change necessarily recovers is incorrect. Offer destruction sends DnD-finished to the source (`:80-85`) but does not directly reset this compositor state. This is a source-closed state-retention and button-suppression defect, not an executed desktop freeze.

**Severity.** High: a local client participating in DnD can withhold completion and cause desktop-wide loss of normal Wayland pointer buttons; compositor shortcuts and explicit recovery remain possible. It is not a compositor crash or an irrevocable lockout.

**Minimal fix direction.** End the input grab at drop independently of transfer lifetime. Preserve only transfer bookkeeping until finish, with cancellation on endpoint destruction and bounded cleanup. Do not keep the global input-suppression predicate true while awaiting destination cooperation.

## F-SEAT-04

**Verdict: partially. Revised severity: Low; victim DoS unproven.**

**Claim.** Repeated receive requests and automatic acceptance cause descriptor/event flooding, allegedly violating one-receive semantics and allowing arbitrary MIME requests.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:44-65` checks source liveness and dead state, optionally notifies acceptance, and forwards a descriptor every time. `m_recvd` records transfer activity rather than enforcing a quota. Equivalent forwarding exists at `src/protocols/DataDeviceWlr.cpp:14-28`, `src/protocols/ExtDataDevice.cpp:14-28`, and `src/protocols/PrimarySelection.cpp:15-29`.

```cpp
// DataDevice.cpp:187-193
if (std::ranges::find(m_mimeTypes, mime) == m_mimeTypes.end()) {
    // log and return
    return;
}
m_resource->sendSend(mime.c_str(), fd.get());
```

**Guards and protocol correction — source-confirmed.** Source implementations validate MIME membership before emitting `send` (`DataDevice.cpp:175-193`; `DataDeviceWlr.cpp:80-86`; `PrimarySelection.cpp:81-87`; `ExtDataDevice.cpp:80-86`). The compositor wraps the received fd in `CFileDescriptor`, so the local receive-handler descriptor is not simply leaked. `/usr/share/wayland/wayland.xml:626-629` explicitly permits multiple receives and preemptive examination before acceptance/drop. The WLR and ext data-control XML also permit multiple receives (`protocols/wlr-data-control-unstable-v1.xml:246-257`; installed `ext-data-control-v1.xml:244-255`). A blanket one-receive limit would break legitimate behavior. Automatic source acceptance is real, but lack of prior acceptance alone is not proof of an invalid receive.

**Path and limits — source-confirmed.** A client already holding a live offer can request an advertised MIME repeatedly; each request reaches the underlying source's send event. Ordinary core offers are focus-routed and invalidated on keyboard focus changes (`DataDevice.cpp:537-544`). F-SEAT-01 replacement does not itself grant a background client an offer for the old selection. Clipboard-manager protocols deliberately receive selections but have security-context visibility restrictions (F-SEAT-13). The exact effect on a victim depends on libwayland queue backpressure and the source application's descriptor handling; no victim process or transport saturation was executed. Default descriptor limits, guaranteed source crash, and unbounded compositor fd growth are not established.

**Severity and minimal fix direction.** Low for the residual cross-client request-amplification/hardening concern; the dossier's Medium victim-exhaustion endpoint remains unproven. If measurements in an isolated test environment establish harmful amplification, bound outstanding transfers and enforce fair per-client service while preserving legal multiple-MIME transfers. Keep MIME validation and descriptor ownership intact.

## F-SEAT-05

**Verdict: partially. Revised severity: Medium.**

**Claim.** Custom-mode dimensions/refresh are insufficiently bounded, protocol test always succeeds, and applying large modes necessarily causes huge allocation or integer overflow.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:462-484` rejects non-positive dimensions and negative refresh only; zero refresh uses the current rate. `:351-355` is explicitly a test stub:

```cpp
if (test) {
    LOG(Log::WARN, "TODO: STUB: applyTestConfiguration for test not implemented, returning true.");
    return true;
}
```

`test` sends succeeded from that result (`:326-332`). Custom size/rate are saved at `:388-391`; `src/config/shared/monitor/MonitorRuleManager.cpp:55-59` copies them into the monitor rule, converting mHz to Hz correctly.

**Path and backend boundaries — source-confirmed.** Configuration input reaches the saved override, scheduled reload (`OutputManagement.cpp:422-424`), monitor rule application (`MonitorRuleManager.cpp:133-173`), and requested-mode selection (`src/output/Monitor.cpp:786-807`). Mode application calls `applyModeWithSwapchain` or `applyCustomModeWithSwapchain` (`:846-865`, `:887-905`). Both update the swapchain (`:2643-2650`); `updateSwapchain` passes mode size to `swapchain->reconfigure` (`:2621-2641`). Thus a requested size reaches allocation-related setup; it is not simply rejected by an EDID-membership guard before that setup.

However, `CMonitorState::test` also checks swapchain update and calls the backend's output test (`Monitor.cpp:2612-2618`). The mode-selection loop rejects failed tests and tries fallbacks (`:852-865`, `:908-936`). Protocol `test` success does not bypass these later tests, and clients are not required to call protocol `test` before `apply` in the first place.

**Preconditions and impact.** The output-management global is unavailable to security-context sandbox clients, but ordinary unmarked socket clients can bind it (`src/Compositor.cpp:267-271`; `src/managers/ProtocolManager.cpp:348-403`). Medium for false validation success and missing resource-policy bounds before swapchain setup. Exact allocation success, memory pressure, backend limits, and any arithmetic overflow are **not verified**; resolving those requires an isolated backend/allocator test with bounded resources or a separately traced concrete arithmetic sink. No universal wlroots 8192-pixel/1000-Hz rule was established from available source; the protocol XML specifies no such constants (`protocols/wlr-output-management-unstable-v1.xml:556-566`). The dossier's unconditional High OOM/UB conclusion is not supported.

**Minimal fix direction.** Implement real side-effect-free protocol testing, validate complete configuration against backend capabilities and allocation budgets, and check dimensions/derived allocation sizes before swapchain setup. Report failure consistently and retain backend fallback behavior.

## F-SEAT-06

**Verdict: partially. Revised severity: Medium.**

**Claim.** Any client can reconfigure or disable displays while locked because output management lacks all permission/session-lock checks.

**Evidence — source-confirmed.** The global is registered unconditionally at `src/managers/ProtocolManager.cpp:195`; `src/protocols/OutputManagement.cpp:586-595` binds without a session-lock check. The configuration handlers and apply path likewise have no lock check (`:269-344`, `:351-429`). In particular:

```cpp
// OutputManagement.cpp:319-323
newState.enabled = false;
Config::monitorRuleMgr()->scheduleReload();
m_owner->m_monitorStates[PMONITOR->m_name] = newState;
```

**Path and guards — source-confirmed.** A client allowed to bind the global can change saved output state during an existing lock. The override sets `rule.m_disabled` (`src/config/shared/monitor/MonitorRuleManager.cpp:43-59`), reload applies the rule (`:133-186`), and `src/output/Monitor.cpp:699-711` sets output disabled and commits. No intervening session-lock authorization appears on that path. Notably disable is saved and reload scheduled **in the disable handler itself**, before apply; this is stronger evidence of non-transactional behavior than the dossier's apply-only description. Existing overrides from earlier managers can take precedence because `getOutputStateFor` returns the first matching manager (`OutputManagement.cpp:644-649`), so success is configuration-dependent.

The claim of no permission boundary is false: `src/Compositor.cpp:267-271` filters globals for clients recorded as sandboxed, and output management is absent from the allowlist (`src/managers/ProtocolManager.cpp:360-403`). Sandbox classification is an explicit tracked-client list (`src/protocols/SecurityContext.cpp:221-222`), not a claim that every externally sandboxed application is automatically recognized. An ordinary unmarked local connection can access this privileged display-control protocol.

**Impact and corrections.** Medium for lock-time display-availability/policy exposure by an unsandboxed client. Physical black-screen behavior and inability to unlock were not tested; fallback-monitor machinery exists (`src/state/FallbackState.cpp:27-39`, `:100-139`). This does not prove authentication bypass, password capture, or exposure of unlocked application content. Display control is the protocol's intended authority (`protocols/wlr-output-management-unstable-v1.xml:42-70`), so availability policy should be distinguished from an arbitrary-client memory-corruption flaw.

**Minimal fix direction.** Establish explicit policy for display changes while locked, enforce it at bind and mutation/commit as appropriate, and stage disable changes until an authorized atomic apply. Preserve legitimate trusted display-management clients and backend hotplug handling.

## F-SEAT-07

**Verdict: partially. Revised severity: Medium.**

**Claim.** Configuration freshness is unchecked, stopped managers remain usable, and unlimited concurrent configurations allegedly violate a one-configuration rule and cause memory DoS/stale-head races.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:22-35` receives `serial` but neither stores nor validates it. `sendDone` generates and immediately sends a serial without retaining it (`:86-88`). `COutputConfiguration` stores an owner and heads, not the requested state serial (`src/protocols/OutputManagement.hpp:132-147`).

```cpp
// OutputManagement.cpp:20, 87
m_resource->setStop([this](CZwlrOutputManagerV1* r) { m_stopped = true; });
m_resource->sendDone(wl_display_next_serial(g_pCompositor->m_wlDisplay));
```

`m_stopped` gates advertising new heads (`:51-53`), but does not gate create, configure, test, or apply. The protocol says the client must stop sending manager requests after stop (`protocols/wlr-output-management-unstable-v1.xml:108-114`); current code does not enforce that lifecycle. It also describes cancellation when output state changed and the client has outdated information (`:507-514`), which cannot be implemented correctly with the received serial discarded.

**Path and reachability — source-confirmed.** An allowed unsandboxed manager's configuration request allocates a protocol-owned object (`OutputManagement.cpp:25-34`), whose later apply directly saves monitor state and schedules reload (`:351-429`). No stored freshness check intervenes, including after a later done/hotplug event. For directly disappeared heads, monitor-destroy resets the head monitor (`:97-115`), and enable/disable reject an absent monitor (`:277-281`, `:306-310`); apply also skips an expired configuration-head monitor (`:370-373`). These guards refute treating every stale object as a dereference of freed memory. No concurrent thread race was established; event-driven stale configuration is the supported mechanism.

**Corrections and severity.** Medium for freshness/lifecycle violations that allow stale display-state mutations. The local protocol XML imposes complete-head and single-use transaction rules (`:414-435`, `:456-481`), not a blanket one-live-configuration-per-manager rule. Resource vectors grow while clients retain objects, but this alone is ordinary Wayland resource accounting, not a demonstrated special memory-amplification vulnerability; destruction erases configurations (`OutputManagement.cpp:610-611`). Actual resource exhaustion was not executed. As in F-SEAT-06, this global is filtered for security-context sandbox clients.

**Minimal fix direction.** Store a configuration snapshot/generation, cancel stale transactions on relevant output changes, enforce manager stop and configuration single-use states, and release child state with its transaction. Consider quotas separately from protocol correctness; do not invent a one-concurrent-configuration requirement.

## F-SEAT-08

**Verdict: confirmed. Revised severity: Medium.**

**Claim.** A mode belonging to one output head can be assigned to another without the required invalid-mode rejection.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:438-459` resolves a mode by resource identity globally (`:635-641`), checks that its backend mode and destination monitor are live, checks duplicate property state, then stores it. There is no head-identity comparison:

```cpp
// OutputManagement.cpp:456-457
m_state.committedProperties |= OUTPUT_HEAD_COMMITTED_MODE;
m_state.mode = MODE;
```

`COutputMode` stores only the resource and weak backend mode (`src/protocols/OutputManagement.hpp:74-85`); it cannot directly prove head ownership. Mode creation comes from each head (`OutputManagement.cpp:214-226`), but that relationship is not consulted by `set_mode`. The XML explicitly defines `invalid_mode` as a mode not belonging to the head (`protocols/wlr-output-management-unstable-v1.xml:539-553`).

**Path and preconditions — source-confirmed.** An unsandboxed client permitted to bind output management and holding valid mode resources for two distinct heads can supply a mode through a different head's configuration. Wayland interface validation does not enforce this semantic ownership relation: both resources have the correct protocol interface and belong to that same client. Apply reads the foreign mode's size/refresh (`OutputManagement.cpp:383-387`) and stores them under the destination monitor name (`:424`), then `src/config/shared/monitor/MonitorRuleManager.cpp:55-59` puts them into that monitor's rule. Both mode and monitor must still be live for this ordinary path; this finding does not rely on expiration behavior.

**Severity and corrections.** Medium for a concrete protocol ownership/integrity defect. Actual output uses the destination's mode-selection and backend test/fallback machinery (`src/output/Monitor.cpp:786-807`, `:846-936`), so the foreign backend mode object is not simply passed through as another monitor's EDID capability. Forced no-signal behavior or a stable desktop DoS was not verified. The dossier's `src/protocols/core/OutputManagement` prefix is wrong; the file is directly under `src/protocols`.

**Minimal fix direction.** Associate each advertised mode with its owning head/monitor and compare that identity before accepting it. Reject foreign modes with the specified `invalid_mode` error; retain backend validation for legitimate custom modes.

## F-SEAT-09

**Verdict: partially. Revised severity: Low; numeric robustness.**

**Claim.** Accepting overlapping or arbitrary positions is intrinsically invalid, with extreme coordinates causing layout arithmetic failures.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:489-503` stores signed 32-bit wire coordinates in `Vector2D`; apply copies them (`:395-398`), and the monitor rule override copies the offset (`src/config/shared/monitor/MonitorRuleManager.cpp:62-65`). Position acceptance is real:

```cpp
// OutputManagement.cpp:500-501
m_state.committedProperties |= OUTPUT_HEAD_COMMITTED_POSITION;
m_state.position = {x, y};
```

**Protocol correction — source-confirmed.** `protocols/wlr-output-management-unstable-v1.xml:569-574` defines signed integer global coordinates without an overlap prohibition or signed-16-bit bound. Its head error enumeration (`:539-546`) contains no `INVALID_POSITION`. Core output geometry itself uses signed integer coordinates (`/usr/share/wayland/wayland.xml:2969-2972`, `wl_output.geometry`), not the dossier's claimed signed-16-bit limit. Upstream intentionally applies explicit positions and reports overlap as a warning (`src/state/MonitorPositionController.cpp:21-34`; `src/state/MonitorLayoutController.cpp:40-53`). Merely configuring an overlap is therefore not enough to establish this security finding.

**Residual numeric path — source-confirmed.** Scheduled reload ends in arrangement (`MonitorRuleManager.cpp:193-194`; `MonitorLayoutController.cpp:56-68`). `CMonitor::explicitPosition` returns the configured offset except for its auto-placement sentinel (`src/output/Monitor.cpp:1219-1223`); `moveTo` stores it (`:1648-1654`). For remaining automatically positioned monitors, `MonitorPositionController.cpp:40-56,61-62` accumulates right/bottom extents in **int** variables using `std::max<double>(position + size, oldExtent)`. An accepted coordinate near the positive signed-int limit plus a normal positive monitor extent can exceed that accumulator's range. Conversion back to int is not guarded. This establishes a numeric robustness defect when explicit and automatic placement coexist; it is not evidence that the original position assignment overflows (that assignment uses floating-point vectors).

**Severity and limits.** Low for the narrowed range-handling issue in privileged output configuration. No compositor crash, trapped focus, or lock bypass was executed or inferred from the range issue alone. A configuration where every monitor is explicitly placed does not run this particular auto-placement accumulator loop. The general overlap/forbidden-layout claim is rejected; the numeric-extents subclaim is retained with the specific sink above.

**Minimal fix direction.** Keep position and extent arithmetic in a sufficiently wide representation, validate conversion ranges including output size, and handle unsupported extreme layouts cleanly. Do not add a fictitious protocol error or prohibit all overlaps solely on this dossier's authority.

## F-SEAT-10

**Verdict: partially. Revised severity: Low.**

**Claim.** Disabled cursor serial checks allegedly permit stale-serial cursor changes even after pointer focus has left the client.

**Evidence — source-confirmed.** `src/protocols/core/Seat.cpp:119-137` resolves the cursor surface, checks its role, and calls `CSeatManager::onSetCursor`. At `src/managers/SeatManager.cpp:678-690`, serial validation is commented out, but an active guard remains:

```cpp
if (!m_state.pointerFocusResource || !seatResource ||
    seatResource->client() != m_state.pointerFocusResource->client())
    return;
// serialValid(...) is commented out
m_events.setCursor.emit(SSetCursorEvent{surf, hotspot});
```

**Path and reachability — source-confirmed.** A client's cursor request reaches this handler; an arbitrary/obsolete serial is accepted only if the requesting seat resource's client is the current pointer-focus client. Once focus belongs to another client, the handler returns before the cursor event. There is no demonstrated interleaving that bypasses this synchronous comparison; merely calling the request close to a focus change is not a closed race argument. Libwayland resource ownership also prevents substituting another client's pointer or surface object. Ordinary clients can legitimately choose, hide, and animate their own focused cursor.

**Protocol correction.** `/usr/share/wayland/wayland.xml:2186-2188` requires the **latest pointer-enter serial**, not an arbitrary button serial and not single-use authorization. `CWLPointerResource::sendEnter` records that serial via `nextSerial(..., true)` (`Seat.cpp:182`), but the shared seat-level serial store can be overwritten across multiple pointer resources. Blindly restoring generic `serialValid(..., erase=true)` is not the correct fix. Focused clients do not need a new button event for each cursor update.

**Severity and minimal fix direction.** Low for a real conformance/freshness defect; cross-client cursor-image injection after focus loss and phishing impact are unproven. Track each pointer resource's latest enter serial and validate it together with appropriate current focus/current-cursor rules. Preserve repeated legal updates using that serial. No visual or live focus-race test was run.

## F-SEAT-11

**Verdict: partially. Revised severity: High (conditional CPU-path memory safety).**

**Claim.** Cursor SHM copies assume four bytes per pixel without validation, large cursors amplify resources, and the CPU cursor path mutates stored color channels each render.

**Evidence and guarded copy path — source-confirmed.** A cursor-role surface commit updates synchronous texture data and then cursor SHM (`src/protocols/core/Compositor.cpp:659-673`). Assigning the cursor role also calls this updater (`src/protocols/core/Seat.cpp:127-134`). `updateCursorShm` checks SHM availability, clips damage to buffer size, obtains `bufLen`, and resizes the cache (`Compositor.cpp:788-810`). Full damage copies `bufLen`; partial damage uses `4 * x` and `shmAttrs.stride` (`:815-824`).

The omitted caller-side checks matter: `src/protocols/core/Shm.cpp:144-156` accepts only advertised formats and validates the layout using `NFormatUtils::isShmBufferLayoutValid`. The advertised list contains only 32-bit formats (`:220-231`), not C8 or RGB565. Layout validation checks minimum stride, multiplication/addition bounds, and pool containment (`src/helpers/Format.cpp:53-80`). `beginDataPtr` returns `stride * height` (`Shm.cpp:65-66`). Therefore the dossier's non-32-bit SHM partial-copy overread is not reachable through these advertised core SHM buffers at this baseline. The CPU hardware path additionally rejects formats other than ARGB8888/ABGR8888 (`src/pointer/PointerManager.cpp:627-636`).

**Color mutation — source-confirmed.** With CPU cursor buffers selected, an empty texture data copy leads to the role-cache fallback (`PointerManager.cpp:617-623`). Ordinary synchronous surface textures are created without `keepDataCopy` (`src/protocols/types/SurfaceState.cpp:58-67`; default `false` in `src/render/Renderer.hpp:190`), so this fallback is meaningful. The exact order is:

```cpp
// PointerManager.cpp:640-649
texData = shmBuffer; // a vector copy, conditional on shmBuffer.data()
// ...
for (size_t i = 0; i < shmBuffer.size(); i += 4)
    std::swap(shmBuffer[i], shmBuffer[i + 2]);
```

The current output copy has **not** been converted by the subsequent swap. Instead the role cache is changed, so another render without refreshed SHM can alternate the channel ordering. This agrees with the existing tracker side-finding correction, independently rechecked at this baseline. `cursorPixelData` returns the role's compositor-owned vector (`Seat.cpp:623-627`; `Seat.hpp:31-42`), not a writable view of client SHM. The dossier's implied shared client/render buffer is incorrect.

**Cursor activation/render path — source-confirmed.** The current-focus guard is `src/managers/SeatManager.cpp:678-690` (F-SEAT-10). Its cursor event reaches `src/managers/input/InputManager.cpp:100,769-792`, with cursor-override policy checked at `:789-790,808-809`, then `src/render/Renderer.cpp:3074-3085` and `src/pointer/cursor/CursorManager.cpp:141-147` forward the selected surface. `src/pointer/PointerManager.cpp:258-302` updates backend state on initial selection and later surface commits. Backend update skips disabled/DPMS-off monitors and nonintersecting cursors, and respects software-cursor locks/policy (`:402-428`); an eligible hardware attempt reaches `renderHWCursorBuffer` (`:483-500`) and the CPU fallback above. These are real preconditions, not a claim that every connected client can force this path on every GPU configuration.

**Additional padding-bound correction — source-confirmed.** The advertised-format guard does not prove every byte in the cache is part of a whole pixel. `Shm.cpp:65-66` sizes it from stride times height, while `Format.cpp:64-80` checks minimum stride and total containment but imposes no four-byte stride divisibility. The ABGR swap loop (`PointerManager.cpp:647-650`) accesses `i + 2` under only `i < shmBuffer.size()`. Therefore an accepted padded layout can leave a tail that this loop indexes outside the vector. No guard at cursor-cache copy, synchronous texture upload (`src/render/gl/GLTexture.cpp:32-65`), or CPU fallback establishes the missing tail bound. This is a narrower memory-safety issue in the same reviewed CPU mutation path, not the dossier's C8/RGB565 partial-copy mechanism. It is source-confirmed; no crashing input, sanitizer reproducer, or compositor execution was constructed.

**Preconditions and limits.** CPU mode is selected by `cursor:use_cpu_buffer` and GPU policy (`PointerManager.cpp:549-551`); both CPU-buffer and hardware-cursor policies default to auto (`src/config/values/ConfigValues.cpp:684,707`), and hardware cursors must actually be enabled/usable; hardware-plane size checks run before copying (`:553-564`), so arbitrary oversized cursors do not necessarily reach that hardware path. The earlier role-cache resize has no cursor-specific budget and can use substantial memory for a valid large SHM buffer. Nevertheless size/format/pool validation exists, small damage normally copies damaged rectangles rather than proving full-image copying on every commit, and no measured CPU/OOM threshold is available. CPU mode, compatible format, usable plane and cache fallback are required for the color bug; it is not a universal every-frame software-cursor defect.

**Severity and minimal fix direction.** High conditional on the CPU hardware-cursor fallback for the source-confirmed out-of-vector access; no compositor crash or exploitation beyond the invalid access is claimed. Channel-order corruption alone is Low, and unquantified allocation pressure remains a hardening concern. Normalize into a checked, tightly packed destination using actual source stride and width, bound all channel accesses to complete pixels, and preserve the source role cache. Keep format/layout validation and plane-size fallback; consider explicit allocation budgets. No GPU, physical display, or large allocation was exercised.

## F-SEAT-12

**Verdict: partially. Revised severity: Medium.**

**Claim.** Shared touch focus/listeners fail to represent multiple contacts, leading to lost/misrouted termination events and potentially corrupt touch-lock accounting.

**Evidence — source-confirmed.** `src/managers/SeatManager.cpp:507-529` resets one destroy listener and focus resource for every new down, stores the new surface, and increments a single count. It retains no contact-ID map. Termination resolves the client through the current shared focus (`:535-553`), irrespective of the original down's owner.

```cpp
// SeatManager.cpp:527, 539, 549, 553
m_listeners.touchSurfaceDestroy = surf->m_events.destroy.listen(
    [this, timeMs, id] { sendTouchUp(timeMs + 10, id); });
auto client = m_state.touchFocusResource->client();
t->sendUp(timeMs, id);
m_touchLocks--;
```

At the per-resource level, `src/protocols/core/Seat.cpp:27-56` likewise replaces one current surface/destroy listener per down and maintains only `m_fingers`. Its header confirms a single listener (`src/protocols/core/Seat.hpp:60-68`). No per-ID membership check protects `sendUp`.

**Path and preconditions — source-confirmed.** Physical/device touch events enter `src/managers/input/Touch.cpp:17-62`; gesture-consumed touches and invalid device/monitor cases are filtered (`:30-53`, `:71-103`). For delivered contacts, each down overwrites `m_touchData.touchFocus*` and calls `sendTouchDown` (`:106-147`). There is no first-contact focus latch for all subsequent non-gesture fingers. A later contact on another surface/client can replace the stored focus; a prior contact's eventual real up is forwarded through that later shared target (`Touch.cpp:173-174`; `SeatManager.cpp:539-549`). Thus a client can receive an up for an ID it never received, while the original recipient does not receive its matching up. Surface destruction replaces the one retained synthesized-up hook rather than tracking all live contacts. Physical multi-touch, or a separately authorized input source, is needed; ordinary clients do not send `wl_touch.down` (it is a server event).

**Corrections and severity.** Medium for multi-touch routing/lifecycle correctness affecting multiple clients under these preconditions. F-SEAT-02/03 DnD requests do not themselves manufacture touch contacts or establish this hardware precondition. A replaced destroy listener alone does not prove a permanently missing physical release: the eventual real release may still occur. `m_touchLocks` is a signed int guarded at zero (`SeatManager.hpp:197`; `SeatManager.cpp:536`); the dossier's precise permanent underflow/race sequence is not established, and callback ordering was not executed. The two distinct destroy callbacks can complicate duplicate termination, but that is not a demonstrated underflow. Synthetic timestamps using the original down time plus ten are confirmed; harmful client timing effects are unmeasured.

**Minimal fix direction.** Track live contacts by device/contact ID, original target and destruction listener; terminate each contact exactly once and derive lock counts from those records. Cancel appropriate contacts on target destruction and preserve gesture-consumption policy. Validate using isolated multi-client multi-touch tests; no live input test was run here.

## F-SEAT-13

**Verdict: rejected. Revised severity: None; single-seat design.**

**Claim.** Ignoring the seat callback argument allows a client that never binds a seat to create a data device, and thereby bypass selection access checks.

**Source-confirmed guard.** `src/protocols/core/DataDevice.cpp:369-387`, `src/protocols/DataDeviceWlr.cpp:180-198`, and `src/protocols/ExtDataDevice.cpp:180-198` do not store or inspect the seat argument. But `/usr/share/wayland/wayland.xml:1101-1106` declares it as a **non-null object of interface wl_seat**. It is not an untyped integer that Hyprland must independently resolve. Libwayland decodes typed objects from the requesting connection's object map before invoking the handler, disallowing null, unknown, wrong-interface, or another connection's objects. The generated server declaration is installed at `/usr/include/wayland-server-protocol.h:3142-3151`.

**Executed metadata control.** The isolated `seat_schema.c` probe in the designated scratch directory linked installed libwayland-server 1.26.0 and checked the actual exported method table:

```text
wl_data_device_manager.get_data_device signature=no arg1=wl_data_device arg2=wl_seat
```

`n` is the new-ID slot and `o` a non-null object slot; the method table explicitly types the latter as `wl_seat`. This verifies installed dispatch metadata, not a Hyprland runtime request. A separate unnamed-socketpair dispatcher control was compiled, but `wl_client_create` failed with `Operation not permitted` in this execution environment before any request dispatch. Its log is retained; **no runtime success of that negative-control test is claimed**.

**Path and semantic corrections — source-confirmed.** Upstream provides one `CWLSeatProtocol` global (`src/managers/ProtocolManager.cpp:163`; `src/protocols/core/Seat.hpp:21` names the seat), and routes devices by client. `dataDeviceForClient` deterministically returns the first matching live entry (`DataDevice.cpp:429-438`); that may deserve separate multi-resource protocol-conformance review, but is not nondeterministic cross-client routing. Core device creation does not immediately deliver an old selection (`:369-387`), and normal offers remain keyboard-focus-routed (`:523-534`). Clipboard-manager protocols do send initial selections (`DataDeviceWlr.cpp:198`; `ExtDataDevice.cpp:198`), which is their intended function; both globals are hidden from security-context sandbox clients (`src/Compositor.cpp:267-271`; `src/managers/ProtocolManager.cpp:360-403`). For permitted unsandboxed managers, requiring a stored seat pointer would not itself add confidentiality authorization.

**Verdict, severity, and fix direction.** Rejected as an access-control/no-seat bypass; no demonstrated security impact from ignoring a valid sole-seat resource. The proposed `WL_DATA_DEVICE_MANAGER_ERROR_INVALID_SEAT` does not exist in the core protocol specification. Explicit seat association would be appropriate for future multi-seat support and broadcasting to multiple same-client device resources merits separate compatibility review, but neither validates this dossier's claimed exploit.

## F-SEAT-14

**Verdict: partially. Revised severity: Medium.**

**Claim.** Stored enter serials can be replayed indefinitely; a 100-element store and high-frequency motion allegedly also make ordinary serials replayable across activation, warp, and move/resize.

**Evidence — source-confirmed.** `src/managers/SeatManager.cpp:112-131` stores one latest pointer-enter serial separately from ordinary serials. `serialValid` has an early return for that enter serial, while ordinary matched serials are erased when requested (`:134-153`). The default is `erase=true` (`src/managers/SeatManager.hpp:109`).

```cpp
// SeatManager.cpp:142-148
if (container->enterSerial == serial)
    return true;
// ordinary serial match:
if (erase)
    container->serials.erase(it);
```

The per-seat container initializes the special serial to zero and deliberately remembers it (`SeatManager.hpp:174`); the ordinary store cap is 100 (`:11`). Pointer enter updates it (`src/protocols/core/Seat.cpp:182`), and pointer leave does not clear it (`:185-204`).

**Path and preconditions — source-confirmed.** A client that has legitimately received a nonzero pointer-enter serial on a live seat resource can present that still-latest special serial during activation-token commit. `src/protocols/XDGActivation.cpp:35-46` uses the default consuming validator; its special branch nevertheless succeeds without consumption. The token handler does not separately check recent input time/current focus. Each successful commit can register a token; a single token resource rejects a second commit (`:25-33`), so the issue is reauthorization of distinct token creations, not recommitting one resource. Activation removes each issued token when used (`:83-102`), and sent-token bookkeeping has a per-client cap (`:48-59`). Those guards do not make the source serial fresh or consumed.

**Corrections — source-confirmed.** Ordinary stored serials are **not** reusable under default consuming validation. Pointer motion emits no serial (`Seat.cpp:207-216`), so motion alone cannot evict the ordinary store. Eviction can reject an older legitimate event serial but does not cause replay. Pointer warp explicitly calls `serialValid(..., false)` and independently requires exact pointer-focus surface and in-surface coordinates (`src/protocols/PointerWarp.cpp:28-31,56-76`); repeated serial use there is intentional and does not confer background/global warp authority. XDG move/resize use the separate surface-bound pointer-button validator (`src/protocols/XDGShell.cpp:202,227`; `SeatManager.cpp:185-204`), not this generic enter bypass. No active PointerConstraints call to `serialValid` was found. The actual activation API is token `set_serial`/`commit`, then manager `activate`, not the dossier's `request_activate` call.

**Severity and minimal fix direction.** Medium for stale interaction authorization in activation-token issuance; focus stealing policy effects require separate application/focus-path validation, and no live activation was attempted. Keep protocol-specific serial semantics: enforce recent qualifying interaction for token issuance, avoid treating a remembered enter serial as unlimited consuming authorization, and keep pointer-enter reuse valid for cursor operations. Increasing the ordinary ring size does not fix this distinction.

## F-SEAT-15

**Verdict: confirmed. Revised severity: Informational.**

**Claim.** There is no ClipboardManager payload cache in the core Wayland path, so an unbounded paste-payload allocation finding is not established in this domain.

**Evidence — source-confirmed.** A source-name search under `src` finds no `ClipboardManager` class/file. Core receive wraps the incoming descriptor and forwards it to `IDataSource::send` (`src/protocols/core/DataDevice.cpp:44-65`); the ordinary Wayland source validates MIME then emits `sendSend(mime, fd.get())` (`:187-193`). Primary and clipboard-manager Wayland sources follow the same pattern (`src/protocols/PrimarySelection.cpp:81-87`; `src/protocols/DataDeviceWlr.cpp:80-86`; `src/protocols/ExtDataDevice.cpp:80-86`). Selection ownership stores references/listeners, not paste bytes (`src/managers/SeatManager.cpp:697-752`).

```cpp
// DataDevice.cpp:63, 193
m_source->send(mime ? mime : "", std::move(sendFd));
m_resource->sendSend(mime.c_str(), fd.get());
```

**Path and boundary — source-confirmed.** The receiving client requests an offered representation; the compositor passes the destination descriptor to the selected Wayland source; the clients transfer payload through that descriptor. The compositor does retain MIME strings, resources and protocol transport messages, so this is specifically an absence of payload caching, not a claim of zero memory/resource costs. F-SEAT-04 discusses transfer-request pressure separately. X11 sources dispatch into `src/xwayland/XDataSource.cpp:51` and XWM selection-transfer machinery, so payload limits there require the separate XWayland audit; they cannot be settled by this core forwarding path.

**Severity and corrections.** Informational control, confirmed. No compositor-payload OOM claim is retained for a purely Wayland-to-Wayland paste. The dossier's broader statement that cursor SHM is the compositor's only other data buffer is outside this narrow conclusion and should not be generalized to all compositor subsystems. No payload transfer or X11 size test was run.

**Minimal fix direction.** No paste-payload cap is needed on a nonexistent core payload cache. If a cache is introduced, define ownership, quotas and eviction explicitly; separately validate bridge buffering and generic per-client metadata/resource accounting.

## Verification artifacts and limits

All scratch files are under `/home/wilf/data/.claude-scratch/agent-reports/work/09-seat-clipboard/`:

- `seat_schema.c`, `seat_schema`, `seat_schema.log`: **executed**, exported libwayland-server method-schema assertions passed; installed library version 1.26.0.
- `seat_argument_guard.c`, `seat_argument_guard`, `seat_argument_guard.log`: compiled isolated unnamed-socketpair guard control; **execution blocked** at `wl_client_create` with `Operation not permitted`, before request dispatch. An initial assertion failure and the diagnostic rerun both occurred before dispatch. No validation result is inferred from this attempt.
- Report-generation scripts: incremental writing helpers only; they do not modify upstream or contact a compositor.

No live paste, drag, touch, output reconfiguration, cursor rendering, victim flooding, backend allocation test, or full build was performed. Installed protocol XML/header evidence is identified by its absolute path and is distinct from pinned upstream source evidence. The dossier's blanket wlroots behavior/limits were not assumed when the local protocol or caller guards contradicted them. Review was confined to the named dossier; related defects were not expanded into new finding IDs.
