# F-CFG-01 Monitor-rule integer conversion can throw

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Configuration / Lua / ICC helpers. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Throwing conversions remain, but have no upstream callers; Lua uses bounded integer fields and direct assignment.

**Verdict: rejected.** The dossier alleges that oversized monitor transform/VRR inputs reach uncaught `std::stoi` calls through Lua configuration or IPC, terminating the compositor. The unsafe conversion bodies remain, but the asserted input path does not exist at this baseline.

**Evidence — source-confirmed.** `src/config/shared/monitor/Parser.cpp:215-228` still calls `std::stoi(value)` after `isNumber`, then checks the transform range. `Parser.cpp:266-274` similarly converts VRR without a catch. These are latent hazards if called with an out-of-range numeric string. However, a repository-wide symbol search finds only the declarations (`Parser.hpp:16,21`) and definitions, with **no calls** to either method in upstream `src/`.

The actual monitor descriptors in `src/config/lua/bindings/LuaBindingsConfigRules.cpp:103-107,135-140` use:

```cpp
{"transform", []() -> ILuaConfigValue* { return new CLuaConfigInt(0, 0, 7); },
 // apply assigns the already parsed integer directly to m_transform
{"vrr", []() -> ILuaConfigValue* { return new CLuaConfigInt(-1, -1, 3); },
 // apply assigns the already parsed integer directly to m_vrr
```

`src/config/lua/types/LuaConfigInt.cpp:28-30` rejects string input without an enum map; `:49-60` checks integer minimum/maximum before storing it. `src/config/lua/bindings/LuaBindingsConfigRules.cpp:1157-1162` only invokes a field's apply callback after `val->parse(L)` succeeds:

```cpp
auto err = val->parse(L);
if (err.errorCode != PARSE_ERROR_OK)
    self->addError(...);
else if (!desc->apply(val.get(), parser))
    self->addError(...);
```

**Trigger chain and guard — source-confirmed.** A user-loaded Lua file or authorized IPC Lua evaluation can call `hl.monitor`; `hlMonitor` resolves the field descriptor, parses its bounded integer value, and assigns the typed field. Bad strings or out-of-range integers produce configuration errors and never enter the alleged `std::stoi` functions. This is a configuration/IPC input boundary, not an ordinary Wayland-client request. No config default enables the missing call path.

**Severity and corrections.** None for the claimed reachable abort at `e2ad066c9`. The fork-specific assertion that Lua directly calls both parsers is stale. The distinction is reachability, not whether `std::stoi` can throw. The full baseline symbol search, rather than merely the presence of `lua_pcall`, is the basis for rejecting this finding.

**Minimal fix direction.** Remove unused conversion methods, or use checked conversion before any future caller is introduced. No crash fix is required for the current Lua descriptor path. No vulnerability-triggering execution was performed.

[Full dossier-level verification report](_evidence/full-audit-04-config-lua-helpers.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md). These remain agent evidence, not human-confirmed material.
