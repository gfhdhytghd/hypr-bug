# F-DSK-01 Destroyed windows remain in groups as expired references

- Agent confirmation: `none`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: inconclusive — Destroy lacks fallback group cleanup, but ordinary role/X11 teardown unmaps first; no client-reachable bypass established.

**Claim.** Destruction bypasses group removal, leaving expired weak members that later group visibility/workspace/focus operations dereference.

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:1809–1825` calls swallow cleanup, unassigns the surface, clears backend listeners, removes the window target and removes the window from state, without explicitly removing group membership. `unmapWindow()` does remove it (`:1668–1669`), but returns early if the surface does not exist or the window is already unmapped (`:1608–1611`). `src/desktop/view/Group.cpp:414–426,461–470` and `Window.cpp:1064–1075` directly access stored weak members. Thus **if** a previously grouped window really reaches deletion without unmap cleanup, later accesses are unsafe.

The omitted role-level guard matters. `src/protocols/XDGShell.cpp:520–543` emits XDG unmap before destroy for a mapped XDG surface, including underlying `wl_surface` destruction:

```cpp
if (m_mapped)
    m_events.unmap.emit();
m_mapped = false;
m_surface.reset();
m_events.destroy.emit();
```

`src/desktop/view/window/WaylandBackend.cpp:131–135,151–160` forwards those signals. `Window.cpp:1624–1669` processes swallowing and removes group membership before `m_isMapped` becomes false (`:1676`). Null-buffer unmap follows the same chain (`XDGShell.cpp:567–570`). The core surface's destructor emitting only destroy (`src/protocols/core/Compositor.cpp:295–298`) does not itself prove skipped **XDG** unmap: the XDG surface-destroy listener supplies it. XDG registers this listener before creating the window (`XDGShell.cpp:533,593`); the window installs its own surface listener later (`Window.cpp:126–127`, `src/desktop/view/WLSurface.cpp:169`).

X11 also explicitly unmaps before surface dissociation (`src/xwayland/XSurface.cpp:86–93,112–115`; `src/xwayland/XWM.cpp:157–164,1235–1243`). `XWM.cpp:68–77`'s destroy notification handler itself lacks that fallback, but showing that a genuinely mapped/grouped window can reach it without the expected preceding unmap remains necessary.

**Dependency evidence — executed.** The isolated `dependency_contracts.cpp` check under the assigned scratch directory passed with installed hyprutils 0.14.2: listeners ran in registration order; a weak pointer remained valid and returned its object during the destructor, became false/null after deletion, and could not be locked during destruction. This corrects the dossier's conflation of destroying and deleted pointers. The test does not execute Hyprland or prove any resource-destruction ordering there.

**Reachability / limits.** Grouping requires a configured group rule or compositor action; ordinary Wayland map/unmap and ordinary X11 unmap do not establish the bad state. The dossier's proposed batching/disconnection race supplies no concrete role/backend state transition defeating the guards. A compositor event-loop ordering/lifecycle test demonstrating a retained grouped member after skipped unmap would settle this; none was performed.

**Verdict and severity.** Inconclusive as a reachable defect. The missing defensive fallback and unsafe consumer assumptions are source-confirmed; the client-to-expired-member producer is not. A proven local-client compositor crash would be High, not Critical.

**Corrections / minimal fix direction.** Do not label arbitrary surface destruction an established bypass. If hardening this lifecycle, make final membership cleanup idempotent and handle expired entries by stored identity; preserve ordinary unmap/focus/fullscreen ordering. Do not blindly add layout resurrection to final destruction.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
