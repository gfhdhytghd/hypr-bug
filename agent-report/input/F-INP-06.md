# F-INP-06 Virtual-pointer access and session-lock checks

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unrestricted pointer injection and prearmed kill-mode lock gap hold; sandbox clients are filtered, normal binds remain gated, Escape cancels kill mode.

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

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
