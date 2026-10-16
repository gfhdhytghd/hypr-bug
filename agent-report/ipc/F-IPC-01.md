# F-IPC-01 IPC Lua evaluation exposes unrestricted execution

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unrestricted owner-accessible evaluation exists; Critical, remote, and cross-user privilege-escalation claims lack a reachable boundary crossing.

**Claim.** IPC `eval` / `repl` executes arbitrary Lua with the compositor's authority, characterized as Critical RCE and potentially cross-user execution.

**Verdict: partially.** The execution capability is present upstream and enabled without a development switch. The dossier does not establish a privilege escalation from a less privileged principal.

**Evidence — source-confirmed.**

- `src/ipc/s1/Commands.cpp:2010-2011` registers both commands. `Commands.cpp:1201-1214` checks only that the provider is Lua, strips the command name, and calls `luaMgr->eval(code, request.starts_with("repl "))`.
- `src/config/ConfigManager.cpp:29-35` selects the Lua provider in both initialization branches; this is not a special fork-only opt-in.
- `src/config/lua/ConfigManager.cpp:530-542` calls `luaL_openlibs(m_lua)` and removes `debug.sethook` / `debug.gethook`, not the OS, file, or package libraries. `ConfigManager.cpp:907-917` compiles the supplied text and executes it in the current configuration state.
- `src/config/lua/bindings/LuaBindingsToplevel.cpp:316-329` exposes process spawning through the executor; `src/config/supplementary/executor/Executor.cpp:173-205` forks and reaches `execl("/bin/sh", "/bin/sh", "-c", args.c_str(), nullptr)`. That path is additional capability evidence, not a tested execution chain.
- `hyprctl/src/Strings.hpp:22,45-46` explicitly documents Lua execution and REPL. This is an administrative interface by design.

**Reachability / complete chain — source-confirmed.** A process with filesystem access to the instance socket connects; `src/ipc/s1/Unix.cpp:283-300` accepts it, `Unix.cpp:316-325` passes its bytes and peer PID to the handler, `src/ipc/s1/S1.cpp:39-41,85-109,205-210` parses and dispatches them, and `evalRequest` reaches the Lua state. The legacy command wrapper (`Commands.cpp:1959-1965`) forwards format and text but does not impose a permission decision. The instance directory is created with `S_IRWXU` (`src/Compositor.cpp:222-231`); an ordinary different UID cannot traverse it. Same-UID applications outside a sandbox normally share the user's file authority already. Merely being a Wayland client does not grant an application filesystem access through its sandbox, nor does Wayland itself provide this environment variable; `Compositor.cpp:213` exports the signature to descendants.

**Severity and corrections.** Informational / trust-boundary hardening, rather than Critical. This verifies a powerful same-user control surface, not remote execution, root escalation, or a sandbox escape. A deployment that deliberately exposes this socket across a sandbox boundary must treat it as full compositor control; no such deployment is demonstrated here. Config persistence follows existing write authority, not a new privilege transition. The F-IPC-04 cross-user chain is blocked by the instance directory. The Lua watchdog (250 ms, discussed in F-IPC-07) constrains ordinary bytecode duration but is not a capability sandbox.

**Minimal fix direction.** Document the socket as an administrative capability and withhold it from untrusted sandboxes. If per-application restrictions are a product goal, authorize evaluation explicitly and separate it from read-only queries. Removing a few standard libraries alone would not confine the native `hl` API. No executable payload, plugin, or live session action was used.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
