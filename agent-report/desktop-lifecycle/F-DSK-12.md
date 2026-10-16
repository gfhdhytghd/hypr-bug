# F-DSK-12 Pinned-window focus can use a missing monitor

- Agent confirmation: `none`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: inconclusive — Pinned focus lacks a monitor guard; startup mapping is guarded and client self-pinning is unsupported. Monitor-removal event ordering still needs validation.

**Claim.** A pinned client obtains focus while the focused monitor is null, dereferencing `m_focusMonitor`; the dossier suggests an X11 ABOVE hint can self-pin it.

**Evidence — source-confirmed.** `src/desktop/state/FocusState.cpp:165–166` has the unguarded access:

```cpp
if (pWindow->m_state & WINDOW_STATE_PINNED)
    pWindow->m_workspace = m_focusMonitor->m_activeWorkspace;
```

The earlier raw-focus guard rejects null/unmapped windows (`:129–155`), but does not check the focused monitor before this assignment. `rawMonitorFocus(nullptr)` clears it (`:272–278`); the last-output disconnect also directly resets focused window/surface/monitor and then clears the monitor's active workspace (`src/output/Monitor.cpp:447–460`). Thus null focused-monitor state exists in source; it is not merely an impossible pointer value.

The proposed startup producer is blocked: mapping chooses/focuses a monitor and returns before setting `m_isMapped` if no valid monitor/workspace exists (`src/desktop/view/window/Window.cpp:1156–1168`). Pinning is a compositor action with floating/fullscreen, monitor and workspace checks (`src/config/shared/actions/ConfigActions.cpp:286–323`), or a configured pin rule (`Window.cpp:1296–1299`), with pinned-parent inheritance (`src/protocols/XDGShell.cpp:595–596`). No source mapping `_NET_WM_STATE_ABOVE` to `WINDOW_STATE_PINNED` was found. The X11 backend state-request fields carry fullscreen/maximize/minimize, not a pin request (`src/desktop/view/window/X11Backend.cpp:188–207`).

**Input/guard chain — source-confirmed up to an ordering gap.** Existing configured/action-pinned mapped window → last-output removal clears focused monitor → a later focus attempt would reach the unguarded pinned assignment if the window remained mapped and focus were processed before recovery. Normal managed-client activation is policy-gated (`Window.cpp:824–835`); raw/full focus additionally checks mapped state and workspace (`FocusState.cpp:75–78,129`). A fallback monitor is requested on removal (`src/state/FallbackState.cpp:26–39`) and initialized in deferred event-loop work (`:118–143`). That recovery must be considered, rather than assuming a stable monitorless desktop.

**Reachability / missing evidence.** A lifecycle/event-order test is needed to establish that a surviving pinned window's accepted focus request can run in the monitorless interval, and whether fallback creation/workspace migration always precedes it. The dossier supplies no such ordering evidence. Configured pinning is a prerequisite; the alleged self-pin hint is not supported. No physical/virtual monitor manipulation, IPC, or live focus test was run.

**Verdict and severity.** Inconclusive. An unguarded nullable-state access is source-confirmed, but an accepted externally triggered focus in the required interval is not. A confirmed compositor crash would be High; the dossier's Low label would then understate availability impact, while current evidence does not justify assigning a confirmed crash severity.

**Corrections / minimal fix direction.** Guard/lock the focused monitor before the pinned assignment, and define focus behavior while no active workspace is available. Add a lifecycle regression covering removal/recovery with pinned windows. Do not describe the ABOVE hint or startup mapping as established ways to reach the access.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
