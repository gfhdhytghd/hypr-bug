# F-INP-08 CVirtualKeyboardV1Resource::destroy 双通道触发：destroy 事件重复发射、destroyKeyboard 重入

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Protocol destruction emits twice, but normal teardown destroys the device listener and drains callbacks; repeated InputManager teardown is not established.

**Verdict: partially. Revised severity: Informational (duplicate low-level notification; asserted repeated cleanup rejected).**

The protocol resource has duplicate destruction notification sites. The dossier incorrectly assumes every notification will retain a live device wrapper, re-enter InputManager, and invoke the same release callbacks again.

**Evidence — source-confirmed.** `src/protocols/VirtualKeyboard.cpp:38–39` registers both request-destroy and resource-destroy handlers. `destroy()` emits at `:141` and erases the owning protocol vector entry at `:142,160–161`; the protocol resource destructor emits again at `:113–115`:

```cpp
CVirtualKeyboardV1Resource::~CVirtualKeyboardV1Resource() {
    m_events.destroy.emit();
}
```

These two emit sites alone establish a duplicate low-level notification during ordinary teardown. The two registered handlers do not themselves prove that both callbacks run sequentially on the same live C++ object; generated resource ownership must also be considered.

The device wrapper subscribes through a scoped listener (`src/devices/VirtualKeyboard.cpp:19–22`) and owns the protocol resource **weakly** (`src/devices/VirtualKeyboard.hpp:19`). The first notification resets that weak pointer and emits device destruction. `src/managers/input/InputManager.cpp:1128–1135,1522–1545` locks a strong device local, removes it from `m_keyboards`, and then releases the temporaries. In the ordinary no-extra-owner case, that destroys the wrapper and its protocol listener before the resource destructor's second emission.

`IKeyboard::~IKeyboard` also emits its device destroy signal (`src/devices/IKeyboard.cpp:34–35`), but self-locking fails during destruction, so the InputManager destroy listener returns at `InputManager.cpp:1131–1132`. The installed weak-pointer contract explicitly rejects `lock()` while destroying (`/usr/include/hyprutils/memory/WeakPtr.hpp:175–177`). Furthermore release callbacks are extracted destructively (`src/keybinds/InputState.cpp:139–155`); a repeated removal does not automatically execute the same callback twice.

**Dependency evidence — executed.** The dependency probe confirmed destructor-time self-lock rejection and scoped-listener removal. No virtual device was created or destroyed in a compositor.

**Reachability and corrections.** Teardown by an eligible unrestricted client reaches the duplicate protocol notification. No independent InputManager double-cleanup/crash was proved. An extra owner retaining the wrapper across both emissions could change notification reachability, so notifications should still be idempotent; this does not justify the dossier's unconditional two-round release-callback or elevated crash assertion.

**Fix direction.** Consolidate notification into a single guarded teardown transition, and make cleanup idempotent. Preserve resource-destruction handling for client disconnects. No extra severity should be assigned without an actual surviving subscriber harmed by the second notification.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
