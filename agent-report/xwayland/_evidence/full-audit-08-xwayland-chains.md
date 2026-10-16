# XWayland lifecycle and integration verification

Baseline: official Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).  
Dossier: [08-xwayland-chains](../../pending-evaluation/2026-10-full-audit/08-xwayland-chains.md).  
Date: 2026-10-15.

Method: Read-only static verification of the pinned upstream worktree, relevant callers, protocol wrappers, and locally available dependency definitions. Evidence is labeled **source-confirmed** or **executed**. No live desktop, compositor binary, IPC, network, or full build is used. Source-level defects are distinguished from unverified runtime outcomes. All source locations below refer to this baseline unless explicitly identified as dependency references.

## Summary

All 11 dossier IDs reviewed: **2 confirmed, 6 partially, 2 rejected, 1 inconclusive**. Verdict counts describe records, not a count of independent vulnerabilities. Partial verdicts retain only the expressly supported portions.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-XW2-01 | inconclusive | Unrated; High only if cross-UID acceptance is established | `src/xwayland/Server.cpp:36–79,303–318` | Socket permissions and missing auth are confirmed; default cross-UID admission depends on Xwayland peer ACLs, not established here. |
| F-XW2-02 | partially | High | `src/xwayland/XWM.cpp:403–413`; `src/protocols/core/Compositor.cpp:326–328` | Unchecked resource cast remains; duplicate-surface guard defeats the claimed theft of an already-associated surface. |
| F-XW2-03 | confirmed | High | `src/xwayland/XDataSource.cpp:10–44` | TARGETS import treats element counts as atom counts without checking format, permitting an out-of-bounds read. |
| F-XW2-04 | partially | Medium | `src/xwayland/XWM.cpp:287–299` | Zero-length transient property permits an out-of-bounds read; arbitrary parent selection already works with valid properties. |
| F-XW2-05 | partially | Medium | `src/xwayland/XWM.cpp:677–683,806–843,1332–1346` | Ungated selection replacement is reachable while locked after X focus; default activation theft and automatic command execution are unsupported. |
| F-XW2-06 | rejected | Informational | `src/xwayland/XWM.cpp:220–241`; `src/desktop/rule/windowRule/WindowRule.cpp:394–407` | Class/title are intentionally client-provided matching metadata; no authenticated identity or privilege boundary is demonstrated. |
| F-XW2-07 | partially | Medium | `src/xwayland/XWM.cpp:485–504`; `src/protocols/core/DataDevice.cpp:219–222` | Completion forwarding lacks destination/phase checks; Wayland notification is reachable, but file deletion and the proposed field check are unsupported. |
| F-XW2-08 | rejected | None established | `src/xwayland/XWM.cpp:875–878,1079–1087,1720–1727`; `src/xwayland/XWM.hpp:186–214` | Hyprutils reset retains access during destruction; the connection outlives transfers. Persistent wl_client leakage is not demonstrated. |
| F-XW2-09 | confirmed | Medium | `src/xwayland/XWM.cpp:56–65,883–912`; `src/xwayland/XSurface.cpp:9–54` | Unmapped top-level windows allocate persistent compositor state and synchronous reply waits; no event-drain budget. Runtime starvation magnitude remains unmeasured. |
| F-XW2-10 | partially | Low | `src/xwayland/Server.cpp:195–244` | Global lock-name exhaustion can prevent startup across 33 displays; cross-user socket replacement and MITM are not established. |
| F-XW2-11 | partially | Low robustness; other items informational or rejected | `src/xwayland/Server.cpp:303–321,397–413`; `src/main.cpp:45–53` | Startup failure lacks explicit retry; zombie accumulation is prevented. PATH and CurrentTime behavior do not establish escalation or unauthorized disclosure. |

## F-XW2-01

**Verdict: inconclusive. Severity: unassigned; High would require confirmed cross-UID admission.** The socket is world-connectable and Hyprland supplies no explicit Xauthority file. Those facts alone do not prove that Xwayland admits every local UID.

**Source-confirmed evidence.** `src/xwayland/Server.cpp:63–66` explicitly delegates authorization to Xwayland's ACL and sets regular socket permissions to 0666. `Server.cpp:303–318` starts Xwayland with display, rootless, core, listening-FD, readiness-FD, and WM-FD arguments, without `-auth` or `-ac`. `openSockets` always creates the conventional regular socket and either a second regular socket with an underscore suffix or an abstract socket (`:123–170`); `xwayland:create_abstract_socket` defaults to false (`src/config/values/ConfigValues.cpp:640`). Socket setup validates the directory owner and unsafe directory writability (`Server.cpp:82–107`). These checks protect the socket directory, not X client authorization.

```cpp
if (isRegularSocket && chmod(addr->sun_path, 0666) < 0) {
```

**Boundary and missing evidence.** A different local UID may reach the Unix listener; admission then depends on the launched Xwayland implementation's access-control initialization and peer-credential policy. Hyprland's source ends at `execl` and does not settle this boundary. In particular, “ACLs only work with an authorization file” is false: the local dependency manual documents host and server-interpreted controls separately from file-based authorization (`/usr/share/man/man7/Xsecurity.7.gz`, sections ACCESS SYSTEM DESCRIPTIONS and THE AUTHORIZATION FILE).

**Source-confirmed supporting dependency evidence.** The locally available X server reference source is **xorg-server 21.1.24**, not a pinned Xwayland source/build (`/usr/src/xorg-server/configure.ac:29`). Its `os/auth.c:185–207` enables local access when no authorization is loaded, but `os/access.c:239–267` distinguishes host-wide and user-scoped local access; `:313–377` can install an entry for the server's own local user. The corresponding Xwayland initialization selecting a scope is absent from that source tree, so extrapolating its generic host default to the launched Xwayland would be unjustified. No server or second-user connection was run.

**Preconditions, impact, and correction.** Xwayland must be enabled and present (`src/xwayland/XWayland.cpp:6–33`), and a second UID must be able to reach the host's socket namespace. Clipboard and X11 input access require successful X authorization first. Even authorized X11 access does not by itself prove input injection into native Wayland windows or a lockscreen bypass. The dossier's comparison with all wlroots compositors, cookie assumptions, and “user-level complete takeover” conclusion are not established by the pinned Hyprland code.

**What would settle it / minimal fix direction.** Review the exact deployed Xwayland authorization initialization and build options, or perform an explicitly isolated two-UID admission test on a disposable server. If admission is too broad, retain per-UID peer authorization or provision session cookies; review abstract and regular listeners together. Do not remove compatibility permissions merely on the assumption that no cookie means no ACL.

## F-XW2-02

**Verdict: partially. Severity: High for the unchecked resource conversion.** The dossier alleges a resource-type confusion and theft of another application's Wayland surface through the legacy association message. The unchecked conversion exists, but the general already-associated-surface theft claim is blocked.

**Source-confirmed evidence.** `src/xwayland/XWM.cpp:390–414` first requires a known X window, then retrieves an object from the Xwayland Wayland client and passes it to an unchecked conversion:

```cpp
auto resource = wl_client_get_object(g_pXWayland->m_server->m_xwaylandClient, id);
if (resource) {
    auto surf = CWLSurfaceResource::fromResource(resource);
    associate(XSURF, surf);
}
```

`src/protocols/core/Compositor.cpp:326–328` casts the returned resource's user data to `CWlSurface*`, calls `data()`, casts again to `CWLSurfaceResource*`, and accesses its weak self pointer. There is no interface, implementation, or live-surface-registry check. Surface setup assigns that data only for actual surfaces (`Compositor.cpp:84–95`). A non-surface object is therefore outside the conversion's type contract; the subsequent pointer operations have undefined behavior. Checking that an object exists does not check its interface.

**Source-confirmed chain and reachability.** An admitted X client can supply client-message data concerning a known X window; delivery to the XWM enters `onEvent` (`XWM.cpp:895–904`) and `handleClientMessage`, reaches the legacy branch, and then the conversion. This is an X event, not a typed Wayland request: libwayland request argument validation cannot validate the embedded integer. Lookup is restricted to **Xwayland's** object namespace; it does not expose arbitrary native Wayland clients' resources. The wrapper definitions inspected locally (`/usr/include/hyprland/protocols/wayland.hpp:1240–1259`) confirm that `data()` is an unchecked member access; those installed generated definitions are supporting dependency evidence, not claimed byte-identical generated output from this baseline.

**Source-confirmed guards and corrections.** `associate()` searches for an existing association and returns if any X surface already owns the candidate (`XWM.cpp:1216–1225`). A normal mapped victim surface is therefore not stolen by the dossier's proposed association. Surface destruction listeners reset the weak surface (`XSurface.cpp:79–94`), so a dangling surface is not established merely by citing this branch. Weak locking returns an empty strong pointer when destruction has begun (`/usr/include/hyprutils/memory/WeakPtr.hpp:175–185`); it does not make a wrong-type pointer valid. The serial protocol describes serials as **monotonic**, not cryptographic secrets (`/usr/share/wayland-protocols/staging/xwayland-shell/xwayland-shell-v1.xml:125–141`), so “unforgeable 64-bit serial” is also withdrawn. Sending a message only to one's own window with no selected event mask is not evidence of XWM delivery; the dossier's sample does not establish that step.

**Impact and limits.** A compositor memory-safety defect is source-confirmed; a deterministic crash, content disclosure, usable UAF, and native-Wayland surface capture were not executed or demonstrated. High reflects the potentially compositor-fatal unchecked type conversion reachable from an admitted local X client, not code execution or cross-user authorization.

**Minimal fix direction.** Resolve legacy IDs through the compositor's live surface registry or validate the exact resource interface/implementation before conversion. Preserve the duplicate-association guard and reject legacy messages when using the serial association protocol.

## F-XW2-03

**Verdict: confirmed. Severity: High.** Selection TARGETS import validates the property type but not its element format, then reads it as an array of 32-bit atoms.

**Source-confirmed evidence.** `src/xwayland/XDataSource.cpp:10–25` requests `_WL_SELECTION` using `XCB_GET_PROPERTY_TYPE_ANY`, checks only `reply->type == XCB_ATOM_ATOM`, and executes:

```cpp
auto value = sc<xcb_atom_t*>(xcb_get_property_value(reply));
for (uint32_t i = 0; i < reply->value_len; i++) {
```

The loop accesses `value[i]` (`:26–41`). For a nonempty 8-bit property, the element count describes bytes, while the loop reads four times that payload size. X's independent property type and format fields permit this representation; the local X server reference validates formats and atom identifiers independently (`/usr/src/xorg-server/dix/property.c:209–247`). No Wayland decoder participates in this XCB reply parsing.

**Source-confirmed chain and preconditions.** An admitted X selection owner supplies a TARGETS conversion result. The XFixes owner handler requests conversion into the compositor's selection window (`XWM.cpp:806–843`); `handleSelectionNotify` obtains the recognized selection, requires a nonempty X focus weak pointer for the TARGETS branch (`:664–683`), and calls `setClipboardToWayland` (`:1332–1334`), which constructs `CXDataSource` and reaches the loop. This requires Xwayland enabled and a live X focus at import, not root or another user's account. The owner is already entitled to write its conversion result into the requestor's property; no property-write race is needed to establish the parser input.

**Impact and corrections.** The out-of-bounds read is source-confirmed. It can cause compositor failure, but the dossier's “stable SIGSEGV” is not an executed result. Subsequent `mimeFromAtom` calls resolve integers as atom names (`XWM.cpp:645–658`); that does not prove arbitrary byte exfiltration. The GetProperty length of 4096 is in **four-byte units**, not a 4096-byte response limit; this correction does not remove the element-count mismatch. This is a distinct parser location in the same format-validation defect family as F-XW-01, not a separately proven disclosure chain.

**Minimal fix direction.** Require atom type and format 32, validate payload byte length against the element count using checked arithmetic, and bound accepted atom counts before iteration.

## F-XW2-04

**Verdict: partially. Severity: Medium.** Transient-parent parsing lacks format and payload-size checks. The zero-length read is real; treating ordinary parent declarations as an additional privilege boundary is unsupported.

**Source-confirmed evidence.** `src/xwayland/XWM.cpp:287–295` contains:

```cpp
if (reply->type != XCB_ATOM_WINDOW)
    return;
const auto XID = rc<const xcb_window_t*>(value);
XSURF->m_transient = XID;
if (!XID)
    return;
```

It subsequently evaluates `windowForXID(*XID)`. `value` comes from `xcb_get_property_value(reply)` (`:217–218`), which addresses the reply payload; a non-null address does not establish that the payload contains a window ID. A present, WINDOW-typed property with zero elements can have no payload, yet this branch reads four bytes. A short nonzero payload may occupy four-byte wire padding: it must not automatically be described as a four-byte read beyond the allocated reply in every short-value case.

**Source-confirmed chain and preconditions.** A connected X client updates its window's transient-parent property. The constructor subscribes to property changes (`XSurface.cpp:16–18`); dispatch calls `handlePropertyNotify`, which obtains the property with type ANY and calls `readProp` (`XWM.cpp:370–387`). Initial window-data reads reach the same parser (`:1189–1204`). No map, elevated permission, or cross-user connection is necessary for a property event on a known X window. X property validation does not impose semantic WINDOW/32-bit pairing (local reference `/usr/src/xorg-server/dix/property.c:209–247`).

**Impact and corrections.** This establishes an invalid read, not a reliable crash or useful disclosure. `m_transient` is actually a boolean (`XSurface.hpp:98`); assigning `XID` converts its non-null address to true, rather than storing a dangling pointer. No later dereference through that field is established. Choosing another known X window as parent is already possible with a valid 32-bit declaration. The existing ancestry check (`XWM.cpp:174–188,295–299`) is separate from payload validation. Thus malformed-data memory safety is retained, while the claimed new arbitrary-parent privilege escalation is withdrawn.

**Minimal fix direction.** Require WINDOW type, format 32, and exactly one available window ID before copying it into a local `xcb_window_t`; handle deletion/reset explicitly.

## F-XW2-05

**Verdict: partially. Severity: Medium.** The X-to-Wayland selection bridge can replace seat selection without checking current seat focus or session lock. The dossier overstates default focus stealing and the consequences of a later paste.

**Source-confirmed evidence.** XFixes selection changes request TARGETS without a lock or current-seat-focus check (`src/xwayland/XWM.cpp:806–843`). TARGETS import checks only the XWM's weak focus pointer (`:677–683`), and a nonempty imported source reaches the seat setters (`:1332–1346`):

```cpp
if (!m_focusedSurface)
    return;
// ... setClipboardToWayland ...
g_pSeatManager->setCurrentSelection(sel.dataSource);
```

The primary-selection branch calls `setCurrentPrimarySelection` in the same function. Seat setters replace their current source and notify protocol consumers without a lock/focus predicate (`src/managers/SeatManager.cpp:697–752`). The XFixes owner-none branch can also clear seat selection without the TARGETS focus check (`XWM.cpp:820–825`).

**Source-confirmed closed lock-state path.** Start with a live, focused managed X window, then enter a normal session lock. `SessionLockManager.cpp:98–100` calls `rawSurfaceFocus(nullptr)`. `FocusState.cpp:245–254` deactivates the old desktop surface and clears seat/surface focus; it does not clear the desktop window there. Deactivation routes through `XWaylandManager.cpp:24–45`, `X11Backend.cpp:354–356`, and `XSurface.cpp:226–230`. In `XWM.cpp:1110–1119`, deactivation while the desktop focus window is still X11 takes the branch retaining that X surface as XWM focus. An X selection owner can then supply a normal conversion result; the old nonempty `m_focusedSurface` passes import while seat focus is null or belongs to the locker. The seat stores that source independently of whether a recipient currently has a data device (`core/DataDevice.cpp:483–520`). Future keyboard-focus updates distribute the current clipboard selection (`:523–545`). PRIMARY delivery differs: it follows pointer focus, and a focused client lacking a primary-selection device can cause the seat primary pointer to reset (`src/protocols/PrimarySelection.cpp:272–328`). The closed lock-state argument above is therefore specifically sufficient for CLIPBOARD; universal PRIMARY persistence is not claimed. No malformed input or successful activation request is needed for the clipboard path.

**Other guards and corrections.** Merely finding no lock-manager references in the Xwayland directory is insufficient to prove a keyboard/render bypass. Ordinary managed activation routes through `X11Backend.cpp:216`, `Window.cpp:143,1828–1849,813–835`; `misc:focus_on_activate` is **false** by default (`ConfigValues.cpp:580`). Normal switching from X to a native window updates desktop focus before deactivating the X window (`FocusState.cpp:185–201`), causing `activateSurface` to clear XWM focus. Thus “any historical X focus always permits replacement” and “most/default configurations grant activation” are withdrawn. Weak focus storage is not a raw-pointer UAF (`XWM.hpp:209`; local `WeakPtr.hpp:157–190`).

**Impact and limits.** Seat selection integrity during lock is affected, and later pastes may use unexpected data. Import does not execute that data, press paste keys, or bypass a terminal's paste handling; automatic shell-command execution is not established. The absence of per-owner throttling is source-visible, but a starvation threshold was not measured. The dossier also cites `SXSelection::onRead` as unlimited X-to-Wayland accumulation: that function reads a **Wayland source into X** (`XWM.cpp:1507–1546,1565–1581,1644–1663`). Incoming X properties instead use `getIncomingSelectionProp` (`:1729–1741`); its large request limit warrants separate resource-budget review, not a proven OOM in this report.

**Minimal fix direction.** Gate selection replacement and clearing on real seat focus and session-lock policy, invalidate stale XWM focus appropriately, and budget conversion work. Keep ordinary selection interoperability and deferred offers consistent across unlock.

## F-XW2-06

**Verdict: rejected as a vulnerability; informational trust-model observation.** X class and title can match user window rules, but the dossier does not identify an authenticated application identity or authorization boundary that this behavior violates.

**Source-confirmed evidence and chain.** X property input goes through `handlePropertyNotify` / `readProp` (`src/xwayland/XWM.cpp:370–387`). Class is copied into `m_state.appid`, and accepted string titles into `m_state.title` (`:220–241`). `X11Backend.cpp:22–35` normalizes these strings as backend metadata. Window metadata changes update rule properties (`src/desktop/view/window/Window.cpp:901–936`), and `CWindowRule::matches` explicitly matches client metadata (`src/desktop/rule/windowRule/WindowRule.cpp:394–407`):

```cpp
case RULE_PROP_CLASS:
    if (!engine->match(w->metadata().appID()))
        return false;
```

A local X app can therefore use a matching class/title if the user configured such a rule. Rules themselves are trusted configuration; these strings do not grant OS credentials, arbitrary execution, or authenticated provenance. The PID query is separate (`XSurface.cpp:9–39`), and is conditional on XRes availability; it is not a signature or a class-to-binary authentication mechanism.

**Corrections, preconditions, and severity.** A deceptive window can display misleading identity or receive configured presentation/focus policies, but class/title matching is the documented architectural purpose visible in these predicates. No vulnerable privileged rule, privileged consumer, or escalation beyond user-selected UI policy is supplied. The `windowrulev2` examples are fork-era configuration syntax, not the baseline's rule implementation. Class-to-PID binding alone would not authenticate an application, and a PID can be reused. Severity is informational, not Medium, absent a separate trusted consumer that mistakes metadata for authenticated identity.

**Minimal fix direction.** Document client metadata as untrusted and avoid using it as an authentication signal. No mandatory source fix follows from this record; any stronger application identity scheme needs a separate threat model.

## F-XW2-07

**Verdict: partially. Severity: Medium.** Incoming XDND status/completion messages can affect the currently retained source without matching the actual target or transfer phase. The completion-forwarding defect is source-confirmed; deletion of user files is a contingent application effect, not a demonstrated outcome.

**Source-confirmed evidence.** `src/xwayland/XWM.cpp:390–394` only requires the message's X window to be known. The XDND branches (`:485–504`) only check that the offer list is nonempty and its weak source locks successfully:

```cpp
if (m_dndDataOffers.empty() || !m_dndDataOffers.at(0)->getSource())
    return;
m_dndDataOffers.at(0)->getSource()->sendDndFinished();
```

There is no comparison with `offer->m_xwaylandSurface`, message window fields, a sent-drop flag, success flag, or negotiated action. `XdndStatus` similarly forwards acceptance. A genuine Wayland source's `sendDndFinished` sends a protocol event when its resource version is at least 3 (`src/protocols/core/DataDevice.cpp:219–222`). The weak source is checked before use; an expired source is guarded rather than dereferenced.

**Source-confirmed chain and preconditions.** `updateDrag` obtains the X data device and creates an X offer for a known X surface (`core/DataDevice.cpp:666–712`); `createX11DataOffer` retains the destination and source (`XWM.cpp:1449–1465`). This path requires that the surface supplied there resolves to an X surface; it is not a claim that every Wayland-to-X drag succeeds in all focus states. While such an offer with a live source exists, an admitted X client's client message delivered to the XWM dispatches through `onEvent` (`:895–904`) and reaches the unchecked branch. It can therefore notify the native Wayland source independently of a matching target completion. Merely owning an arbitrary window with no offer is insufficient. The dossier's self-window/no-event-mask sample does not itself prove WM delivery.

**Corrections and limits.** XDND reply `data[0]` identifies the **target**, while the event destination is the source-side window; the dossier reverses this relationship. Comparing `data[0]` to the compositor's source selection window is therefore not a correct general fix. Track the actual target/proxy pair used by `Dnd.cpp:130–140,181–192,198–213`. The outbound proxy property is already type/format/size checked and self-verified (`Dnd.cpp:43–71`). X SendEvent provides no authenticated sender field; destination and phase validation improve state consistency but cannot create process isolation inside X11.

No tested file manager is supplied, and the handler ignores the reported MOVE action instead of establishing a valid MOVE negotiation. A source might react dangerously to early completion, but automatic file deletion is not confirmed. Likewise, transporting `text/uri-list` without a compositor-level file existence or scheme whitelist is not itself a defect: interpreting offered URIs belongs to the receiving application's policy.

**Minimal fix direction.** Match the active target/proxy and source destination, validate the format and transfer phase, and forward success/action consistently only after an actual drop. Keep the source lifetime guard. Do not claim sender authentication from a client-controlled window field.

## F-XW2-08

**Verdict: rejected for the claimed reset-time null dereference. Severity: none established.** The dossier applies `std::unique_ptr`-style assumptions to Hyprutils' different owner implementation. Its asserted permanent client/fd leak is also not established.

**Source-confirmed evidence.** Hangup queues `m_wm.reset()`, then `m_server.reset()`, then recreation (`src/xwayland/XWM.cpp:869–880`). A transfer destructor accesses the global WM's connection if `incomingWindow` is set (`:1720–1727`). However, `m_wm` uses Hyprutils `UP` (`src/xwayland/XWayland.hpp:21–23` and `src/helpers/memory/Memory.hpp:12–13`), whose installed implementation does this:

```cpp
void reset() {
    decrement();
    impl_ = nullptr;
    m_data = nullptr;
}
```

Dependency evidence: `/usr/include/hyprutils/memory/UniquePtr.hpp:103–119,132–159` and `ImplBase.hpp:82–93`. Destruction runs inside `decrement()` **before** resetting owner storage; the implementation keeps its data pointer non-null during the deleter. `get()` does not reject the destroying state. In addition, `m_connection` is declared before all selection members (`XWM.hpp:186,212–214`), so reverse member destruction destroys transfers before the connection. `CXWM::~CXWM` itself does not reset that connection (`XWM.cpp:1079–1087`). The claimed null-before-transfer sequence is broken twice by these actual lifetime rules.

**Executed countercheck.** The isolated [ownership probe](/home/wilf/data/.claude-scratch/agent-reports/work/08-xwayland-chains/ownership_probe.cpp) compiled with the installed Hyprutils headers and ran without a compositor or X server. It creates an owner with a connection followed by a transfer, resets the global owner, and checks the same ownership order. Output:

```text
owner-destructor
transfer-sees-owner-and-connection=true
connection-destroyed
owner-empty-after-reset=true
```

The probe and its output reside only in the assigned scratch directory; it verifies dependency semantics, not the complete Hyprland restart path or every dependency version. The absolute scratch source path is `/home/wilf/data/.claude-scratch/agent-reports/work/08-xwayland-chains/ownership_probe.cpp`.

**Leak qualification and preconditions.** `Server.cpp:283–287` really omits an explicit `wl_client_destroy`, but its transferred fd is owned by libwayland (`:357–364`), and the opposite socketpair ends remain RAII members (`Server.hpp:35–41`). Omitting an explicit destroy is not proof that a disconnected Wayland client permanently survives normal EOF cleanup. The dossier provides no retained-client/fd measurements and no independent X-client primitive for terminating Xwayland: both are additional assumptions. A hangup with an incoming transfer is a plausible lifecycle state, but it does not make the cited destructor invalid under these semantics.

**Minimal fix direction.** No reset-order fix is justified by the alleged null dereference. Explicit connection references in transfer cleanup could reduce coupling, while any client cleanup change should be based on an isolated disconnect/leak test and libwayland ownership review. Do not clear transfers prematurely merely to fix a nonexistent null-owner ordering.

## F-XW2-09

**Verdict: confirmed, with unmeasured impact bounded to a resource/fairness defect. Severity: Medium.** Top-level X window creation allocates full persistent compositor state before mapping and performs blocking reply waits inside an unbudgeted event drain.

**Source-confirmed evidence and chain.** With Xwayland enabled (default true: `src/config/values/ConfigValues.cpp:637`), the XWM subscribes to root substructure notifications (`XWM.cpp:1044–1047`). An admitted X client creating root children causes CreateNotify dispatch (`:895–896`); apart from `isWMWindow`, `handleCreate` has no mapped-window or resource-budget guard (`:56–65`):

```cpp
const auto XSURF = m_surfaces.emplace_back(SP<CXWaylandSurface>(new CXWaylandSurface(...)));
const auto WINDOW = Desktop::View::CWindow::create(makeUnique<Desktop::View::CX11Backend>(XSURF));
```

`CXWaylandSurface` waits for XRes PID replies if available (`XSurface.cpp:9–39`), then normally calls `recheckSupportedProps`, which separately waits for ListProperties and GetProperty replies (`:42–54`). `CWindow::create` initializes the backend, rule applicator, animation/presentation, target, and view (`src/desktop/view/window/Window.cpp:101–130`). `IView::initView` emits view creation (`src/desktop/view/View.cpp:23–34`), and `CWindowState` retains a strong window pointer (`src/desktop/state/WindowState.cpp:13–20`). Thus the local `WINDOW` variable going out of scope does not free the object; no map is required to retain it. The X surface vector also retains strong pointers (`XWM.hpp:205`).

**Fairness and cleanup.** `onEvent` polls and processes until the X event queue is empty, with no iteration/time cap (`XWM.cpp:883–912`). The callback is registered on the compositor's Wayland event loop (`:1038`). A sustained stream therefore couples X request work and reply waits to compositor dispatch, while retained unmapped windows consume memory. DestroyNotify emits destruction and erases the X surface (`:68–77`), and the window destruction path removes the window from global state (`Window.cpp:1810–1825`); this is resource retention while windows remain alive, not a permanent leak after normal client teardown.

**Corrections and severity.** `xcb_change_window_attributes` is an asynchronous request, **not** a third blocking round trip. The synchronous waits are PID lookup when XRes is available plus the two property replies; error returns can shorten that path. Root substructure notification does not imply that every nested child in an arbitrary client tree has the same path. No throughput benchmark was run; visible input latency, sustained lockscreen freeze, and OOM remain unverified. Medium reflects observable source-level amplification and absence of dispatch fairness, rather than a claim of a measured system-wide crash or guaranteed permanent hang.

**Minimal fix direction.** Budget X event work per dispatch, pipeline reply-dependent metadata queries, and keep unmapped-window bookkeeping lightweight or capped without losing legitimate lifecycle notifications.

## F-XW2-10

**Verdict: partially. Severity: Low.** Occupying the finite global display-lock namespace can prevent Xwayland startup. The proposed cross-user socket replacement/MITM chain is not established.

**Source-confirmed evidence and chain.** Startup calls `create` → `tryOpenSockets` (`src/xwayland/Server.cpp:290–298`). It tries public lock names with exclusive creation and skips ones whose existing contents cannot be reclaimed (`:195–244`):

```cpp
for (size_t i = 0; i <= MAX_SOCKET_RETRIES; ++i) {
    // ... O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC ...
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        if (!safeRemove(lockPath))
            continue;
        i--;
    }
}
```

`MAX_SOCKET_RETRIES` is 32 (`:33`), and the inclusive loop covers **0 through 32: 33 displays**. A different local user able to create names in the shared temporary-directory namespace before startup can leave nonreclaimable names covering those candidates; `m_display` remains negative and startup fails (`:242–244`). No X protocol access, Xwayland authorization, or elevated permission is required for name occupancy on an ordinary shared host. Existing running Xwayland instances are not terminated by this startup-only failure.

**Guards and corrections.** The dossier's 0–31 sample omits the last candidate. `kill(pid,0)` may fail with EPERM for another UID, which still prevents reclaim because only ESRCH is handled. Empty or malformed contents are also skipped (`:224–233`). Conversely, different-UID lock files in a sticky directory generally cannot be removed by the victim, including stale ones; failure to remove causes a skip. The socket directory is checked for owner and writable-without-sticky conditions (`:82–107`), and socket paths are unlinked by the compositor's own UID (`:53–59`). Those operations cannot normally remove another user's socket in a root-owned sticky directory. An already-bound abstract socket cannot be replaced by unlinking a pathname. Namespace/UID-sharing configurations can change these constraints, but none is supplied as a closed MITM precondition.

**Impact and limits.** This is a bounded startup-availability weakness in the traditional X display allocation scheme. It does not prove a general ability to redirect an established victim display, and F-XW2-01's authorization uncertainty cannot be treated as a confirmed building block. No files in the real global namespace were created or deleted for this review.

**Minimal fix direction.** Improve allocation failure diagnostics and recovery, consider broader display allocation with careful compatibility requirements, and validate lock/socket ownership before cleanup. Using `-displayfd` does not by itself eliminate the conventional X socket/lock namespace or authorize ignoring occupied names.

## F-XW2-11

**Verdict: partially. Severity: Low for startup robustness; the remaining subclaims are informational or rejected.** This combined record includes four independent assertions, assessed separately below.

### Selection timestamps and requestor identity

**Source-confirmed.** `src/xwayland/XWM.cpp:749–759` bypasses its stale-time condition for CurrentTime, but then requires real seat keyboard focus to belong to Xwayland:

```cpp
if (!g_pSeatManager->m_state.keyboardFocusResource ||
    g_pSeatManager->m_state.keyboardFocusResource->client() != g_pXWayland->m_server->m_xwaylandClient) {
    selectionSendNotify(e, false);
    return;
}
```

A delivered X selection request reaches this guard through `handleSelectionEvent` (`:848–859`). If it passes, the requested source is selected by the compositor and data is delivered via `SXSelection::sendData` (`:1565–1663`); imported X selection reflection is explicitly rejected (`:1579–1581`). CurrentTime is a valid X selection timestamp convention, not an authentication token. The event provides a requestor window, not an authenticated originating process identity that this handler can compare. No source-confirmed unauthorized new-owner disclosure sequence is supplied, particularly when a native client or locker has focus. **Rejected as the claimed authorization bypass.** Optional hardening is to review timestamp/owner state transitions and accepted request phases; inventing process authentication from X event fields is not a fix.

### Shell and PATH launch

**Source-confirmed.** `Server.cpp:310–318` formats only internal display/fd numbers and passes the command through `/bin/sh -c`. `XWayland.cpp:21` checks availability using `NFsUtils::executableExistsInPath`, whose return type is **bool**, not an absolute executable path (`src/helpers/fs/FsUtils.cpp:83–107`). Thus the dossier's claim that a resolved path was already obtained and discarded is false.

The input is the compositor's inherited PATH. Execution of a replacement program requires control over an executable searched by that trusted launch environment. The record gives no mechanism by which an untrusted X client changes the parent compositor's PATH, no elevated launch identity, and no injectable command field. **Informational deployment hardening, not demonstrated escalation.** Direct argv-based execution and an explicitly resolved path can simplify launch behavior if desired; `execvp` alone still searches PATH.

### Failed child startup, retry, and zombies

**Source-confirmed.** Descriptor flag failure or exec failure exits the forked child (`Server.cpp:303–321`); startup is initiated by an idle callback (`:179–181,298`). The parent does not implement an explicit startup retry. Non-readable readiness failure resets the server (`:397–413`), while early readable EOF returns without setting up the WM (`:398–406`). The normal post-start XWM hangup path separately schedules recreation (`XWM.cpp:869–879`). Resource failure or a broken launch environment can therefore leave X compatibility unavailable for that compositor session, with exact readiness-event ordering unexecuted. This retained startup robustness issue is **Low**, not an independently established arbitrary-X-client attack.

**Source-confirmed counterevidence.** `src/main.cpp:45–53` installs `SIGCHLD` with `SA_NOCLDWAIT`; it is called at `:272`, before `initServer` (`:289`) and the manager stage that creates Xwayland (`src/Compositor.cpp:768–769`). Thus the claimed accumulating zombie children are prevented globally even though this particular function has no `waitpid`. It is also incorrect to claim there is no restart mechanism at all: the WM hangup mechanism exists, though it does not imply recovery before WM initialization.

**Minimal fix direction.** Make readiness EOF/error states explicit and bounded, record launch failure, and add controlled startup retry/backoff if required. Do not add redundant child reaping to fix the nonexistent zombie claim.

### Oversized WM_HINTS retains previous state

**Source-confirmed.** `XWM.cpp:257–275` resets absent/empty hints, rejects an oversized element count at `:266–267`, and uses the ICCCM parsing helper before replacing the old hint state. The focus decision consuming the input hint is `XWM.cpp:585–590`. An admitted client can submit invalid hints and leave its own previous hints effective, but can also validly request those same hints. No new authority or invalid-memory access follows. **Rejected as a security defect; informational parser policy.** Resetting to safe defaults on invalid metadata could make behavior clearer, but retaining the last valid metadata is not a demonstrated privilege violation.

## Cross-cutting lockscreen controls and evidence limits

**Source-confirmed control records.** Workspace rendering is suppressed under ordinary lock policy once the locker state requires it (`src/render/Renderer.cpp:1176–1180`), and `misc:session_lock_xray` defaults to false (`ConfigValues.cpp:583`). `CFocusState::rawWindowFocus` and `rawSurfaceFocus` contain lock checks (`FocusState.cpp:105–109,235–236`); the former has a priority-focus exception and is not an unconditional ban for every possible configuration. Input routing also has a lock-surface branch (`src/managers/input/InputManager.cpp:340–359`), while ordinary keyboard dispatch goes through the seat (`:1743–1744`). None of this review establishes native-Wayland input injection through XTEST, a window visibly covering the lockscreen, or XGrabKeyboard bypassing the compositor seat. Enabling xray means rendering **below** the locker, not automatically raising an X window above it.

The selection-integrity issue in F-XW2-05 and general event/resource pressure in F-XW2-09 remain relevant during lock because ordinary X clients continue running; that fact alone does not require dropping every X event while locked. F-XW2-08 does not establish a lock-state crash. The dossier's previous failed nested-session attempt is historical evidence from another tree and was not repeated or treated as verification of this baseline.

All vulnerability reachability and impact judgments above are static; only the benign ownership-order countercheck was executed. No exploit client, crash reproducer, live IPC, desktop interaction, second-user access test, compositor build, or network operation was performed.
