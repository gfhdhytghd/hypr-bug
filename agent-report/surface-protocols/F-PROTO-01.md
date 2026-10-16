# F-PROTO-01 Self-parented popup can loop while accumulating offsets

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Self-parenting is accepted, but the proposed hang chain lacks a desktop popup; duplicate ownership can instead abort, covered by F-PROTO-07.

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

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
