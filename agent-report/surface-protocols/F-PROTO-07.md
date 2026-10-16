# F-PROTO-07 Duplicate surface-role creation is not rejected

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Role checks are absent, but duplicate toplevel creation aborts on CWLSurface ownership before unbounded window accumulation.

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

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
