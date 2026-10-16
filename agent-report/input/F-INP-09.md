# F-INP-09 Keybinds::CResolver 键码缓存以裸指针为键，热插拔指针复用返回过期 keycode；resolveKeycode 数字解析未防溢出

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Cache and numeric conversion defects affect configured shortcut forwarding; ordinary binding matching uses different parsing. No client-supplied binding-string path established.

**Verdict: partially. Revised severity: Low (configuration-dependent forwarding correctness and exception handling).**

The resolver uses a keyboard address/name cache with incomplete invalidation and unchecked `std::stoi` conversion. These defects belong to configured shortcut/key-state forwarding; the dossier incorrectly describes this resolver as the ordinary binding-match parser and assumes a configuration-registration crash.

**Evidence — source-confirmed.** `src/keybinds/Resolver.cpp:44–55` calls `std::stoi` after a numeric-syntax check without handling range errors:

```cpp
if (isNumber(key) && std::stoi(key) > 9)
    return (uint32_t)std::stoi(key);
```

The `code:` and `mouse:` branches have the same unchecked conversion. Syntactically numeric does not imply representable as `int`. The installed utility check confirmed this distinction: the isolated probe caught `std::out_of_range` for an all-digit magnitude accepted by `Hyprutils::String::isNumber` (**executed**, process remained healthy). This was an API behavior check, not an attempted compositor crash.

For symbolic names, `Resolver.cpp:60–79` uses `KB.get()` and the requested string as a cache key; a hit returns before reading the current keymap. The only call site clearing this cache is a keymap event on the **currently selected** keyboard (`src/managers/input/InputManager.cpp:1162–1168`). `destroyKeyboard` (`:1522–1545`) and `CSeatManager::setKeyboard` (`src/managers/SeatManager.cpp:218–236`) do not clear it. `IKeyboard::setKeymap` rebuilds keymap/state (`src/devices/IKeyboard.cpp:61–119`) and updates active keyboard data (`:144–148`) without emitting the keymap event that invokes that cache clear; `updateKeymapFD` (`:151–198`) also does not emit it. This permits stale cached forwarding after a configuration-driven keymap change on the same device, even without address reuse. Reuse of a destroyed physical wrapper's address by a differently mapped keyboard is a further conditional stale-identity case; allocator reuse frequency was not measured.

**Call boundaries and preconditions — source-confirmed.** The resolver has two production callers: shortcut forwarding (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:391–406`) and explicit key-state forwarding (`:409–425`). Their arguments are captured from configuration tables (`:428–462`). A named forwarding action populates the cache; a later mapping change without invalidation can cause the same configured action to forward the previous numeric code. This affects what key is sent to an application, rather than turning physical A into a different ordinary compositor binding.

For numeric errors, invoking one of these configured actions reaches the unchecked conversion. Neither caller nor `resolveKeycode` catches the C++ exception. `CConfigManager::guardedPCall` (`src/config/lua/ConfigManager.cpp:467–491`) uses `lua_pcall` and an instruction watchdog; it is not a C++ exception handler. `callLuaFnBind` (`:1410–1425`) only handles a returned Lua status. The event loop (`src/managers/eventLoop/EventLoopManager.cpp:124–137`) and normal entry (`src/main.cpp:299–300`) contain no outer exception recovery for that path. This establishes an unhandled native-exception risk when the configured action runs, not an executed compositor termination or a separate configuration-parser thread crash. No ordinary client-facing API accepting a new forwarding key string was established; the string originates in trusted configuration.

**Corrections and negative control — source-confirmed.** Ordinary bind creation instead constructs `CKey` (`src/keybinds/Bind.cpp:140–142`). That parser uses checked `strToNumber<uint32_t>` for `code:` patterns (`src/keybinds/Key.cpp:31–37`) and keysym matching (`:95–105`), not this resolver. Registering an action closure does not itself call `resolveKeycode`; the dispatcher body calls it later. The dossier's “press A triggers B binding” and registration-time parsing mechanism are therefore rejected. The stale-forwarding and unchecked-conversion problems remain Low-severity defects requiring relevant user configuration and, for stale forwarding, a mapping change/lifetime collision.

**Fix direction.** Use stable device/map generation identities or invalidate on mapping, selected-layout, and device-lifetime changes. Replace throwing numeric conversion with checked unsigned parsing and explicit range validation, returning `std::unexpected` through the existing error interface. Add focused forwarding regression coverage, including map replacement without a keymap event.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
