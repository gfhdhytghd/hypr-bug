# F-DSK-13 Deferred window-map work can target a destroying window

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Weak validity differs during destruction, but idle execution cannot overlap synchronous window destruction on the compositor event loop.

**Claim.** The size-animation begin callback queues raw-`this` work whose weak-boolean check permits execution against a partially destroyed window.

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:562–574` contains the quoted `doLater([this, self = m_self] { if (!self) return; sendWindowSize(); })`. Installed `/usr/include/hyprutils/memory/WeakPtr.hpp:154–190,221–226` confirms that boolean validity remains true during destruction, whereas `lock()` rejects that state. It also shows why “expired always means operator-> returns null” is incorrect: `get()` checks the data pointer, not the destroying flag.

`src/managers/eventLoop/EventLoopManager.cpp:215–235` queues work and registers a Wayland **idle** callback; only that callback runs the queued functions:

```cpp
m_idle.fns.emplace_back(NEW_SEQ, fn);
m_idle.eventSource = wl_event_loop_add_idle(m_wayland.loop, ...);
// idle callback:
for (auto& f : fns) {
    if (f.second)
        f.second();
}
```

**Trigger/guard chain — source-confirmed.** Mapped-window size animation → enqueue idle work → next idle dispatch. A synchronous window destructor completes before control returns to the same event loop. If destruction happened in between, the retained weak handle has no live data and `!self` returns. If the window remains alive, the callback accesses a live object. The inspected `CWindow` destructor (`Window.cpp:165–175`) does not dispatch a nested event loop. No background-thread destruction or nested-loop trigger was established.

**Verdict and severity.** Rejected as a reachable destroying-window UAF. Different pointer semantics alone do not produce concurrent execution. This is source reasoning, not a runtime race test.

**Corrections / minimal fix direction.** Capturing only the weak handle and calling through `if (auto w = self.lock())` is clearer and provides a strong lifetime lease, but is defensive hardening rather than proof of the claimed crash. Animation ownership is not an independently established exploit path.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
