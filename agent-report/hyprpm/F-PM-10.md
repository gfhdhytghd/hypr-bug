# F-PM-10 Informational: plugin-manager design concerns

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Build trust and permissive validators exist; deprecated notification calls a null manager, but stateless notify does not establish the claimed deterministic crash.

**Claim.** This informational bundle alleges arbitrary trusted build commands, an overly broad name validator, PATH-based privilege-tool selection, a deprecated-flag null-pointer crash, and permissive pin hash lengths.

**Build trust — source-confirmed.** `hyprpm/src/core/Manifest.cpp:51–55,114–118` retains build commands, and `PluginManager.cpp:330–338,834–842` executes them as shell commands in the user's context. This is the intended native-plugin build model, not a sandbox escape. The once-only general disclaimer is at `163–170`, but each add still has an explicit repository/author trust prompt (`178–189`). Update does not repeat it (`main.cpp:136–158`). An update-review option is reasonable hardening; the trust model itself is informational. Privileged build execution is separately assessed in F-PM-03.

**Names — source-confirmed.** `hyprpm/src/core/Manifest.cpp:7–8` allows `=` and accepts the empty string through `all_of`; the comment's narrower description is inaccurate. Plugin destinations are built as `<name>.so` (`DataState.cpp:103`) and passed in an argument vector (`Sys.cpp:161`), so an equals sign alone is not evidence of shell assignment or path traversal. No concrete unexpected placement or security impact from `=` is established. Require nonempty names and remove unintended characters for consistency, without promoting this observation to an exploit.

**Privilege-tool lookup — source-confirmed.** `hyprpm/src/helpers/Sys.cpp:41–59` searches PATH and accepts a regular file with others-execute permission, without an ownership/integrity check. `subin` (`65–84`) stores the bare tool name, not the resolved full path. `cacheSudo` (`107–111`) and the other helpers then invoke that selected name. Control of an earlier PATH directory or executable therefore affects program selection, but requires compromising a trusted caller environment/filesystem component; it is not a new way for an arbitrary different UID to gain privileges under a safe PATH. More ordinary build/dependency executables are also PATH-selected. Resolve and retain approved absolute helper paths where the threat model requires it; do not claim this prevents all environment/toolchain compromise.

**Deprecated flag — source-confirmed, crash not established.** `hyprpm/src/core/PluginManager.hpp:92` declares a default-null global `unique_ptr`. Parsing `--notify-fail`/`-nn` reaches `main.cpp:61–64` before construction at `94`:

```cpp
g_pPluginManager->notify(ICON_INFO, 0, 10000, "...");
```

The local CLI flag is a complete source trigger for an invalid non-static member call on a null object. However, `CPluginManager::notify` is nonvirtual (`PluginManager.hpp:67`) and its body (`PluginManager.cpp:1089–1091`) uses no instance fields: it directly calls `NHyprlandSocket::send`. Thus C++ undefined behavior is confirmed, while a guaranteed machine-code null-memory dereference or deterministic crash is **not** established by this source. Typical unsanitized compilation can reach the socket routine without reading `this`; an instrumentation/build-specific result would be needed for a definite crash claim. No actual hyprpm execution was used, since that path attempts IPC. Even if a particular build crashes, the affected process is the user's invoked CLI, not the compositor.

**Hash validation — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:59–61` checks hexadecimal characters without a nonempty or exact-length constraint. Manifest pin callers (`284,801`) quote the resulting value (`291,810`), so this is not a demonstrated shell-injection path. Empty, abbreviated or excessively long IDs can produce Git errors or ambiguous revision behavior, and command success/identity checks should be improved. It does not justify a separate remote-execution severity.

**Verdict and minimal fixes.** Partially: the design/validation observations and undefined deprecated-flag call hold; the asserted deterministic DoS and unsupported equals-sign impact do not. Low for the local correctness defect, otherwise informational. Remove the deprecated branch or defer notifications until manager construction, narrow names/hash syntax to the supported contract, retain approved helper paths, and make update trust behavior explicit. No crash or live-session behavior is claimed as executed evidence.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
