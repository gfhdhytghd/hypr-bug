# F-PROTO-02 Popup grab / reposition uses expired surface references

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`, plus empirical reproduction in the nested harness; current-version and physical-display acceptance must be established separately.

- Empirical: Empirically reproduced: nested compositor SIGABRT (status 134) after an xdg_popup grab on a destroyed content surface; also crashed the real XPS session (SIGABRT, watchdog respawn).

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Destroy-before-role leaves a live popup with expired content; grab dereferences it. Reposition also lacks a content-lifetime guard. [EXECUTED nested SIGABRT (+real-session crash)]

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

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
