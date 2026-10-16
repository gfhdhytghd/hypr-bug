# F-INP-03 Touch-device destruction listener ordering

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Touch destruction can drop the last owner during emission, then reset a member of the freed wrapper; physical/backend removal required.

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

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
