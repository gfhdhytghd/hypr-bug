# F-PERM-03 Dialog privileges are keyed only by process ID

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: PID-only recognition exists; live PID collision is impossible, X11 focus is excluded, and claimed guaranteed lock-screen takeover is unproved.

**Claim.** Dialog recognition by numeric PID lets a process cycle through PIDs and obtain ten-year close protection and priority focus, including through X11. **Verdict: partially: the PID-only recognition is real; the proposed guaranteed takeover is rejected.**

**Evidence — source-confirmed.** `src/helpers/AsyncDialogBox.cpp:12,28-40` keeps `(pid_t, WP<CAsyncDialogBox>)` entries. `isAsyncDialogBox` compares only the numeric PID; `isPriorityDialogBox` additionally checks a valid weak pointer and its priority flag. `src/desktop/view/window/Window.cpp:1351-1352` applies ten-year close protection without an X11 exclusion:

```cpp
if (CAsyncDialogBox::isAsyncDialogBox(m_backend->pid()))
    m_closeableSince = Time::steadyNow() + std::chrono::years(10);
```

`Window.cpp:1083-1084` explicitly excludes X11 from priority focus:

```cpp
return !m_backend->isX11() && CAsyncDialogBox::isPriorityDialogBox(m_backend->pid());
```

**Identity chain and guards — source-confirmed.** A mapped Wayland window gets its PID through `wl_client_get_credentials` in `src/desktop/view/window/WaylandBackend.cpp:111-116,227-228`, not client metadata. A dialog PID is entered after its process starts (`AsyncDialogBox.cpp:127-134`) and removed when its stdout pipe reports hangup/error (`83-90`). PID allocation cannot give an unrelated live process the still-live dialog's PID; cycling through the PID namespace does not circumvent uniqueness. On normal dialog completion the registry is cleared. The weak pointer in the table does not extend lifetime; the explicit `m_selfReference` does so until the hangup handler resets it (`AsyncDialogBox.cpp:122,139,90`; `src/helpers/AsyncDialogBox.hpp:54-56`). Installed hyprutils weak-pointer validity/lock guards are at `/usr/include/hyprutils/memory/WeakPtr.hpp:154-180`.

**X11 correction — source-confirmed.** `src/xwayland/XSurface.cpp:9-39` obtains a PID through XRes `LOCAL_CLIENT_PID`, and `src/desktop/view/window/X11Backend.cpp:140,238-239` copies/returns it. This is not a path that reads `_NET_WM_PID` into the backend PID at this baseline. Absence of an X11 exclusion from the close-protection test is accurate, but does not establish that an arbitrary property value can impersonate the helper. X11 is excluded from the priority-focus path regardless.

**Reachability and severity.** Low hardening issue: numeric identity has no process-start or client-connection binding. A misclassification would require the registry to survive the real process's death long enough for PID reuse and a replacement window map, for example if stdout remained open elsewhere or event handling were delayed. This additional stale-entry lifetime and successful reuse are not established for the genuine helper here. There is no source-closed attack from ordinary PID cycling while the real helper is alive. If a window did satisfy priority recognition, `src/desktop/state/FocusState.cpp:105-115` skips its raw-focus session-lock/exclusive-layer checks; that conditional branch does not prove a replacement client can satisfy it or that a window is visible over a lock screen. No focus, lock-screen, PID-reuse, or visual experiment was performed.

**Effect limits and minimal direction — source-confirmed.** The ten-year timestamp gates the normal close action (`src/config/shared/actions/ConfigActions.cpp:200-208`), not all ways to terminate the application: the separate kill action at `213-220` does not consult it. Prefer binding the helper's window to an authenticated client identity, validating process lifetime, and explicitly deciding whether X11 dialog recognition is supported. Do not describe an untested PID reuse race as guaranteed, cheap, or an unkillable ransomware window.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
