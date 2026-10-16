# IPC / hyprctl dossier verification

- Baseline: official Hyprland `main` at `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).
- Dossier: [02-ipc-hyprctl](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md).
- Date: 2026-10-15.
- Method: adversarial source review of the pinned upstream worktree, with isolated offline checks where useful. Evidence is labeled **source-confirmed** or **executed**. No live compositor, desktop IPC, Hyprland binary, network, or full build is used. All eight finding IDs and the dossier's four unnumbered control records were reviewed. The example reports named in the brief were absent from this tracker checkout; the required report structure is followed directly.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
| --- | --- | --- | --- | --- |
| F-IPC-01 | partially | Informational / trust-boundary hardening | `src/ipc/s1/Commands.cpp:1201-1214`; `src/config/lua/ConfigManager.cpp:533` | Unrestricted owner-accessible evaluation exists; Critical, remote, and cross-user privilege-escalation claims lack a reachable boundary crossing. |
| F-IPC-02 | partially | Medium only for an unsafe external-data wrapper; otherwise informational | `src/ipc/s1/Commands.cpp:1217-1234` | Lua expression evaluation is real and intentional; injection requires an unsafe trusted wrapper, which the dossier does not identify. |
| F-IPC-03 | partially | Informational / administrative capability | `src/plugins/PluginSystem.cpp:20-77`; `src/config/values/ConfigValues.cpp:717` | Arbitrary native paths load when permission enforcement is off by default; enabled enforcement can deny or require user approval before dlopen. |
| F-IPC-04 | partially | Informational / defense in depth | `src/Compositor.cpp:196-231`; `src/ipc/s1/Unix.cpp:54-64,252-264` | Missing UID checks and socket chmod hold; mandatory 0700 instance directories block the claimed cross-user access through a shared parent. |
| F-IPC-05 | rejected | None for configuration disclosure; log access is informational | `src/config/lua/ConfigManager.cpp:1083-1085`; `src/ipc/s1/Commands.cpp:1185-1198` | Lua configuration dumps return a fixed unsupported message; rolling logs remain accessible to socket-authorized peers, without demonstrated secret exposure. |
| F-IPC-06 | partially | Low / conditional integration hazard | `hyprctl/src/main.cpp:347-356,478,532-533`; `src/ipc/s1/S1.cpp:129-158,205-210` | Batch text is intentionally raw; ordinary dispatch does not split semicolons. Injection requires external data inserted into explicit batch syntax. |
| F-IPC-07 | partially | Medium / same-user availability hardening | `src/ipc/s1/Unix.cpp:79-91,111-142,283-325`; `src/config/lua/ConfigManager.cpp:452-491,917` | Synchronous work and absent aggregate peer quotas hold; a 250 ms watchdog limits ordinary Lua loops, and input is not retained as claimed. |
| F-IPC-08 | confirmed | Low / parser robustness; no demonstrated security bypass | `src/ipc/s1/S1.cpp:46-109` | Unknown flags and command-prefix aliases are accepted; slashes after a space preserve arguments, and no authorization bypass is demonstrated. |

## F-IPC-01

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

## F-IPC-02

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

## F-IPC-03

**Claim.** Any IPC peer can load an arbitrary shared-library path without checks, obtaining native execution in the compositor; unload may leave dispatcher registrations behind.

**Verdict: partially.** Arbitrary readable paths are accepted in the default permission-disabled configuration. The dossier misses an explicit permission gate and overstates the unloaded-dispatcher claim.

**Evidence / complete chain — source-confirmed.** `src/ipc/s1/Commands.cpp:1770-1794` parses the operation and path and passes the real socket peer PID to:

```cpp
g_pPluginSystem->loadPlugin(PATH, SPECIAL_PID_TYPE_NONE, pid)
```

`src/plugins/PluginSystem.cpp:20-64` first calls `clientPermissionModeWithString(..., path, PERMISSION_TYPE_PLUGIN)`. A pending decision waits for a promise; errors and denial reject the request (`25-42`), and only an allow decision reaches `loadPluginInternal` (`46` or `59`). `src/helpers/defer/Promise.hpp:98-107` executes the promise body immediately and delivers already-resolved results to a later `then`; the use of a promise is not a bypass of this check.

`src/managers/permissions/DynamicPermissionManager.cpp:152-156` returns allow if `ecosystem:enforce_permissions` is false. That option defaults to false at `src/config/values/ConfigValues.cpp:717`. With enforcement enabled, configured/cached allow and deny rules are applied (`DynamicPermissionManager.cpp:172-219`); an unmatched plugin request asks and returns pending (`222-229`). Thus default socket-authorized peers can reach the loader, but a deny rule or denied prompt blocks this direct chain before native loading. This is a source statement about control flow, not validation of the permission dialog subsystem under all failure conditions. It is also only a gate on this plugin-loading route: the unrestricted evaluation interface in F-IPC-01 means enabling plugin permission checks alone is not an authorization boundary for the entire administrative socket.

`src/plugins/PluginSystem.cpp:67-104` checks duplicate paths, then calls `dlopen(path.c_str(), RTLD_LAZY)` at line 77. There is no directory allowlist or signature verification there. Export/API checks come afterward; they do not protect against library initialization that happens during `dlopen`. No library was compiled or loaded in this review.

**Reachability / severity.** The peer needs traversal of the owner-only instance directory plus a library path readable by the compositor; default permission enforcement does not add a prompt. This is an intentional same-user native-extension capability, with no demonstrated different-UID or sandbox boundary crossing. Revised severity is informational for the standalone claim. If an integration exposes this socket as a restricted or read-only API, native loading violates that integration's assumption, but that deployment is not shown here.

**Corrections — source-confirmed.** The claim that no authorization exists is false. Default-disabled authorization and absent authorization are distinct facts. ABI validation is present but follows `dlopen`. The statement that dispatcher registrations may remain after unload is unsupported for the tracked list: `PluginSystem.cpp:164-166` iterates `m_registeredDispatchers` and calls `HyprlandAPI::removeDispatcher`; `168-175` also unregisters IPC commands and invokes provider cleanup. This does not prove the safety of every conceivable plugin-owned callback, and that broader audit is outside this dossier.

**Minimal fix direction.** Document that the IPC socket can load native extensions, keep it private, and enable permission enforcement where approval is required. If stronger policy is desired, use a correctly enforced trusted-library source policy and test the deny/pending/allow paths. Disabling the entire feature or calling an arbitrary writable-directory allowlist a security boundary is not justified by this report.

## F-IPC-04

**Claim.** Neither IPC socket checks peer UID or sets owner-only socket permissions, so a shared or nonstandard runtime directory permits another local user to execute commands or read events.

**Verdict: partially.** Missing defense-in-depth checks are real; the proposed cross-user trigger omits the private instance directory and does not close.

**Evidence — source-confirmed.** `src/ipc/s1/Unix.cpp:54-64` obtains peer credentials but returns only `credentials.CRED_PID`. `Unix.cpp:252-264` binds and listens without changing socket mode. Socket2 likewise binds and listens at `src/ipc/s2/Unix.cpp:92-112` and accepts peers without a UID comparison at `Unix.cpp:120-147`.

The decisive guard is in `src/Compositor.cpp:222-231`:

```cpp
m_instancePath = std::format("{}/{}", m_hyprTempDataRoot, m_instanceSignature);
// Existing instance path causes startup failure.
if (mkdir(m_instancePath.c_str(), S_IRWXU) < 0) {
    // Startup failure.
}
```

`S_IRWXU` requests 0700. Umask can remove permission bits; it cannot add group/other search permission. Existing instance paths are rejected (`Compositor.cpp:224-227`), so simply precreating a permissive instance directory is not the described path. The parent is also created as 0700 when absent (`Compositor.cpp:215-219`), although preexisting parents are accepted without checking ownership/mode. Missing or invalid runtime roots bail out at `Compositor.cpp:196-202`; a nonstandard root merely warns at `204-205`.

**Reachability / blocked chain — source-confirmed.** A different UID can potentially enumerate a permissive runtime parent and learn the signature, but pathname resolution to either socket must still traverse the newly created 0700 instance directory. The kernel denies that traversal before `accept4` or any command handler. Knowledge of the signature changes no permission. A same-UID peer can traverse the directory; that is the administrative scope discussed in F-IPC-01. Root or a principal able to alter the victim's directory is outside this claimed ordinary cross-user trigger.

**Severity and corrections.** Informational hardening, not a demonstrated High cross-user execution or disclosure defect. On Linux, pathname stream socket connection also requires socket write permission; a typical 0022 umask gives a socket mode of 0755, not world-writable. The dossier alternates between 0777 and 0666 creation masks; pathname sockets use 0777 filtered by umask. Neither a 0755 runtime parent nor a permissive umask defeats the nested 0700 guard. Active replacement of directories under a hostile writable parent is a different threat requiring separate race, ownership, and filesystem analysis; it is not established by this finding.

**Minimal fix direction.** Checking UID before registering either peer, setting socket mode to 0600, and validating runtime-directory ownership would improve defense in depth. Requiring the literal `/run/user/<uid>` pathname is unnecessary: owner-only, correctly owned nonstandard runtime roots can be valid. Any permissions change should target the pathname or appropriate filesystem handle; ordinary socket `fchmod` must not be assumed to chmod the pathname inode on Linux.

## F-IPC-05

**Claim.** The `c/systeminfo` request returns all Lua configuration text, including credentials; a separate follow request continuously exposes logs and window titles.

**Verdict: rejected** for the tracker entry's configuration-disclosure defect. Log access exists, but does not restore the claimed configuration dump or prove disclosure of a secret.

**Evidence / full chain — source-confirmed.** `src/ipc/s1/S1.cpp:67-80` recognizes the `c` flag and sets `includeConfig`; the exact `systeminfo` handler is registered in `src/ipc/s1/Commands.cpp:1982`. `Commands.cpp:1185-1198` appends config markers around `Config::mgr()->getConfigString()`. However, the only selected provider is Lua (`src/config/ConfigManager.cpp:29-35`) and its implementation is:

```cpp
// src/config/lua/ConfigManager.cpp:1083-1085
std::string CConfigManager::getConfigString() {
    return "Not supported under lua";
}
```

Thus an authorized socket request reaches a fixed placeholder, not source files, imported Lua modules, or embedded tokens. The original finding stops before following this virtual method to its implementation.

**Separate log behavior — source-confirmed.** `Commands.cpp:1012-1022` returns the rolling log; `1984-1987` selects follow mode when requested. `src/ipc/s1/Unix.cpp:364-388` starts follow delivery and polls at 100 ms; `src/debug/log/RollingLogFollow.hpp:23-29,44-48` queues generated log records. This is an explicit diagnostic feature (`hyprctl/src/Strings.hpp:47-48`). Sensitive strings could appear when generated: for example, `src/config/supplementary/executor/Executor.cpp:174` logs command text. Logging defaults to disabled (`src/config/values/ConfigValues.cpp:726`; `src/debug/log/Logger.hpp:43-49` and `Logger.cpp:51-60` gate later logging), so continuously collecting new private titles is not an unconditional default behavior. Startup records or previously generated records may still be present; none were inspected.

**Reachability / severity / corrections.** Both paths require access through the 0700 instance directory described in F-IPC-04. No different-UID exposure is established. Configuration disclosure is absent upstream; owner-accessible log diagnostics are informational and should not be represented as confirmed credential leakage. No actual user's config, log, or credential was read for this verification.

**Minimal fix direction.** No configuration-dump fix is required at this baseline. Keep future config export explicitly opt-in, omit secrets from diagnostic logs, and document logs as sensitive when users share them. Any stricter per-application log policy would require an IPC authorization model.

## F-IPC-06

**Claim.** Unescaped `;` and backslash in hyprctl arguments let ordinary dispatch arguments become extra IPC commands, with broader script-dependent injection through batch mode.

**Verdict: partially.** Explicit batch text has a delimiter/escaping contract; the ordinary single-request example does not activate that parser.

**Evidence — source-confirmed.** `hyprctl/src/main.cpp:478,486-488` joins arguments with spaces, removes the trailing space, and adds the flag prefix. `main.cpp:433-434,532-533` selects explicit batch mode; `347-356` takes the supplied batch command text and prepends `[[BATCH]]`. It deliberately preserves batch syntax. `hyprctl/src/Strings.hpp:69` documents `--batch` as executing semicolon-separated commands.

The server's gate is decisive (`src/ipc/s1/S1.cpp:205-210`):

```cpp
if (request.starts_with(BATCH_TOKEN))
    return dispatchBatch(std::move(request), pid);
return dispatchSingle(std::move(request), pid);
```

Only `dispatchBatch` splits on bare semicolons (`S1.cpp:135-141`), and it consumes the supported backslash escapes (`144-150`) before calling `dispatchSingle` for each piece (`156-158`). The normal parser/dispatcher does not split semicolons. A syntax error or Lua behavior in an ordinary dispatch argument is a separate F-IPC-02 question, not proof of batch delimiter injection.

**Full conditional chain / reachability — source-confirmed.** An external string can affect extra commands if a trusted caller places it, without batch escaping, inside text intentionally sent through `--batch` or the batch protocol token. The client forwards that text, the server selects `dispatchBatch`, the tokenizer identifies delimiters, and it dispatches the resulting command pieces. That chain requires the trusted wrapper/data insertion precondition; no affected wrapper from the repository or a third-party tool was supplied. Shell quoting preserves an argument across the shell but does not escape this separate batch grammar. The resulting commands retain the peer's ordinary socket authority, including plugin permission checks where enabled.

**Offline check — executed.** An extracted-method harness under the assigned scratch directory compiled with `g++ -std=c++23 -Wall -Wextra -Werror` and passed 10 benign parser/tokenizer checks; see `parsing-results.txt`, `parsing_check.cpp`, `check_parsing.py`, and `extraction.txt`. Exact upstream `parseRequest` and the batch tokenization body were used; logging was suppressed and ASCII trimming was stubbed. Checks include preservation of a semicolon in a normal parsed argument, splitting two diagnostic names in explicit batch mode, literal semicolon/backslash escapes, and a trailing backslash. This is offline grammar evidence, not end-to-end hyprctl or compositor execution. Batch selection itself is established by the source gate above.

**Severity and corrections.** Low / conditional integration hazard. Passing untrusted text as a batch program can cause unintended command selection; automatically escaping the entire batch argument would also disable the documented command separator. The dossier's ordinary-dispatch example does not prove two IPC commands run. The `starts_with` command-boundary issue is independently covered by F-IPC-08; it does not enable the batch parser.

**Minimal fix direction.** Preserve the documented raw batch API and document escaping for literal data; prefer a structured multi-command API when callers must pass arbitrary argument values. Fix specific wrappers that mix external text with batch syntax. Do not apply blanket batch escaping to ordinary single-request arguments, where backslashes and semicolons can be legitimate data.

## F-IPC-07

**Claim.** Synchronous eval or heavy queries can freeze the compositor indefinitely, while unlimited connections each retain 1 MiB of input plus 64 KiB of follow output.

**Verdict: partially.** Main-loop execution and missing application-level aggregate quotas are real. The plain unbounded-bytecode-loop example and the per-connection retained-memory calculation omit existing guards and state transitions.

**Evidence / execution chain — source-confirmed.** `src/ipc/s1/Unix.cpp:264` registers the listener with `m_wlEventLoop`; `66-67` registers each peer with that loop. The client callback reads a request and directly invokes `m_requestHandler` at `Unix.cpp:316-325`; `src/ipc/s1/S1.cpp:39-41,85-109,205-210` dispatches it synchronously. Response promises support deferred results, but they do not automatically move handler CPU work to another thread (`src/helpers/defer/Promise.hpp:98-107`; `Unix.cpp:145-157`). A slow synchronous handler therefore delays compositor event processing.

**Existing CPU guard — source-confirmed.** `src/config/lua/ConfigManager.cpp:917` calls:

```cpp
guardedPCall(0, LUA_MULTRET, 0, LUA_TIMEOUT_EVAL_MS, "hyprctl eval")
```

`src/config/lua/ConfigManager.hpp:118-125` sets an instruction interval of 10,000, eval timeout of 250 ms, and dispatch timeout of 100 ms. `ConfigManager.cpp:452-491` installs a count hook, checks the steady-clock deadline, raises a Lua error when it expires, and restores the previous hook/state. `533-542` also removes the ordinary debug hook setter/getter. This breaks the dossier's simple uninterrupted bytecode-loop trigger: an ordinary loop is expected to return an error around the deadline at the next hook opportunity. This is a source conclusion, not measured desktop timing or a proof that hostile Lua is safely confined. An instruction hook is not a preemptive timer for blocking native library operations, Lua compilation, or every other C++ handler; the opened native libraries therefore cannot be assigned a universal wall-clock bound from this guard. No bypass or blocking payload was tested.

**Existing memory/lifetime guards — source-confirmed.**

- `src/ipc/s1/Unix.cpp:43-44` defines a 1 MiB request cap and 64 KiB incremental follow-output cap. `111-142` appends while bytes arrive, rejects input above the cap, then moves nonempty input to the request as soon as the current read drains. It does not retain a partially supplied 1 MiB request on an idle peer waiting for a length terminator. Nonempty input is dispatched; holding a connection open does not reserve a full megabyte.
- `Unix.cpp:79-91` starts a five-second request timer and `138-141` resets that timer when a nonempty request is dispatched. This is a request-acquisition timeout, not an execution, reply-drain, deferred-response, or follow-session deadline. `src/managers/eventLoop/EventLoopManager.cpp:142-146,191-195` skips timers with no owning reference and later prunes them, so resetting the peer's timer does stop its later callback from enforcing an execution deadline.
- `Unix.cpp:283-300` drains accepts and stores peers without an application peer-count limit. `listen(..., 10)` at line 258 is a pending-connection backlog, not an active-peer quota. Kernel/process FD and memory limits still exist; “no cap” here means no IPC-specific quota, not literally infinite allocatable resources.
- `Unix.cpp:165-170` enforces 64 KiB when appending follow data. Initial replies at `145-162` have no equivalent size check. Furthermore the upstream log producer first accumulates per-follower text in `src/debug/log/RollingLogFollow.hpp:23-29`; that staging string is drained later at `Unix.cpp:391-402`. Thus 64 KiB is not a bound on all log-related storage in every phase.
- `Unix.cpp:151-155,350-353` locks weak peers and checks membership before completing deferred work. A disconnected peer is not kept alive merely by that weak continuation. No dangling-pointer or use-after-free claim follows from the connection-limit observation.

**Reachability / severity / corrections.** An ordinary same-UID process with socket access can submit work or hold multiple peers. Other UIDs remain blocked by the private instance directory (F-IPC-04). Per-request CPU work runs on the event loop and batch entries execute sequentially (`S1.cpp:156-158`) without an aggregate batch time budget. This supports Medium availability-hardening priority for aggregate work and connection quotas. It does not prove a compositor crash, a persistent hang from the supplied simple loop, or the asserted hundreds-of-megabytes retention scenario. No RSS increase, exhaustion threshold, responsiveness loss, or crash was measured; query-specific pathological costs are not established here.

**Minimal fix direction.** Add conservative active/deferred/follow peer quotas, admission and batch-work budgets, and bounded producer-side log queues. Bound reply lifetime/output where practical. Keep the existing Lua watchdog; do not describe adding it as a missing fix. If arbitrary native operations remain available, their blocking behavior requires separate scheduling or capability policy rather than assuming instruction hooks provide hard preemption.

## F-IPC-08

**Claim.** Unknown request flags are ignored, slash syntax can change command interpretation, and prefix commands match without a token boundary.

**Verdict: confirmed** as a Low parser-robustness defect, with the URL example and security implications corrected below. Acceptance and misclassification are established by source and benign offline controls; no separate injection or authorization-bypass defect is claimed.

**Evidence — source-confirmed.** `src/ipc/s1/S1.cpp:46-83` scans characters preceding the first slash. It sets flags for `j`, `r`, `a`, `c`, and `f` at `67-76` but has no unknown-character rejection branch. If a space occurs first, `60-62` sets `separator` to the entire request length and prevents command truncation. Otherwise `79-80` drops everything through the slash.

Exact command matches are attempted first (`S1.cpp:90-95`). Prefix commands then use:

```cpp
// src/ipc/s1/S1.cpp:99
parsed.command.starts_with(command->name)
```

There is no requirement for a space or end-of-string after the registered name. Registrations such as `dispatch` at `src/ipc/s1/Commands.cpp:2006` are prefix commands, so a longer command name beginning with that token can select the same handler. `dispatchRequest` strips through its first space (`Commands.cpp:1218-1219`), so a suffixed command token followed by ordinary arguments can reach the normal dispatch body. This aliases a spelling; it does not raise the peer's authorization.

**Full chain / preconditions — source-confirmed.** Socket-readable bytes follow `src/ipc/s1/Unix.cpp:316-325` into `CSocket1::dispatch` and `dispatchSingle` (`S1.cpp:205-210,85-109`). The parsed flags and shortened command are used directly for handler selection. No external source beyond the socket peer is needed to observe tolerant parsing. The private instance directory and normal administrative authority still apply; there is no claimed inaccessible command unlocked by these aliases.

**Counterexamples / offline results — executed.** The same extracted-method harness described in F-IPC-06 passed these controls:

| Benign input | Observed parsed result |
| --- | --- |
| `zz/clients` | `clients`, normal format; unknown letters ignored |
| `jrx/clients` | `clients`, JSON format and refresh enabled; only `x` ignored |
| `dispatch open-folder/http://x` | Full command preserved; a space precedes the slash |
| `dispatch foo/bar` | Full command preserved for the same reason |
| `dispatch/bar` | Command becomes `bar`; `a` and `c` in the prefix set flags |

These are parser-only checks. No request was sent to a compositor, and no command handler was executed. Prefix-handler selection is source-confirmed rather than executed. The first local run had an incorrect test expectation that the word `dispatch` contained an `r`; the expectation was corrected, the unchanged extracted source was rebuilt, and all 10 checks passed. That was a harness correction, not evidence of a source change.

**Severity and corrections.** Low robustness/API ambiguity. The dossier incorrectly says `jrx/clients` keeps default formatting: recognized flags are still honored. Its URL example contains a space before the slash, so that example is not truncated. No-space slash strings are interpreted as the protocol's flag prefix followed by a command; the concrete defect is that arbitrary flag words are tolerated, not that every slash in a URL or argument is consumed. No actual filter or privilege check depending on canonical command spellings was found in this path, and batching remains separately gated (F-IPC-06).

**Minimal fix direction.** Reject unknown flag letters, require a token boundary for prefix commands, and add parser regressions for slash-after-space, mixed valid/invalid flags, and suffixed command tokens. Preserve supported `j/r/a/c/f` syntax and documented batch escaping; stricter rejection is a compatibility change that should be documented.


## Dossier's unnumbered control records

These records have no finding IDs in the dossier; they are retained here rather than inventing additional tracker IDs.

| Control record | Verification |
| --- | --- |
| Private randomized runtime directory; no legacy global temporary-directory socket layout | **Source-confirmed:** `src/Compositor.cpp:196-231` uses the environment-derived runtime root, exports a signature containing commit/time/random-device-seeded PRNG output, rejects an existing instance path, and creates the instance directory as 0700. Both socket paths use that instance directory (`src/ipc/s1/Unix.cpp:241-242`; `src/ipc/s2/Unix.cpp:92-93`). This layout is present upstream too; random naming is not a substitute for permissions or a cryptographic authentication token. **Executed:** isolated `mkdir(0700)` checks under umasks 000, 022, and 077 all yielded 0700 (`mode-results.txt` in scratch). These tests inspect only newly created scratch directories, not the running session. |
| Socket1 request cap and five-second timeout; socket2 event queue cap | **Source-confirmed, narrowed:** `src/ipc/s1/Unix.cpp:43,79-91,111-142` enforces the input cap and request-acquisition timer. `src/ipc/s2/Unix.cpp:186-206` removes peers with 64 queued events, and `src/ipc/s2/S2.cpp:17-22` caps event data to 1024 bytes and normalizes newlines. These are per-peer/path guards, not aggregate quotas or proof of an absence of every integer/reading defect. See F-IPC-07 for the timer reset and follow-queue staging limits. |
| systemd units have no demonstrated privilege/template-injection defect | **Source-confirmed:** `systemd/hyprland-uwsm.desktop:1-7` starts `uwsm` with fixed arguments; `systemd/hyprland-session.target:1-6` declares session dependencies; `example/hyprland.service:1,7-12` is explicitly a user-service example with fixed `/usr/bin/Hyprland`. The `%p` at line 3 affects the description, not an interpolated shell command. These files contain no elevated `User=` or shell-based variable-execution path supporting the dossier's threat. No running systemd unit or installed file was inspected. |
| Socket2 uses `sockaddr_in` with AF_UNIX accept | **Source-confirmed:** `src/ipc/s2/Unix.cpp:131-147` supplies an IPv4-shaped buffer and its size to `accept4`, but does not inspect the returned address. It is an inappropriate address type, not an established buffer overflow: accept's address buffer is length-delimited and the returned address is unused. Prefer null address arguments, as socket1 does at `src/ipc/s1/Unix.cpp:284`, or the correct address type. No live socket test was performed. |

## Verification scope and disposition

All source locations above refer to the pinned upstream checkout, whose HEAD was verified as `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb`. The dossier's assertion that these Lua/IPC paths are predominantly fork-only cannot be used at this baseline: the referenced mechanisms exist in upstream, while the configuration-export implementation and existing permission/watchdog guards materially change several conclusions.

The source review confirms an administrative IPC trust model and some hardening gaps. It establishes no remote, ordinary different-UID, root-escalation, or sandbox-escape exploit. Intentional evaluation/native-extension features must not be counted as independently demonstrated Critical execution vulnerabilities merely because a socket-authorized process can invoke them. The Low confirmed parser record is a concrete robustness finding, without an established security-boundary bypass.

Executed evidence is limited to the 10 benign extracted parser/tokenizer checks and three scratch-directory mode checks. The extraction metadata and results remain under `/home/wilf/data/.claude-scratch/agent-reports/work/02-ipc-hyprctl/`. No source changes, full builds, compositor launches, desktop connections, injected input, shared-library loads, GitHub interactions, commits, or git-state changes were made.

Final verdict counts: **confirmed 1; partially 6; rejected 1; inconclusive 0**. No Critical or High finding is sustained by this verification.
