# Input and keybind dossier verification

- Baseline: official Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`), verified from the supplied detached upstream worktree.
- Dossier: [13-input-keybinds](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md).
- Date: 2026-10-15.
- Method: read-only source review of the pinned upstream and installed dependency semantics; isolated offline checks where stated. No compositor, session IPC, device input, full build, network, or Git mutation. Source paths below are relative to the supplied upstream worktree. The fork dossier is an allegation list, not evidence of upstream behavior.
- Status: complete. Verdicts: **2 confirmed, 5 partially, 3 rejected, 0 inconclusive**. These are source-level verdicts; no compositor vulnerability was reproduced.
- Evidence boundaries: **source-confirmed** means the pinned implementation or identified installed dependency establishes the stated behavior; **executed** is limited to the safe offline dependency checks below. The example report files named in the brief are absent from this tracker checkout; the required report schema is followed directly.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-INP-01 | partially | Medium | `Compositor.cpp:267`; `managers/input/InputManager.cpp:1680`; `keybinds/Manager.cpp:897` | Unrestricted clients reach configured binds; security-context sandbox clients are filtered. Arbitrary command execution and universal sandbox escape are not established. |
| F-INP-02 | confirmed | Medium | `keybinds/Manager.cpp:440,462,551,624`; `config/shared/actions/ConfigActions.cpp:1686` | Armed release callbacks bypass lock checks; drag release stops dragging, rather than starting locked-state movement. Other callback effects depend on configuration. |
| F-INP-03 | confirmed | Medium | `devices/TouchDevice.cpp:17`; `managers/input/InputManager.cpp:1571,1993` | Touch destruction can drop the last owner during emission, then reset a member of the freed wrapper; physical/backend removal required. |
| F-INP-04 | rejected | None | `managers/input/InputManager.cpp:1128–1173`; `devices/VirtualKeyboard.cpp:19` | Missing null checks are real, but owner-scoped synchronous signals and strong locals break the alleged late-event crash path. |
| F-INP-05 | partially | Medium | `managers/input/InputManager.cpp:1713–1744,1927`; `devices/VirtualKeyboard.cpp:24` | Held shared keys suppress physical presses/releases while the device persists; removal leaves a recoverable stale key, not permanent keyboard loss. |
| F-INP-06 | partially | Medium | `Compositor.cpp:267`; `managers/input/InputManager.cpp:719,923`; `keybinds/Manager.cpp:883` | Unrestricted pointer injection and prearmed kill-mode lock gap hold; sandbox clients are filtered, normal binds remain gated, Escape cancels kill mode. |
| F-INP-07 | rejected | None | `keybinds/Manager.cpp:610–615`; `keybinds/InputState.cpp:139–155` | Synthetic release recurses once with pressed=false; extracted callbacks own binds and are removed before invocation. No claimed unbounded recursion demonstrated. |
| F-INP-08 | partially | Informational | `protocols/VirtualKeyboard.cpp:113,137`; `devices/VirtualKeyboard.cpp:19` | Protocol destruction emits twice, but normal teardown destroys the device listener and drains callbacks; repeated InputManager teardown is not established. |
| F-INP-09 | partially | Low | `keybinds/Resolver.cpp:44–79`; `managers/input/InputManager.cpp:1162`; `config/lua/bindings/LuaBindingsDispatchers.cpp:395` | Cache and numeric conversion defects affect configured shortcut forwarding; ordinary binding matching uses different parsing. No client-supplied binding-string path established. |
| F-INP-10 | rejected | None | `protocols/ExtWorkspace.cpp:29`; `workspace/HLWorkspace.cpp:94` | Empty-workspace commands exist, but ext-workspace advertises no creation capability and implements no create-workspace request handler. |

## F-INP-01

**Verdict: partially. Revised severity: Medium (input authorization/policy exposure for unrestricted clients).**

The dossier claims every Wayland client can use a virtual keyboard to run arbitrary commands through user bindings, including a Wayland sandbox escape. Reaching existing configured bindings is source-confirmed for unrestricted connections; the universal client, sandbox-escape, and arbitrary-command conclusions do not follow.

**Evidence — source-confirmed.** `src/Compositor.cpp:267–271` supplies the guard omitted by the dossier:

```cpp
if (!PROTO::securityContext->isClientSandboxed(client))
    return true;
return !g_pProtocolManager || !g_pProtocolManager->isGlobalPrivileged(global);
```

`src/Compositor.cpp:288` installs this global filter. The whitelist in `src/managers/ProtocolManager.cpp:360–403` excludes both virtual-input managers. Thus security-context clients cannot access those globals through the normal registry/bind path. `src/protocols/SecurityContext.cpp:152–164` records clients accepted through the security-context listener. This protection is specific to that integration; an application sandbox given an unrestricted compositor connection is not automatically classified this way.

For an unrestricted connection, virtual-keyboard creation reaches the device setup in `src/protocols/VirtualKeyboard.cpp:164–176` and `src/managers/input/InputManager.cpp:1109–1112`. `InputManager.cpp:1215–1226` enables keybinds by default and consults the keyboard permission rule. `src/managers/permissions/DynamicPermissionManager.cpp:194–224` honors matching allow/deny/ask rules before falling back to keyboard ALLOW. This is a default policy, not an absence of all permission handling.

`InputManager.cpp:1681` rejects disabled/disallowed keyboards. `:1684–1700` excludes the grabbing IME's own virtual keyboard from binding actions, then otherwise routes input to the keybind manager. `src/keybinds/Manager.cpp:198–199` checks `m_allowBinds`; `:470` and `:551` apply `canInvokeNow` to ordinary candidates. `:904–905` blocks ordinary bindings during session lock. Device selectors, submaps, and modifier matching also apply (`src/keybinds/Bind.cpp:162–167`, `:222–238`). Accepted callbacks reach `Manager.cpp:606` and `Bind.cpp:241–242`.

Configured execution actions use a captured configuration string (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:180–205`) and the executor ultimately launches that string (`src/config/supplementary/executor/Executor.cpp:173–205`). The virtual-keyboard request supplies input events, not the execution string. A binding that launches a fixed program proves only execution of that program. Additional application interaction or a configuration-dependent callback would be required to establish attacker-chosen command execution; neither is demonstrated here.

**Reachability and corrections — source-confirmed.** An unrestricted session client, a usable virtual keyboard, no deny rule, an enabled device with binds allowed, and a matching available binding are required. `input:virtualkeyboard:share_states=2` is the default (`src/config/values/ConfigValues.cpp:436`), but disabling shared state alone does not establish device trust or disable all bindings. The default `misc:name_vk_after_proc` is **true**, not a universal common name (`ConfigValues.cpp:571`). `InputManager.cpp:2157–2169` also disambiguates simultaneous HID names. Permissions remain keyed by the resulting device name (`:1226`), not authenticated per-connection trust; this is a hardening concern, not proof that every device shares one approval.

The VT subclaim is narrower and real: internal key handling precedes ordinary candidate gating (`Manager.cpp:215–216`), and `:868–885` can ask an available backend session to switch VT without a lock or virtual-device check. It does not unlock the existing session; a backend session and a different VT are required.

**Fix direction.** Define a deliberate trust policy for virtual input on unrestricted connections, preserve the existing security-context filter, and gate binding/VT privileges separately where appropriate. Bind permissions to authenticated identity rather than treating a display name as identity. Do not label this Critical RCE or a demonstrated sandbox escape.

## F-INP-02

**Verdict: confirmed. Revised severity: Medium (configuration-dependent execution across a lock/activity boundary).**

Previously armed follow-up callbacks can execute without the availability checks used for new bindings. The dossier correctly identifies that policy gap, but its stock move/resize example does not establish movement while locked, and ordinary execution-only bindings do not automatically acquire release callbacks.

**Evidence — source-confirmed.** A successful press can arm the original callback through `src/keybinds/Manager.cpp:606–613`. The Lua wrapper maps a returned `request_release` field into this follow-up (`src/config/lua/bindings/LuaBindingsToplevel.cpp:235–240`; `src/config/lua/ConfigManager.cpp:1396–1398`). Built-in stateful actions can also request a follow-up using `LuaBindingsDispatchers.cpp:35–43`, e.g. pass/global/drag/resize at `:226`, `:251`, `:665`, and `:673`.

New ordinary binding candidates are checked at `Manager.cpp:470` against `canInvokeNow`, whose `:900–906` checks include enabled/registered/shadowed state, input capture, session lock, and inhibition. Three release paths differ:

- Device removal: `src/managers/input/InputManager.cpp:1525` calls `onDeviceRemoved`; `Manager.cpp:440–443` extracts that device's follow-ups and invokes them before clearing its pressed state.
- Ordinary release: `Manager.cpp:462–466` inserts extracted follow-ups into `hits`, and `:551` explicitly exempts them from the availability check.
- An allowed keyboard event while the backend session is inactive: `Manager.cpp:190–195` extracts all follow-ups, invokes them, and clears state.

The direct drain contains no availability check (`Manager.cpp:624–638`):

```cpp
invokeBind(release.bind, false,
    m_inputState.find(release.key, release.device.lock()));
```

`invokeBind` itself (`:587–621`) directly invokes the same configured callback and sets `m_passPressed` to 0 for that invocation. Lock entry in `src/managers/SessionLockManager.cpp:52–132` changes lock/focus/grab state but does not discard armed callbacks. The subscribed compositor lock callback (`src/Compositor.cpp:430–440`) does not do so either.

**Trigger boundary and prerequisites — source-confirmed.** A configured callback must have run while available and requested a release follow-up; the associated press must remain recorded across lock or session deactivation. A later physical release or device removal reaches that pre-existing callback. An unrestricted virtual-input client has the F-INP-01 access restrictions; device destruction need not satisfy keyboard focus. This cannot select an arbitrary unarmed binding or provide new Lua code. Physical unplug is an alternative trigger. An inactive-session drain occurs on a subsequent accepted keyboard event, not merely because the VT changed.

**Corrections and impact — source-confirmed.** Stock mouse drag/resize releases call `src/config/shared/actions/ConfigActions.cpp:1683–1689`:

```cpp
if (!PRESSED) {
    if (g_layoutManager->dragController()->target())
        g_layoutManager->endDragTarget();
    return {};
}
```

That is drag cleanup, not a new move operation. Likewise global/pass actions use the release phase (`ConfigActions.cpp:1528–1537`, `:1678`); cleanup of held external state may be intentional. The defect is replaying the **entire arbitrary configured callback**, whose unrelated side effects need not be release-safe. An execution-only binding does not request a follow-up by itself; a compound/custom callback must do so. Therefore the source proves boundary-crossing callback execution, not a universal locked-state command primitive or unlock. Severity is Medium because impact depends on previously armed configuration and timing. No locked desktop execution was attempted.

**Fix direction.** Separate release cleanup from replay of general Lua callbacks. On lock/activity transitions, cancel unsafe follow-ups while delivering the narrowly required drag/key/global-shortcut cleanup. Blindly dropping every release or blindly applying every availability condition can itself leave state stuck; define cleanup semantics explicitly.

## F-INP-03

**Verdict: confirmed. Revised severity: Medium (physical/backend hotplug memory-safety defect).**

The touch wrapper emits its destruction event before resetting its backend weak pointer. The destruction event can synchronously remove the wrapper's final strong owner, leaving the subsequent member access outside its lifetime.

**Evidence — source-confirmed.** `src/devices/TouchDevice.cpp:17–20` retains the problematic order:

```cpp
m_listeners.destroy = m_touch->events.destroy.listen([this] {
    m_events.destroy.emit();
    m_touch.reset();
});
```

The backend pointer is weak (`src/devices/TouchDevice.hpp:15`), and the lambda captures only raw `this`. `src/managers/input/InputManager.cpp:1980–1982` stores the wrapper in `m_touches` and a weak reference in `m_hids`; their types are `InputManager.hpp:186,190`. `src/pointer/PointerManager.hpp:166–176` similarly stores a weak touch reference, not another owning touch pointer. The setup-local `PNEWDEV` is gone after device registration returns.

The wrapper destroy listener (`InputManager.cpp:1993–2000`) temporarily locks `m_self`, calls `destroyTouchDevice`, and then returns. `destroyTouchDevice` (`:1571–1576`) erases the wrapper from `m_touches`; its parameter and the listener-local strong pointer disappear before `m_events.destroy.emit()` returns to the backend lambda. With no active event or external owner retaining the device, the wrapper is consequently destroyed before `TouchDevice.cpp:19` accesses `m_touch`.

The installed Aquamarine interface emits backend destruction in `Aquamarine::ITouch::~ITouch` (`/usr/include/aquamarine/input/Input.hpp:155–159`). This connects ordinary backend removal to the registered wrapper callback; no concurrent event or high-frequency touching is required. Active touch-down arguments can temporarily hold a strong reference (`src/devices/ITouch.hpp:20`), so the no-extra-owner precondition matters.

**Dependency evidence — source-confirmed and executed.** Hyprutils `WP::lock()` creates a temporary strong owner but the owner only lasts for that variable's scope (`/usr/include/hyprutils/memory/WeakPtr.hpp:175–185`). A `CHyprSignalListener` owns the callback, not the object captured as raw `this`; the signal declaration stores listener references (`/usr/include/hyprutils/signal/Signal.hpp:17–22,48–49`). The safe dependency probe described below confirmed strong-local lifetime and scoped listener cleanup. It did **not** reproduce this use-after-free or run a compositor.

**Reachability, severity, and corrections.** The source chain is backend touch removal → wrapper destruction emission → input-manager erase → final temporary owners leave scope → member access after wrapper destruction. A physical touch-device removal or equivalent privileged/backend teardown is required; ordinary virtual-keyboard and virtual-pointer clients cannot create this Aquamarine touch device. Medium reflects that prerequisite and the compositor memory-safety consequence. There is no evidence of code execution or a client-only trigger. The dossier's additional signal-iterator corruption hypothesis is unnecessary and unverified; it depends on the library emission implementation and is not counted.

**Fix direction.** At minimum, reset the backend weak pointer before emission, matching `src/devices/Keyboard.cpp:26–28`. More robustly retain a strong self-reference for the callback or defer owner removal until emission unwinds; review emitter lifetime too. No blanket claim is made that merely moving the reset proves every signal lifetime safe.

## F-INP-04

**Verdict: rejected. Revised severity: None for the asserted crash path.**

The key/modifier/keymap listeners omit a null check after locking `m_self`, whereas the destruction listener checks it. The dossier assumes queued events can invoke these member signals after their keyboard is destroyed, but supplies no such event source or surviving signal owner.

**Evidence and lifetime guard — source-confirmed.** The unchecked uses exist at `src/managers/input/InputManager.cpp:1138–1173`, including:

```cpp
auto PKEEB = keeb->m_self.lock();
onKeyboardKey(event, PKEEB);
```

However, these static listeners belong to `keeb->m_keyboardEvents`, not a global queue. Hyprutils explicitly documents static listeners as dying with their signal (`/usr/include/hyprutils/signal/Signal.hpp:74–76`). Physical keyboard event forwarding is synchronous (`src/devices/Keyboard.cpp:31–52`), and virtual event forwarding is synchronous (`src/devices/VirtualKeyboard.cpp:24–44`). While registered, `m_keyboards` strongly owns the keyboard (`src/managers/input/InputManager.hpp:184`). Once the listener enters, its `PKEEB` strongly retains that keyboard through the rest of its callback, including the call to `onKeyboardKey` and subsequent field accesses.

Backend/protocol subscriptions are scoped `CHyprSignalListener` members of each wrapper (`src/devices/Keyboard.hpp` and `src/devices/VirtualKeyboard.hpp`). Destruction removes these subscriptions; `CVirtualKeyboard` resets its backend weak pointer before emitting destroy (`VirtualKeyboard.cpp:19–21`). The actual keyboard destructor emits only the separate destroy signal (`src/devices/IKeyboard.cpp:34–37`); that callback has the null guard (`InputManager.cpp:1128–1134`). It does not emit key/keymap/modifier signals.

**Dependency evidence — executed.** The isolated probe confirmed that dropping a scoped listener prevents later emission delivery, static listeners live with their signal, a successfully locked strong local pins its owner, and self-locking during destruction returns empty. These checks used the installed Hyprutils 0.14.2, not a running desktop.

**Reachability and corrections.** No source chain was found from client requests or physical removal to a late member-key signal with an expired owner. A second request in the same Wayland dispatch cycle is not evidence of asynchronous delivery through a destroyed C++ member signal. The raw capture alone does not create a global listener that survives the signal. The `setupMouse` concern (`InputManager.cpp:1326`) similarly does not demonstrate a reachable null argument in normal wrapper destruction. The separate touch callback ordering defect remains F-INP-03; it does not prove a keyboard event race.

**Fix direction.** Null checks or weak captures can be useful defensive maintenance, but are not a verified fix for a demonstrated crash here. Reopen only with a concrete event producer retaining and invoking the relevant signal after keyboard lifetime ends; a hypothetical plugin breaking ownership contracts would be a different precondition.

## F-INP-05

**Verdict: partially. Revised severity: Medium (persistent-device input denial; recoverable residue after removal).**

A shared keyboard holding a key can suppress subsequent physical releases and duplicate presses in seat delivery. The dossier's permanent-disability claim conflates a still-held live device with stale aggregate state after that device is removed.

**Evidence — source-confirmed.** The virtual wrapper updates its per-keyboard pressed state before forwarding each event (`src/devices/VirtualKeyboard.cpp:24–26`); `src/devices/IKeyboard.cpp:450–467` inserts/removes the code. The default sharing policy enables non-IME virtual keyboards (`src/config/values/ConfigValues.cpp:436`; `VirtualKeyboard.cpp:49–51`), with the IME exclusion in `src/managers/input/InputManager.cpp:1802–1816`.

After binding processing and keyboard-event-handler dispatch (`InputManager.cpp:1699–1706`), merged delivery checks `shareKeyFromAllKBs` at `:1713–1724`. A release is dropped if another enabled, sharing, non-ignored keyboard still reports that code down (`:1927–1948`). The seat-output aggregate then drops duplicate presses (`:1731–1736`):

```cpp
if (CONTAINS && pressed)
    return;
if (!CONTAINS && !pressed)
    return;
```

Only a forwarded release removes that code from `CInputManager::m_pressed` (`:1738–1741`). Keyboard removal (`:1522–1545`) removes the device and its keybind/event-handler state but does not reconcile this seat-output aggregate. The protocol's optional teardown release is off by default (`ConfigValues.cpp:438`; `src/protocols/VirtualKeyboard.cpp:137–142`). `CKeyboardEventHandlerStack::onKeyboardRemoved` only erases handler-owned keys (`src/managers/SeatManager.cpp:82–83`), not the aggregate.

**Reachability and bounded state analysis — source-confirmed, not executed against a compositor.** An eligible unrestricted virtual keyboard, default sharing, an enabled/allowed device, and a key that reaches normal seat delivery are sufficient. The simplest case has no IME grab, input capture, consuming binding, or active keyboard handler. The first forwarded press inserts the code. While another live device reports it down, physical release does not remove it, and later physical presses are duplicates. Applications may also repeat that initial held key; this is not necessarily silent loss of every character.

Once the holding keyboard is removed from `m_keyboards`, it is no longer visited by the sharing loop. If no remaining device holds that code, the next physical press can be dropped because the aggregate is stale, but its subsequent release removes the residue. The following press can work normally. The same reasoning applies to a held physical keyboard being unplugged. Thus stale state is a real cleanup defect and can cost an initial press/release cycle after removal, but it is not an indefinitely unrecoverable key after the source disappears.

Keyboard focus transitions obtain their pressed array from that same aggregate (`src/managers/SeatManager.cpp:272`), so simply switching focus is not guaranteed to repair the state. Binding processing happens before seat deduplication (`InputManager.cpp:1699–1700`); loss of normal text input does not prove all bindings stop working. During session lock, ordinary bindings are gated but merged seat delivery still exists, so availability interference with lockscreen text entry is plausible from the same source path. No lockscreen/password interaction was performed.

**Severity and corrections.** Medium reflects a live local virtual-input source disrupting session input, without a demonstrated sandbox bypass or permanent lockout. The prerequisite global-access restrictions in F-INP-01 apply. The dossier's test wording itself says destroying the connection restores input, contradicting its stronger permanent claim. The protocol's own `m_pressed` list and the wrapper's list are distinct; removing the wrapper eliminates its contribution to the merge even if the aggregate still needs repair.

**Fix direction.** Track per-device contributions to forwarded seat state and synthesize a release on removal only when no remaining device owns that key. Apply the trust policy consistently to shared key/modifier state. Merely clearing every key on any removal would incorrectly release keys still physically held elsewhere.

## F-INP-06

**Verdict: partially. Revised severity: Medium (unrestricted synthetic input plus a conditional kill-mode lock gap).**

The virtual-pointer path accepts motion/button input without a separate dynamic-permission prompt. The dossier overstates global access and normal binding lock bypass, and reverses Escape's kill-mode behavior; a pre-existing kill mode nevertheless lacks a lock check at the kill endpoint.

**Evidence — source-confirmed.** The same installed global filter as F-INP-01 applies (`src/Compositor.cpp:267–271,288`; `src/managers/ProtocolManager.cpp:360–403`): security-context sandbox clients cannot access the virtual-pointer global. An unrestricted client can reach creation (`src/protocols/VirtualPointer.cpp:130–146,158–170`). Events pass through `src/devices/VirtualPointer.cpp:22–33`, input-manager setup (`src/managers/input/InputManager.cpp:1279–1282,1303–1326`), and pointer-manager subscriptions (`src/pointer/PointerManager.cpp:1081–1108`) into ordinary input processing. No per-client virtual-pointer authorization check was found in this path. Device configuration can still detach a disabled pointer (`InputManager.cpp:1339–1346`); input-capture handling can also consume events (`:728–734`).

Normal mouse bindings are processed in `InputManager.cpp:841–843` → `src/keybinds/Manager.cpp:348–387` → `processEvent`, whose ordinary candidate checks include the session lock (`Manager.cpp:470,897–906`). They do not acquire a fresh lock exemption merely because the pointer is virtual. Follow-up callbacks are the separate F-INP-02 case.

The kill-mode endpoint differs (`InputManager.cpp:750–753,923–946`):

```cpp
case CLICKMODE_KILL: processMouseDownKill(e); break;
// inside processMouseDownKill, for a press with a window hit:
kill(PWINDOW->backend().pid(), SIGKILL);
```

It does not check session lock, and its window hit-testing does not silently add one. `src/desktop/state/ViewHitTester.cpp:37–70,102–104,193–194,255–266` selects mapped/input-accepting windows on the relevant workspace; `src/desktop/view/window/Window.cpp:229–230` defines intrinsic focus availability as not hidden. `src/desktop/view/focusable/Focusable.cpp:11–28` checks intrinsic availability and input-block reasons, whose definitions (`focusable/FocusBlockReasons.hpp:13–19`) do not include session lock. This leaves a hit-testable ordinary window available even when normal lockscreen focus routing would choose a lock surface.

**Reachability and preconditions — source-confirmed.** The conditional chain is an already enabled kill mode surviving lock entry, a hit-testable window under the pointer, and a button press reaching the kill branch without capture/cancellation. Kill mode defaults to normal behavior; a relevant setup action is required. The IPC kill handler can enable it (`src/ipc/s1/Commands.cpp:1258–1261`), and `setClickMode` has no lock guard (`InputManager.cpp:816–835`). Lock entry (`src/managers/SessionLockManager.cpp:52–132`) does not clear that mode. This is a session policy/availability gap; it is not an unconditional ability for every client to kill any hidden or arbitrary off-workspace window.

**Corrections and unproved extensions.** `src/keybinds/Manager.cpp:883–894` accepts Escape only when already in kill mode and calls `setClickMode(CLICKMODE_DEFAULT)`: Escape **cancels**, rather than enables, kill mode. The dossier's proposed Escape-based setup chain is false.

Permission windows have priority (`src/managers/permissions/DynamicPermissionManager.cpp:276–277`), and motion checks priority windows before ordinary session-lock routing (`InputManager.cpp:333–343`). `src/desktop/state/FocusState.cpp:105–108` also exempts priority windows from its normal window-focus lock refusal. These facts support a synthetic-input authorization concern if an actual mapped dialog is interactable, but do not by themselves verify a button press was accepted or a particular permission granted. No GUI permission decision was automated in this audit.

The protocol handler casts button state directly (`VirtualPointer.cpp:37–42`), whereas its XML declares the allowed `wl_pointer.button_state` enum (`protocols/wlr-virtual-pointer-unstable-v1.xml:64–70`). This is an application-validation hardening issue; libwayland wire type validation is not proof of enum membership. No separate memory-safety effect is established by an arbitrary button number, and the dossier's proposed small button whitelist would require compatibility review for legitimate additional buttons.

**Fix direction.** Restrict synthetic input where sensitive prompts rely on physical user intent; preserve sandbox filtering and add explicit trust policy for unrestricted connections. Reset/cancel kill mode at lock entry and guard the destructive endpoint, including IPC entry policy. Validate protocol enum values explicitly. Keep permission-UI acceptance unverified until tested in an isolated authorized environment.

## F-INP-07

**Verdict: rejected. Revised severity: None.**

The dossier alleges unbounded `requestRelease` recursion and dangling state during nested device removal. The direct synthetic-release branch is bounded, and callback extraction avoids the asserted vector-reference lifetime problem.

**Evidence and terminating guard — source-confirmed.** `src/keybinds/Manager.cpp:610–615` explicitly requires a press before scheduling or immediately invoking a release:

```cpp
if (INPUT_KEY && pressed && result.followUp == BIND_FOLLOW_UP_TRIGGER_RELEASE) {
    // store follow-up
} else if (!INPUT_KEY && pressed && result.followUp == BIND_FOLLOW_UP_TRIGGER_RELEASE)
    invokeBind(bind, false);
```

The nested call has `pressed=false`, so it cannot enter either press-only branch again, even if the callback returns `request_release=true` again. The local automatic transition is therefore one press invocation followed by at most one synthetic release invocation. The absence of an invocation-depth maximum does not negate this Boolean terminating condition. Stateful Lua helpers also set the shared request bit only in the press phase (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:35–37`).

`src/keybinds/InputState.cpp:139–155` removes callbacks from the input record **before** invoking them, placing strong `PBind` values in an independent `std::vector<SPendingRelease>` (`InputState.hpp:33–39`). `Manager.cpp:624–638` iterates that snapshot and re-finds the input on every callback. `invokeBind` snapshots the input key and strong device before executing user code (`:588–589`) and re-finds state afterward (`:611`). Nested removal therefore does not automatically re-extract the same callbacks or invalidate a retained `SPressedInput&` in the asserted loop.

**Reachability and corrections — source-confirmed.** Input without a pressed record, such as the scroll event path (`Manager.cpp:333–343`) or switch path (`:422–437`), can reach the synthetic-release branch if the configured callback asks for it. It still terminates as above. Custom configuration can introduce its own recursive function calls, but the dossier does not identify an automatic compositor path that reinvokes arbitrary bindings indefinitely. Lua keybind userdata exposes enable/remove/query operations, not a binding-invoke method (`src/config/lua/objects/LuaKeybind.cpp:107–167`). Lua callback dispatch additionally uses protected calls and a watchdog (`src/config/lua/ConfigManager.cpp:467–491,1410–1425`), although that is not needed to prove the local branch terminates.

**Severity and fix direction.** No separate recursion or use-after-free defect is established. A callback snapshot can still execute a bind whose availability changed after extraction, but that is F-INP-02's policy question, not proof of dangling memory. Depth limits may be defense in depth; do not present one as a required fix for this rejected mechanism.

## F-INP-08

**Verdict: partially. Revised severity: Informational (duplicate low-level notification; asserted repeated cleanup rejected).**

The protocol resource has duplicate destruction notification sites. The dossier incorrectly assumes every notification will retain a live device wrapper, re-enter InputManager, and invoke the same release callbacks again.

**Evidence — source-confirmed.** `src/protocols/VirtualKeyboard.cpp:38–39` registers both request-destroy and resource-destroy handlers. `destroy()` emits at `:141` and erases the owning protocol vector entry at `:142,160–161`; the protocol resource destructor emits again at `:113–115`:

```cpp
CVirtualKeyboardV1Resource::~CVirtualKeyboardV1Resource() {
    m_events.destroy.emit();
}
```

These two emit sites alone establish a duplicate low-level notification during ordinary teardown. The two registered handlers do not themselves prove that both callbacks run sequentially on the same live C++ object; generated resource ownership must also be considered.

The device wrapper subscribes through a scoped listener (`src/devices/VirtualKeyboard.cpp:19–22`) and owns the protocol resource **weakly** (`src/devices/VirtualKeyboard.hpp:19`). The first notification resets that weak pointer and emits device destruction. `src/managers/input/InputManager.cpp:1128–1135,1522–1545` locks a strong device local, removes it from `m_keyboards`, and then releases the temporaries. In the ordinary no-extra-owner case, that destroys the wrapper and its protocol listener before the resource destructor's second emission.

`IKeyboard::~IKeyboard` also emits its device destroy signal (`src/devices/IKeyboard.cpp:34–35`), but self-locking fails during destruction, so the InputManager destroy listener returns at `InputManager.cpp:1131–1132`. The installed weak-pointer contract explicitly rejects `lock()` while destroying (`/usr/include/hyprutils/memory/WeakPtr.hpp:175–177`). Furthermore release callbacks are extracted destructively (`src/keybinds/InputState.cpp:139–155`); a repeated removal does not automatically execute the same callback twice.

**Dependency evidence — executed.** The dependency probe confirmed destructor-time self-lock rejection and scoped-listener removal. No virtual device was created or destroyed in a compositor.

**Reachability and corrections.** Teardown by an eligible unrestricted client reaches the duplicate protocol notification. No independent InputManager double-cleanup/crash was proved. An extra owner retaining the wrapper across both emissions could change notification reachability, so notifications should still be idempotent; this does not justify the dossier's unconditional two-round release-callback or elevated crash assertion.

**Fix direction.** Consolidate notification into a single guarded teardown transition, and make cleanup idempotent. Preserve resource-destruction handling for client disconnects. No extra severity should be assigned without an actual surviving subscriber harmed by the second notification.

## F-INP-09

**Verdict: partially. Revised severity: Low (configuration-dependent forwarding correctness and exception handling).**

The resolver uses a keyboard address/name cache with incomplete invalidation and unchecked `std::stoi` conversion. These defects belong to configured shortcut/key-state forwarding; the dossier incorrectly describes this resolver as the ordinary binding-match parser and assumes a configuration-registration crash.

**Evidence — source-confirmed.** `src/keybinds/Resolver.cpp:44–55` calls `std::stoi` after a numeric-syntax check without handling range errors:

```cpp
if (isNumber(key) && std::stoi(key) > 9)
    return (uint32_t)std::stoi(key);
```

The `code:` and `mouse:` branches have the same unchecked conversion. Syntactically numeric does not imply representable as `int`. The installed utility check confirmed this distinction: the isolated probe caught `std::out_of_range` for an all-digit magnitude accepted by `Hyprutils::String::isNumber` (**executed**, process remained healthy). This was an API behavior check, not an attempted compositor crash.

For symbolic names, `Resolver.cpp:60–79` uses `KB.get()` and the requested string as a cache key; a hit returns before reading the current keymap. The only call site clearing this cache is a keymap event on the **currently selected** keyboard (`src/managers/input/InputManager.cpp:1162–1168`). `destroyKeyboard` (`:1522–1545`) and `CSeatManager::setKeyboard` (`src/managers/SeatManager.cpp:218–236`) do not clear it. `IKeyboard::setKeymap` rebuilds keymap/state (`src/devices/IKeyboard.cpp:61–119`) and updates active keyboard data (`:144–148`) without emitting the keymap event that invokes that cache clear; `updateKeymapFD` (`:151–198`) also does not emit it. This permits stale cached forwarding after a configuration-driven keymap change on the same device, even without address reuse. Reuse of a destroyed physical wrapper's address by a differently mapped keyboard is a further conditional stale-identity case; allocator reuse frequency was not measured.

**Call boundaries and preconditions — source-confirmed.** The resolver has two production callers: shortcut forwarding (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:391–406`) and explicit key-state forwarding (`:409–425`). Their arguments are captured from configuration tables (`:428–462`). A named forwarding action populates the cache; a later mapping change without invalidation can cause the same configured action to forward the previous numeric code. This affects what key is sent to an application, rather than turning physical A into a different ordinary compositor binding.

For numeric errors, invoking one of these configured actions reaches the unchecked conversion. Neither caller nor `resolveKeycode` catches the C++ exception. `CConfigManager::guardedPCall` (`src/config/lua/ConfigManager.cpp:467–491`) uses `lua_pcall` and an instruction watchdog; it is not a C++ exception handler. `callLuaFnBind` (`:1410–1425`) only handles a returned Lua status. The event loop (`src/managers/eventLoop/EventLoopManager.cpp:124–137`) and normal entry (`src/main.cpp:299–300`) contain no outer exception recovery for that path. This establishes an unhandled native-exception risk when the configured action runs, not an executed compositor termination or a separate configuration-parser thread crash. No ordinary client-facing API accepting a new forwarding key string was established; the string originates in trusted configuration.

**Corrections and negative control — source-confirmed.** Ordinary bind creation instead constructs `CKey` (`src/keybinds/Bind.cpp:140–142`). That parser uses checked `strToNumber<uint32_t>` for `code:` patterns (`src/keybinds/Key.cpp:31–37`) and keysym matching (`:95–105`), not this resolver. Registering an action closure does not itself call `resolveKeycode`; the dispatcher body calls it later. The dossier's “press A triggers B binding” and registration-time parsing mechanism are therefore rejected. The stale-forwarding and unchecked-conversion problems remain Low-severity defects requiring relevant user configuration and, for stale forwarding, a mapping change/lifetime collision.

**Fix direction.** Use stable device/map generation identities or invalidate on mapping, selected-layout, and device-lifetime changes. Replace throwing numeric conversion with checked unsigned parsing and explicit range validation, returning `std::unexpected` through the existing error interface. Add focused forwarding regression coverage, including map replacement without a keymap event.

## F-INP-10

**Verdict: rejected. Revised severity: None.**

The dossier claims an ext-workspace client can choose a new workspace name and thereby trigger a configured empty-workspace command. The command hook exists, but the alleged client creation path is absent at this baseline.

**Evidence and blocking guard — source-confirmed.** `src/workspace/HLWorkspace.cpp:83–96` selects a workspace rule and executes its configured command when `m_wasCreatedEmpty` is true:

```cpp
if (self->m_wasCreatedEmpty)
    if (auto cmd = RULEFORTHIS.m_onCreatedEmptyRunCmd)
        Config::Supplementary::executor()->spawnWithRules(*cmd, self);
```

However, `src/protocols/ExtWorkspace.cpp:29` sends group capabilities **0**. The complete group constructor (`:12–51`) registers destruction callbacks and output notifications, but no create-workspace callback. Workspace handle callbacks (`:129–140`) only destroy, activate, deactivate, and assign existing workspaces. The manager's commit handler (`:257–261`) commits those existing handles; `CExtWorkspaceResource::commit` (`:229–243`) changes activation/placement, not names or creation. A source search of `ExtWorkspace.cpp/.hpp` finds no create-workspace implementation.

**Reachability and corrections — source-confirmed.** A configured empty-workspace command is an intended configuration feature. The dossier jumps from a protocol-defined request to assumed compositor support. Merely defining a request in protocol XML does not connect it to `CHLWorkspace::init`. There is no upstream source chain from the claimed client-supplied name to this execution hook. A client also does not own a separate private workspace namespace as asserted.

**Fix direction.** No fix is justified for this claimed path. If client-driven creation is added later, review capability advertisement, creation authorization, rule matching, and execution-hook policy together. This conclusion does not audit unrelated workspace-creation interfaces.

## Offline checks and audit limits

**Executed:** a standalone C++23 program, [dependency_checks.cpp](/home/wilf/data/.claude-scratch/agent-reports/work/13-input-keybinds/dependency_checks.cpp), linked to the installed Hyprutils **0.14.2**. All compiler outputs and the binary were placed in the required scratch directory; `TMPDIR` was set there for compilation. The source does not create a Wayland connection or use compositor code. The [output log](/home/wilf/data/.claude-scratch/agent-reports/work/13-input-keybinds/dependency_checks.log) records four passing checks:

1. A strong local obtained from a weak pointer pins the owner; destructor-time self-locking returns empty.
2. Destroying a scoped signal-listener handle prevents subsequent delivery on its still-live emitter.
3. Static listener captures are retained by the signal and released when that signal dies.
4. Numeric syntax acceptance does not imply `std::stoi` range acceptance; the probe catches the resulting exception locally.

**Source-confirmed:** the supplied upstream worktree HEAD is `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb`; its tracked/untracked status was clean when checked. Installed Aquamarine interface version is **0.15.1**. Header/library checks describe those installed dependencies, not a historical runtime binary loaded by the user's compositor. The installed ext-workspace specification also states that unsupported creation requests are ignored (`/usr/share/wayland-protocols/staging/ext-workspace/ext-workspace-v1.xml:143–157,212–220`), consistent with F-INP-10's missing handler and zero capability.

No compositor, ASan compositor reproduction, physical hotplug, lockscreen, permission dialog, virtual-input client, or exploit harness was run. No network, GitHub tool, session socket/IPC, live desktop input, full build, source modification, or Git-state mutation was used. Only this report and files under the assigned scratch directory were written. Cross-referenced F-PR2-01/F-PR2-02 belong to the other dossier and are not independently re-evaluated or counted here.
