# F-IPC-02 IPC dispatch interpolates input into Lua source

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Lua expression evaluation is real and intentional; injection requires an unsafe trusted wrapper, which the dossier does not identify.

**Claim.** IPC `dispatch` interpolates arbitrary input into Lua source, allowing code injection with Critical compositor-level impact and amplification through third-party scripts.

**Verdict: partially.** The text-to-code path is source-confirmed; an independent privilege boundary or affected third-party caller is not. The command explicitly accepts Lua dispatch expressions at this baseline.

**Evidence — source-confirmed.** `src/ipc/s1/Commands.cpp:1217-1234` strips the command name and does:

```cpp
std::string evalStr = std::format("return hl.dispatch({})", in);
auto ret = luaMgr->eval(evalStr).value_or("ok");
```

The following comment and diagnostic (`1227-1231`) explicitly describe the Lua dispatch syntax. `src/config/lua/bindings/LuaBindingsToplevel.cpp:347-369` obtains a dispatcher or function and runs it. `src/config/lua/bindings/LuaBindingsDispatcherUtils.cpp:109-127` deliberately accepts a Lua function as well as a dispatcher object. Therefore dispatch is already a programming interface, rather than a fixed dispatcher name plus opaque argument vector.

**Full chain / preconditions — source-confirmed.** A socket-authorized process supplies the request; the transport and dispatcher chain is the one traced in F-IPC-01. `Commands.cpp:2006` registers the handler; `1219-1225` places its remaining bytes into the Lua expression before `src/config/lua/ConfigManager.cpp:907-917` compiles and runs it. There is no escaping layer or permission gate in that path. Lua evaluates argument expressions before the dispatcher-object validation, so that validation does not turn the surrounding source into inert data. This is reachable under the normally selected Lua provider (`src/config/ConfigManager.cpp:29-35`).

**Counterchecks and corrections — source-confirmed.** The numeric first call in the dossier's proposed example is not a valid dispatcher: `LuaBindingsDispatcherUtils.cpp:118-120` rejects it. However, this does **not** by itself invalidate the subsequent expression path: `LuaBindingsToplevel.cpp:348-349` calls `Internal::configError`, and `LuaBindingsInternal.cpp:356-365,456-460` records an error and returns zero Lua values rather than throwing a Lua exception. In expression context that leaves a nil result. A failure message must not be mistaken for proof that earlier or later expression side effects were prevented. No exploit payload was executed or refined in this review.

**Severity.** Informational for a direct caller already holding the administrative socket capability. Medium would be appropriate for a demonstrated trusted script that inserts externally controlled text into a Lua expression while intending that text to be data; no such actual script or data source is provided here. Assertions about widespread bar/menu exploitation are unverified. Neither remote access nor a different-UID execution path is established (F-IPC-04), so Critical is unsupported.

**Minimal fix direction.** Keep expression execution explicitly documented as code. For callers that need opaque arguments, provide or use a typed data interface and avoid building Lua source from external strings. Replacing this API with `hl.dispatch(arg1)` where `arg1` is merely a string, as suggested in the dossier, is not a drop-in repair: the current binding accepts dispatcher objects or functions, not dispatcher command strings.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
