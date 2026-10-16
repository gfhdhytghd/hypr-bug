# F-DSK-05 Backend listeners can outlive their window

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Listeners are RAII handles; backend ownership and automatic member destruction prevent the claimed post-window callback.

**Claim.** Raw-`this` backend/surface callbacks survive window destruction unless `destroyWindow()` explicitly unregisters them, enabling later callbacks into freed memory.

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:133–150` does capture `this`, but `src/desktop/view/window/Window.hpp:298–317` owns the backend with `UP<IWindowBackend>` and owns every listener with `CHyprSignalListener`. The listener aggregate is declared after the backend, so normal member destruction releases the handles before destroying the backend. Installed hyprutils 0.14.2 `/usr/include/hyprutils/signal/Signal.hpp:21–22,48–55` stores ordinary listeners weakly and explicitly documents the lifetime contract:

```cpp
std::vector<CWeakPointer<CSignalListener>> m_vListeners;
[[nodiscard("Listener is unregistered when the ptr is lost")]]
CHyprSignalListener listen(...);
```

`src/desktop/view/WLSurface.cpp:31–32` calls `destroy()` from its destructor; `:140–156` resets the destroy-listener handle and resource. Even if its early return applies, automatic member destruction still releases the handle. The backend is owned by the window, whereas XWM owns the lower-level X surface; these are different objects (`src/xwayland/XWM.cpp:60–64`).

**Trigger/guard chain — source-confirmed.** Client protocol/X events → lower-level resource signal → owned backend → window-owned listener. Window destruction releases the handles and owned backend; later resource events cannot reach those released listeners. The only source call to `windowState()->removeSafe()` is `Window.cpp:1825`. The alleged monitor/workspace teardown calls do not exist; `windowState()->clear()` appears in compositor shutdown (`src/Compositor.cpp:579,595–607`), with shutdown already set.

**Verdict and severity.** Rejected; no demonstrated post-destruction UAF. This does not certify every possible synchronous callback reentrancy; it refutes the dossier's specific surviving-listener argument.

**Corrections / minimal fix direction.** Explicit reset is not the only unregister mechanism. No defect fix is required for this claim; locking a weak self in callbacks can be optional lifetime hardening where synchronous ownership release is possible.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
