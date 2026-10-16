# F-INP-07 Release callback recursion and device-removal reentrancy

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Synthetic release recurses once with pressed=false; extracted callbacks own binds and are removed before invocation. No claimed unbounded recursion demonstrated.

**Verdict: rejected. Revised severity: None.**

The dossier alleges unbounded `requestRelease` recursion and dangling state during nested device removal. The direct synthetic-release branch is bounded, and callback extraction avoids the asserted vector-reference lifetime problem.

**Evidence and terminating guard — source-confirmed.** `src/keybinds/Manager.cpp:610–615` explicitly requires a press before scheduling or immediately invoking a release:

```cpp
if (INPUT_KEY && pressed && result.followUp == BIND_FOLLOW_UP_TRIGGER_RELEASE) {
    // store follow-up
} else if (!INPUT_KEY && pressed && result.followUp == BIND_FOLLOW_UP_TRIGGER_RELEASE)
    invokeBind(bind, false);
```

The nested call has `pressed=false`, so it cannot enter either press-only branch again, even if the callback returns `request_release=true` again. The local automatic transition is therefore one press invocation followed by at most one synthetic release invocation. The absence of an invocation-depth maximum does not negate this Boolean terminating condition. Stateful Lua helpers also set the shared request bit only in the press phase (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:35–37`).

`src/keybinds/InputState.cpp:139–155` removes callbacks from the input record **before** invoking them, placing strong `PBind` values in an independent `std::vector<SPendingRelease>` (`InputState.hpp:33–39`). `Manager.cpp:624–638` iterates that snapshot and re-finds the input on every callback. `invokeBind` snapshots the input key and strong device before executing user code (`:588–589`) and re-finds state afterward (`:611`). Nested removal therefore does not automatically re-extract the same callbacks or invalidate a retained `SPressedInput&` in the asserted loop.

**Reachability and corrections — source-confirmed.** Input without a pressed record, such as the scroll event path (`Manager.cpp:333–343`) or switch path (`:422–437`), can reach the synthetic-release branch if the configured callback asks for it. It still terminates as above. Custom configuration can introduce its own recursive function calls, but the dossier does not identify an automatic compositor path that reinvokes arbitrary bindings indefinitely. Lua keybind userdata exposes enable/remove/query operations, not a binding-invoke method (`src/config/lua/objects/LuaKeybind.cpp:107–167`). Lua callback dispatch additionally uses protected calls and a watchdog (`src/config/lua/ConfigManager.cpp:467–491,1410–1425`), although that is not needed to prove the local branch terminates.

**Severity and fix direction.** No separate recursion or use-after-free defect is established. A callback snapshot can still execute a bind whose availability changed after extraction, but that is F-INP-02's policy question, not proof of dangling memory. Depth limits may be defense in depth; do not present one as a required fix for this rejected mechanism.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
