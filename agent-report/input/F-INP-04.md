# F-INP-04 Keyboard listeners use unchecked weak references

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Missing null checks are real, but owner-scoped synchronous signals and strong locals break the alleged late-event crash path.

**Verdict: rejected. Revised severity: None for the asserted crash path.**

The key/modifier/keymap listeners omit a null check after locking `m_self`, whereas the destruction listener checks it. The dossier assumes queued events can invoke these member signals after their keyboard is destroyed, but supplies no such event source or surviving signal owner.

**Evidence and lifetime guard — source-confirmed.** The unchecked uses exist at `src/managers/input/InputManager.cpp:1138–1173`, including:

```cpp
auto PKEEB = keeb->m_self.lock();
onKeyboardKey(event, PKEEB);
```

However, these static listeners belong to `keeb->m_keyboardEvents`, not a global queue. Hyprutils explicitly documents static listeners as dying with their signal (`/usr/include/hyprutils/signal/Signal.hpp:74–76`). Physical keyboard event forwarding is synchronous (`src/devices/Keyboard.cpp:31–52`), and virtual event forwarding is synchronous (`src/devices/VirtualKeyboard.cpp:24–44`). While registered, `m_keyboards` strongly owns the keyboard (`src/managers/input/InputManager.hpp:184`). Once the listener enters, its `PKEEB` strongly retains that keyboard through the rest of its callback, including the call to `onKeyboardKey` and subsequent field accesses.

Backend/protocol subscriptions are scoped `CHyprSignalListener` members of each wrapper (`src/devices/Keyboard.hpp` and `src/devices/VirtualKeyboard.hpp`). Destruction removes these subscriptions; `CVirtualKeyboard` resets its backend weak pointer before emitting destroy (`VirtualKeyboard.cpp:19–21`). The actual keyboard destructor emits only the separate destroy signal (`src/devices/IKeyboard.cpp:34–37`); that callback has the null guard (`InputManager.cpp:1128–1134`). It does not emit key/keymap/modifier signals.

**Dependency evidence — executed.** The isolated probe confirmed that dropping a scoped listener prevents later emission delivery, static listeners live with their signal, a successfully locked strong local pins its owner, and self-locking during destruction returns empty. These checks used the installed Hyprutils 0.14.2, not a running desktop.

**Reachability and corrections.** No source chain was found from client requests or physical removal to a late member-key signal with an expired owner. A second request in the same Wayland dispatch cycle is not evidence of asynchronous delivery through a destroyed C++ member signal. The raw capture alone does not create a global listener that survives the signal. The `setupMouse` concern (`InputManager.cpp:1326`) similarly does not demonstrate a reachable null argument in normal wrapper destruction. The separate touch callback ordering defect remains F-INP-03; it does not prove a keyboard event race.

**Fix direction.** Null checks or weak captures can be useful defensive maintenance, but are not a verified fix for a demonstrated crash here. Reopen only with a concrete event producer retaining and invoking the relevant signal after keyboard lifetime ends; a hypothetical plugin breaking ownership contracts would be a different precondition.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
