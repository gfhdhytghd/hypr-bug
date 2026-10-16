# Permission dialogs, permission caching, and execution tokens: upstream verification

- Baseline: **`e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb`** (`e2ad066c9`), verified from the detached upstream checkout.
- Dossier: [14-permission-dialogs-tokens](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md).
- Date: **2026-10-15**.
- Method: read-only source tracing at the pinned baseline, including callers, lifecycle guards, and installed dependency interfaces. No full build, compositor execution, desktop connection, IPC, or exploit reproduction. Evidence labeled *source-confirmed* establishes source behavior; it does not imply runtime or visual acceptance. *Executed* is reserved for isolated verification commands.
- Source root (read-only): `/home/wilf/data/.claude-scratch/agent-reports/upstream/`. Scratch evidence: `/home/wilf/data/.claude-scratch/agent-reports/work/14-permission-dialogs-tokens/`.
- Scope: the ten dossier records below; references to other dossiers are context, not independently verified findings.

## Summary

All ten records verified: **4 confirmed, 5 partially, 1 rejected, 0 inconclusive**. The confirmed count includes the informational default-policy record. No runtime crash, focus takeover, visual phishing, or resource exhaustion was reproduced.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
| --- | --- | --- | --- | --- |
| F-PERM-01 | confirmed | Medium (conditional trust-path issue); Low (pending-state failure) | `src/helpers/AsyncDialogBox.cpp:16,72,86,103`; `src/managers/permissions/DynamicPermissionManager.cpp:303` | Dialog stdout controls decisions and malformed completion leaves PENDING; executable substitution additionally requires control of the compositor’s helper search path. |
| F-PERM-02 | partially | Medium (descriptor isolation); Xwayland scenarios rejected | `src/config/supplementary/executor/Executor.cpp:183`; `src/helpers/AsyncDialogBox.cpp:105`; `src/xwayland/Server.cpp:48,335,351,373` | Dialog pipe inheritance is real and spawn lacks descriptor cleanup; Xwayland sockets use CLOEXEC and its notify writer is scoped to startup. |
| F-PERM-03 | partially | Low (identity hardening); claimed deterministic takeover rejected | `src/helpers/AsyncDialogBox.cpp:28,32,87,134`; `src/desktop/view/window/Window.cpp:1083,1351` | PID-only recognition exists; live PID collision is impossible, X11 focus is excluded, and claimed guaranteed lock-screen takeover is unproved. |
| F-PERM-04 | confirmed | Low | `src/managers/permissions/DynamicPermissionManager.cpp:173,233,238,364`; `src/plugins/PluginSystem.cpp:21` | Plugin permission cache uses requester PID, path and type without process-start identity or death cleanup; keyboard calls use a sentinel, not process PID. |
| F-PERM-05 | confirmed | Informational | `src/config/values/ConfigValues.cpp:717`; `src/managers/permissions/DynamicPermissionManager.cpp:83,155` | Enforcement defaults off and both permission APIs immediately allow; this is an explicit default policy, not an independent privilege-escalation defect. |
| F-TOK-01 | partially | Informational (control); Low (retention bound) | `src/managers/TokenManager.cpp:13,27,34`; `src/protocols/XDGActivation.cpp:35,42,50,84` | UUID generation and lazy expiry hold; activation consumes its authorization entry, while retained live token resources can outgrow the delivery-list cap. |
| F-RULE-02 | rejected | None for the alleged fixed-secret bypass | `src/config/supplementary/executor/Executor.cpp:66,131,195`; `src/desktop/rule/windowRule/WindowRule.cpp:466` | Launch tokens are random per spawn and rules are consumed on mapping; the alleged plaintext configuration secret and arbitrary self-declaration are absent. |
| F-RULE-03 | confirmed | Low (performance; magnitude unmeasured) | `src/desktop/view/window/Window.cpp:753`; `src/desktop/rule/windowRule/WindowRule.cpp:466`; `src/desktop/rule/windowRule/WindowRuleApplicator.cpp:536` | Eligible execution-token rules reread process environments synchronously during mapping; no cache exists, but latency and denial-of-service magnitude remain unmeasured. |
| F-PERM-06 | partially | Low (text integrity); visual exploit unverified | `src/managers/permissions/DynamicPermissionManager.cpp:240`; `src/i18n/Engine.cpp:219,1822`; `src/helpers/AsyncDialogBox.cpp:103` | Unescaped variable substitution into markup-bearing text is confirmed; visual phishing depends on the external renderer, and virtual-keyboard names are derived rather than arbitrary. |
| F-PERM-07 | partially | High (conditional null dereference); fail-open claim rejected | `src/helpers/AsyncDialogBox.cpp:16`; `src/managers/permissions/DynamicPermissionManager.cpp:276`; `src/Compositor.cpp:911` | Helper lookup weaknesses hold, but a null dialog is dereferenced before fallback: conditional compositor crash, not allow-all; startup also warns. |

## F-PERM-01

**Claim.** Permission decisions trust a PATH-resolved helper's stdout; an empty or abnormal response can leave permission pending. **Verdict: confirmed, with narrower trust and persistence claims.**

**Evidence — source-confirmed.** `src/helpers/AsyncDialogBox.cpp:15-19` checks for `hyprland-dialog` using `executableExistsInPath`. `AsyncDialogBox.cpp:103,111-115` starts the same bare command name and routes its stdout to a pipe. `AsyncDialogBox.cpp:59-90` appends arbitrary bytes to `m_stdout` and resolves that string on hangup/error, without checking exit status or validating the result:

```cpp
m_stdout += std::string_view{(buf.data()), sc<size_t>(ret)};
// on hangup/error:
m_promiseResolver->resolve(m_stdout);
```

`src/managers/permissions/DynamicPermissionManager.cpp:233,299-318` initializes a rule to PENDING, accepts localized choice prefixes, and has no fallback assignment when none matches:

```cpp
if (result.starts_with(ALLOW_ONCE))
    r->m_allowMode = PERMISSION_RULE_ALLOW_MODE_ALLOW;
// DENY and ALLOW_AND_REMEMBER branches follow
else if (result.starts_with(ALLOW))
    r->m_allowMode = PERMISSION_RULE_ALLOW_MODE_ALLOW;
```

**Caller chain and preconditions — source-confirmed.** A valid client capture request goes through `src/protocols/Screencopy.cpp:64-65,135`, `src/managers/screenshare/ScreenshareSession.cpp:187-193`, and `src/managers/screenshare/ScreenshareManager.cpp:27-40` to `src/managers/screenshare/ScreenshareFrame.cpp:162`. With enforcement enabled, no applicable allow/deny/pending rule, and a discoverable helper, `DynamicPermissionManager.cpp:119-149,232-286` creates and opens the prompt. On helper stdout closure, the callback changes the rule according to its textual prefix. Subsequent checks at `DynamicPermissionManager.cpp:135-143` return that state. Empty/unrecognized completion leaves PENDING; `ScreenshareFrame.cpp:163-168` returns without delivering the frame while pending. The external promise, if present, resolves with PENDING and is reset rather than producing a terminal denial (`DynamicPermissionManager.cpp:314-318`). No response timeout exists in this path. Disconnecting the Wayland client removes its rule (`DynamicPermissionManager.cpp:17-34,364-365`), so “permanent” means for that retained client/rule, not across all clients or restarts.

**Dependency check — executed.** The installed hyprutils is **0.14.2**, not pinned as part of Hyprland's commit. Static inspection of `/usr/lib/libhyprutils.so`, `CProcess::runAsync`, shows its final command dispatch calls `execvp` (offset `0x50ed6`), corroborating PATH resolution for this installed dependency. The disassembly is saved in the designated scratch directory as `installed-hyprutils-process-disassembly.txt`; no helper or compositor was launched. Hyprland's bare command name and PATH precheck are independently source-confirmed.

**Severity and corrections.** Medium only with an attacker-writable directory already on the compositor's effective search path, or an otherwise compromised helper. A client's own environment does not change an already-running compositor's PATH. Same-user executable substitution is a deployment/environment trust issue, not an unauthenticated Wayland-only bypass. Malformed completion is a Low availability defect. “Allow and remember” only sets `m_binaryPath` on an in-memory rule (`DynamicPermissionManager.cpp:308-310`); it does not write permanent configuration, and the originating client destructor removes that rule. The default disables enforcement (F-PERM-05). A missing helper takes the different, faulty null-handling path described in F-PERM-07.

**Minimal fix direction.** Resolve a trusted packaged helper consistently, validate one exact choice with a bounded response, handle spawn/exit/error and empty output as terminal denial, and add a response timeout. A nonce echoed by a substituted executable does not authenticate that executable: it sees the nonce and can echo it too. Nonce-only handshakes would not repair helper identity.

## F-PERM-02

**Claim.** General command spawning leaks non-CLOEXEC descriptors, including the dialog pipe and Xwayland's private channels. **Verdict: partially: dialog read-descriptor inheritance holds; the named Xwayland channels are guarded.**

**Evidence — source-confirmed.** The entire child path in `src/config/supplementary/executor/Executor.cpp:183-208` restores resource limits and the signal mask, sets environment variables, redirects stdout/stderr, and invokes `/bin/sh`. It has no close-all/allowlist operation:

```cpp
int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
// dup2 onto stdout/stderr, close devnull
execl("/bin/sh", "/bin/sh", "-c", args.c_str(), nullptr);
```

`src/helpers/AsyncDialogBox.cpp:105-113` uses plain `pipe(outPipe)` and stores its read end in `m_pipeReadFd` without setting `FD_CLOEXEC`. The parent closes the write end after a successful spawn (`AsyncDialogBox.cpp:136-137`), but does not close/reset the read end on completion (`AsyncDialogBox.cpp:83-90`); the owning box can remain referenced by its permission rule.

**Dependency check — executed.** The isolated scratch program `dependency_checks.cpp` wraps a fresh ordinary pipe with the installed hyprutils 0.14.2 `CFileDescriptor` and checks `getFlags()`. Output: `wrapped_pipe_read_cloexec=0`. Thus the wrapper does not silently repair this flag in the installed dependency. No session descriptors were inspected or inherited by this check. Installed-library disassembly also shows the integer constructor stores the descriptor without a flag-changing call.

**Reachability — source-confirmed.** User startup requests use the executor at `Executor.cpp:41-42`; general `spawn`/`spawnRaw` entry points converge at `Executor.cpp:82-95,128-132,173`. If an executor launch occurs while a dialog read endpoint is retained, the fork child inherits that endpoint and it survives the shown shell exec. A program eventually launched by the shell may close extraneous descriptors itself; inheritance to every descendant regardless of its own cleanup is not guaranteed. The boundary failure is that Hyprland passes an unrelated descriptor to the shell. An inherited read end could consume dialog output and disturb permission completion. A read end alone cannot inject response bytes or keep the pipe's writer count nonzero. Enabling permission enforcement is required for permission dialogs, but ANR dialogs also use this helper (`src/managers/ANRManager.cpp:188-203`).

**Counterevidence — source-confirmed.** `src/xwayland/Server.cpp:42-51` sets `FD_CLOEXEC` on newly created listening sockets and fails creation if that fails. Both ends of the Wayland socketpair are protected at `Server.cpp:335-339`, and both XWM endpoints at `351-355`. `Server.cpp:303-318,384-390` clears selected flags only in the dedicated fork child immediately before launching Xwayland; that does not clear the parent's descriptor flags. The ready pipe read end is protected at `375-379`. Its deliberately inheritable write end is a local `CFileDescriptor` at `373`, goes only to the Xwayland child at `390`, and is destroyed as `start()` returns at `394-395`. There is no event-loop yield in that successful parent startup sequence enabling an unrelated executor launch before its scope ends. The listening socket's filesystem mode is unrelated to descriptor inheritance.

**Severity and corrections.** Medium for unintended capability/descriptor exposure; the concrete dialog effect shown is Low disruption of approval delivery. Reject claims that arbitrary launched applications inherit the enumerated live Xwayland channels or can write the ready pipe through ordinary event-loop launches. The dossier omitted the explicit CLOEXEC setters and scoped writer lifetime. No Xwayland channel interference or permission-result manipulation was executed.

**Minimal fix direction.** Create the dialog pipe with close-on-exec protection and explicitly retain only the intended child stdout endpoint. Add a descriptor allowlist/close-all boundary to general spawning, with portability handling. Retain the existing Xwayland child-only exceptions.

## F-PERM-03

**Claim.** Dialog recognition by numeric PID lets a process cycle through PIDs and obtain ten-year close protection and priority focus, including through X11. **Verdict: partially: the PID-only recognition is real; the proposed guaranteed takeover is rejected.**

**Evidence — source-confirmed.** `src/helpers/AsyncDialogBox.cpp:12,28-40` keeps `(pid_t, WP<CAsyncDialogBox>)` entries. `isAsyncDialogBox` compares only the numeric PID; `isPriorityDialogBox` additionally checks a valid weak pointer and its priority flag. `src/desktop/view/window/Window.cpp:1351-1352` applies ten-year close protection without an X11 exclusion:

```cpp
if (CAsyncDialogBox::isAsyncDialogBox(m_backend->pid()))
    m_closeableSince = Time::steadyNow() + std::chrono::years(10);
```

`Window.cpp:1083-1084` explicitly excludes X11 from priority focus:

```cpp
return !m_backend->isX11() && CAsyncDialogBox::isPriorityDialogBox(m_backend->pid());
```

**Identity chain and guards — source-confirmed.** A mapped Wayland window gets its PID through `wl_client_get_credentials` in `src/desktop/view/window/WaylandBackend.cpp:111-116,227-228`, not client metadata. A dialog PID is entered after its process starts (`AsyncDialogBox.cpp:127-134`) and removed when its stdout pipe reports hangup/error (`83-90`). PID allocation cannot give an unrelated live process the still-live dialog's PID; cycling through the PID namespace does not circumvent uniqueness. On normal dialog completion the registry is cleared. The weak pointer in the table does not extend lifetime; the explicit `m_selfReference` does so until the hangup handler resets it (`AsyncDialogBox.cpp:122,139,90`; `src/helpers/AsyncDialogBox.hpp:54-56`). Installed hyprutils weak-pointer validity/lock guards are at `/usr/include/hyprutils/memory/WeakPtr.hpp:154-180`.

**X11 correction — source-confirmed.** `src/xwayland/XSurface.cpp:9-39` obtains a PID through XRes `LOCAL_CLIENT_PID`, and `src/desktop/view/window/X11Backend.cpp:140,238-239` copies/returns it. This is not a path that reads `_NET_WM_PID` into the backend PID at this baseline. Absence of an X11 exclusion from the close-protection test is accurate, but does not establish that an arbitrary property value can impersonate the helper. X11 is excluded from the priority-focus path regardless.

**Reachability and severity.** Low hardening issue: numeric identity has no process-start or client-connection binding. A misclassification would require the registry to survive the real process's death long enough for PID reuse and a replacement window map, for example if stdout remained open elsewhere or event handling were delayed. This additional stale-entry lifetime and successful reuse are not established for the genuine helper here. There is no source-closed attack from ordinary PID cycling while the real helper is alive. If a window did satisfy priority recognition, `src/desktop/state/FocusState.cpp:105-115` skips its raw-focus session-lock/exclusive-layer checks; that conditional branch does not prove a replacement client can satisfy it or that a window is visible over a lock screen. No focus, lock-screen, PID-reuse, or visual experiment was performed.

**Effect limits and minimal direction — source-confirmed.** The ten-year timestamp gates the normal close action (`src/config/shared/actions/ConfigActions.cpp:200-208`), not all ways to terminate the application: the separate kill action at `213-220` does not consult it. Prefer binding the helper's window to an authenticated client identity, validating process lifetime, and explicitly deciding whether X11 dialog recognition is supported. Do not describe an untested PID reuse race as guaranteed, cheap, or an unkillable ransomware window.

## F-PERM-04

**Claim.** String-keyed permission decisions can outlive a requester and be inherited after PID reuse. **Verdict: confirmed for plugin requests; the keyboard example is incorrect.**

**Evidence — source-confirmed.** `src/managers/permissions/DynamicPermissionManager.cpp:161-173` obtains the current executable path but then looks up the cache solely by the following fields:

```cpp
return e->m_keyString == str && pid && pid == e->m_pid && e->m_type == permission;
```

The executable lookup is not compared with the cached requester's identity. Matching entries return allow, deny, or pending at `DynamicPermissionManager.cpp:211-219`. `askForPermission(nullptr, ...)` creates a runtime rule, stores the key, and stores the requester PID (`232-238`). The rule constructor installs a destroy listener only if its `wl_client*` is non-null (`28-34`). `removeRulesForClient` only erases matching client pointers (`364-365`); config cleanup at `71-73` removes config-source rules, not these runtime string rules. No process death observer, process start time, or TTL is stored (`src/managers/permissions/DynamicPermissionManager.hpp:71-84`).

**Full chain and preconditions — source-confirmed.** A process able to reach the compositor's command socket supplies a plugin path; on Linux `src/ipc/s1/Unix.cpp:54-63,283-300` obtains the PID using `SO_PEERCRED`. `Unix.cpp:323-324` forwards that peer identity, and `src/ipc/s1/Commands.cpp:1770-1784` passes it to `CPluginSystem::loadPlugin`. `src/plugins/PluginSystem.cpp:16-25` calls `clientPermissionModeWithString(requesterPid, path, PLUGIN)`. With enforcement enabled and an ask decision, the rule is kept independently of the command-socket peer. After that requester exits, a later requester with a recycled positive PID and the same path/type matches the old rule without a new prompt. A denied decision can similarly be inherited. An allow that loads code still encounters the normal “already loaded” check (`PluginSystem.cpp:67-70`); a repeated actual load would require the plugin to have been unloaded or the earlier load not to remain present. No PID churn or plugin was executed.

**Severity and corrections.** Low as in the dossier: requires prior cached state, PID reuse, the same exact plugin path and permission type, and command-socket access. PID is kernel-derived, not an arbitrary field supplied in the command. Keyboard checks instead pass **-1** at `src/managers/input/InputManager.cpp:1226,1233`, and default to allow absent an ask/deny configuration rule (`DynamicPermissionManager.cpp:222-224`). Their names are not normal-process PID cache keys. This record does not demonstrate a Wayland client's ability to choose a positive PID or bypass a lifetime-tracked `wl_client*` permission cache.

**Minimal fix direction.** Bind positive-PID rules to process-start identity or requester connection lifetime and remove/revalidate on death. Treat special negative identities separately; a TTL alone only bounds the stale-decision window.

## F-PERM-05

**Claim.** Permission enforcement is disabled by default, so permission checks do not prompt. **Verdict: confirmed as an informational policy/control record.**

**Evidence — source-confirmed.** `src/config/values/ConfigValues.cpp:717` registers:

```cpp
MS<Bool>("ecosystem:enforce_permissions", "whether to enable permission control.", false),
```

`src/managers/permissions/DynamicPermissionManager.cpp:81-84` and `153-156` both return `PERMISSION_RULE_ALLOW_MODE_ALLOW` immediately when the setting is zero, before consulting configured or cached rules. This also means an explicit deny rule is ineffective while enforcement is disabled.

**Caller chain and reachability — source-confirmed.** A valid screencopy frame request enters `src/protocols/Screencopy.cpp:64-65,135`, reaches the screenshare frame machinery, and checks permission in `src/managers/screenshare/ScreenshareFrame.cpp:162,367`. Input capture checks on activation, after its enabled-state prerequisite, at `src/protocols/InputCapture.cpp:212-218`. Plugin loading checks at `src/plugins/PluginSystem.cpp:16-21`; keyboard configuration checks at `src/managers/input/InputManager.cpp:1226`. All receive allow from the permission API under the default. Protocol availability, valid buffers/resources, IPC access, input-capture session state and other independent caller restrictions still apply.

**Severity and corrections.** Informational: this is the declared configuration default, not proof that every client can perform every listed operation, evade session-lock rendering protections, or execute a plugin through Wayland. The other dossiers' crash and lock-screen claims were not verified here. Keyboard prompting additionally requires an applicable ask rule even when enforcement is enabled (`DynamicPermissionManager.cpp:222-224`).

**Minimal direction.** Document the default and the requirement to enable enforcement. Any change to the default needs compatibility review and a separate wiki update; it is not justified as an unnoticed implementation fix by this record alone.

## F-TOK-01

**Claim.** Token generation and lazy expiry look sound; non-consuming lookup might permit replay, and registration has no total cap. **Verdict: partially: control checks and a boundedness concern hold; replay does not follow from lookup alone.**

**Evidence — source-confirmed.** `src/managers/TokenManager.cpp:13-24` calls `uuid_generate_random` and retries on a map collision. `TokenManager.cpp:27-31` inserts a shared token without a quota. `TokenManager.cpp:34-43` removes expired entries on lookup and returns an existing entry without consuming it:

```cpp
std::erase_if(m_tokens, [&NOW](const auto& el) { return el.second->m_expiresAt < NOW; });
// then return the entry, if present
return m_tokens.at(uuid);
```

No periodic cleanup is shown, so the map can retain expired entries until a later lookup. This review confirms use of libuuid's random-generation API, not the host's exact entropy source, entropy quality, or randomness at runtime.

**Activation chain and replay guard — source-confirmed.** A bound activation manager creates a token resource at `src/protocols/XDGActivation.cpp:77-82,114-122`. Commit is allowed once per resource (`19-39`) and requires a nonzero serial valid for the requesting client's seat. `src/managers/SeatManager.cpp:134-153` rejects absent seat resources; its normal event serials are consumed when validated (default `erase=true` at `src/managers/SeatManager.hpp:109`), while the current enter serial has a distinct successful check. An accepted commit registers a twelve-month token (`XDGActivation.cpp:42`) and adds it to `m_sentTokens`. Activation looks up **that separate list**, not `CTokenManager::getToken`, and erases its entry before attempting the window activation (`XDGActivation.cpp:83-102`). Repeating an already spent token does not pass this list check. Token-resource destruction calls manager removal (`64-66,110-111`).

**Quota distinction — source-confirmed.** `XDGActivation.cpp:50-59` caps each client's sent-token delivery list at ten by erasing the oldest list entry. That does not erase its token-manager entry or destroy its live token resource. Thus issuance can retain additional resource-backed manager entries if those resources remain alive, subject to the seat-serial admission guard. This is a Low resource-retention concern, not an unconditional “any client can allocate infinite accepted tokens” claim. No load test or resource exhaustion was attempted, and a delivery-list cap must not be reported as a global manager-memory cap.

**Other consumers — source-confirmed.** The dossier's “activation only” impression is incomplete: ruled launches register tokens (`src/config/supplementary/executor/Executor.cpp:131`), and initial-workspace tracking does so at `165-168`. The one-shot workspace mode explicitly removes a token at `src/desktop/view/window/Window.cpp:1182-1201`; persistent mode deliberately keeps it (`1202-1208`) and has later owner cleanup at `496-504`. Shared lookup is useful for these different semantics and is not intrinsically a replay flaw.

**Severity, corrections, and minimal direction.** Retain the random-generation/expiry check as an informational control result. Treat manager quotas and lifecycle cleanup as Low defense against retained resources; close the resource and token-map lifetime gap when evicting issuance entries, or introduce an appropriate quota. A separate `consumeToken` API can clarify genuinely one-shot uses but must not break intentional persistent-workspace behavior. No confirmed activation replay is reported.

## F-RULE-02

**Claim.** Execution rules use a fixed configuration secret that an unrelated client can declare in its environment. **Verdict: rejected at this upstream baseline.**

**Evidence — source-confirmed.** `src/config/supplementary/executor/Executor.cpp:128-140` generates a token for each ruled launch:

```cpp
const auto TOKEN = g_pTokenManager->registerNewToken(nullptr, std::chrono::seconds(1));
const auto PROC  = spawnRawProc(args, pInitialWorkspace, TOKEN);
```

`src/managers/TokenManager.cpp:13-24,27-31` uses `uuid_generate_random` and checks for collisions in its token map. `Executor.cpp:66-70` registers the generated token and spawned PID on the rule. `Executor.cpp:195-196` passes the token through the child environment. `src/desktop/rule/Rule.cpp:26-45,78-83` does not expose `exec_token` as a public property name; it is an internal match engine (`Rule.cpp:70-71`). The dossier's fixed-secret configuration example does not describe this path.

**Full match chain and guards — source-confirmed.** A user-configured ruled launch reaches `spawnWithRules`, token generation, child environment injection, then window mapping. `src/desktop/rule/windowRule/WindowRule.cpp:466-481` calls `getEnv()` only when `allowEnvLookup` is true; it must match the generated token if present, otherwise it may match the registered launch PID. The PID match is a fallback, not an additional requirement when a token exists. `src/desktop/rule/windowRule/WindowRuleApplicator.cpp:530-559` collects matching execution rules and unregisters them on the non-preread mapping pass. `src/desktop/rule/Rule.cpp:146-163` gives execution rules a one-minute expiry, with cleanup at `src/desktop/rule/Engine.cpp:30-35`. Expiry cleanup is opportunistic; the one-second token-manager expiry is not checked by the environment matcher and must not be advertised as a one-second matching limit.

**Reachability, severity, and corrections.** No fixed known secret exists for an arbitrary unrelated client to self-declare. Descendants can inherit a launch token; that is consistent with matching launched applications and does not establish an authorization bypass. The token is a bearer value, so disclosure would be a different precondition. No token disclosure or security-sensitive rule takeover is established here. On Linux, a `setenv()` immediately before opening a window also does not reliably replace the initial environment exposed by `/proc/PID/environ`; the dossier's proposed `main()` sequence is not sufficient evidence.

**Minimal direction.** No change is needed for the alleged missing randomization: it already exists. Keep window-rule matching distinct from authentication. If execution-rule expiry needs stronger guarantees, enforce expiry at match time as a separate narrowly scoped change.

## F-RULE-03

**Claim.** Execution-token matching repeatedly reads process environments synchronously on the mapping path. **Verdict: confirmed for the performance mechanism; no measured compositor stall is claimed.**

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:753-778` gets the backend PID, rejects PID ≤ 1, opens `/proc/PID/environ` synchronously on Linux, and expands a local buffer in 512-byte increments:

```cpp
std::ifstream ifs(environFile, std::ios::binary);
if (!ifs.good())
    return {};
buffer.resize(512, '\0');
while (ifs.read(buffer.data() + needle, 512)) {
    buffer.resize(buffer.size() + 512, '\0');
    needle += 512;
}
```

The function parses the entire buffer into a fresh map at `Window.cpp:794-810`; there is no cache. `src/desktop/rule/windowRule/WindowRule.cpp:466-470` invokes it for an execution-token matcher when `allowEnvLookup` is true.

**Call chain, guards, and preconditions — source-confirmed.** A client's first window map calls `src/desktop/view/window/Window.cpp:1342-1346` into `readStaticRules` and possibly `recheckStaticRules`. Their rule loops call `matches(window, true)` at `src/desktop/rule/windowRule/WindowRuleApplicator.cpp:530-536,564-574`. Only a matcher reached before another property rejects the window performs the read; ordinary rules without an execution-token match do not. Execution rules must already exist from compositor-authorized ruled launches (`src/config/supplementary/executor/Executor.cpp:118-140`); a Wayland client does not create arbitrary token rules just by declaring an environment variable. Rules are removed after a matching normal mapping (`WindowRuleApplicator.cpp:555-559`). An independent environment read also occurs when initial-workspace tracking is enabled (`Window.cpp:1182-1187`), whose default is 1 (`src/config/values/ConfigValues.cpp:591`). Access failure yields an empty map rather than a blocking retry.

**Severity and corrections.** Low: redundant main-thread work under a configured population of outstanding execution rules. N eligible token rules can mean N reads per mapping pass, but total rule count, process environment size, and map rate determine impact; no timings or denial-of-service threshold were executed. The dossier's universal two-MiB environment limit is not established: Linux exec limits depend on platform and stack/resource settings. The read is procfs data, not an arbitrary client-supplied file path. Normal dynamic rule updates call `matches` with its default false environment flag and skip execution rules (`WindowRuleApplicator.cpp:598-604`).

**Minimal direction.** Read the necessary environment keys once per mapping pass and share the result across rule matching and initial-workspace handling. A persistent PID cache would require process-start identity and invalidation; a per-pass cache avoids that additional lifecycle problem.

## F-PERM-06

**Claim.** Client-related names/paths are substituted unescaped into permission text containing markup, enabling rich-text phishing. **Verdict: partially: unescaped text flow is confirmed; the external renderer's exact phishing behavior is not verified.**

**Evidence — source-confirmed.** `src/managers/permissions/DynamicPermissionManager.cpp:240-261` derives `appName` and inserts it, the plugin path, or the keyboard name into localized text without local escaping. `DynamicPermissionManager.cpp:271` appends explicit `<br/>` and `<i>` markup. English translations in `src/i18n/Engine.cpp:219-225` surround the variables with `<b>...</b>`, for example:

```cpp
// DynamicPermissionManager.cpp:258
I18n::i18nEngine()->localize(I18n::TXT_KEY_PERMISSION_REQUEST_PLUGIN,
                           {{"app", appName}, {"plugin", binaryPath}});
// AsyncDialogBox.cpp:103: description is passed as the --text argument
```

`src/i18n/Engine.cpp:1822-1825` delegates variable substitution to hyprutils. `src/helpers/AsyncDialogBox.cpp:103` passes the resulting description as one vector argument to `CProcess`, not a shell command string. This is not evidence of shell injection or code execution.

**Substitution check — executed.** The isolated `dependency_checks.cpp` uses installed hyprutils **0.14.2** to localize a test variable containing ordinary comparison characters. It prints `localized=Label: A < B & C > D`, confirming that this installed localization implementation does not HTML-escape the value. This checks substitution only. No dialog was opened and no renderer or human reaction was tested; hyprutils is an installed dependency, not part of the pinned commit.

**Full text-source chains and preconditions — source-confirmed.** A valid capture request reaches the permission call at `src/managers/screenshare/ScreenshareFrame.cpp:162`; enabled enforcement and an uncached request enter `DynamicPermissionManager.cpp:86,147,232`. `binaryNameForWlClient` obtains peer credentials and canonicalizes `/proc/PID/exe` (`src/helpers/MiscFunctions.cpp:368-410`); the prompt displays the executable basename (`DynamicPermissionManager.cpp:243-244`). A client can control the name of an executable it runs, subject to filesystem naming rules and procfs lookup permissions; this is not an arbitrary protocol app-title string. Plugin request paths come from `src/ipc/s1/Commands.cpp:1770-1784` through `src/plugins/PluginSystem.cpp:21` into the same formatter. The IPC parser's space-separated tokenization constrains path contents. Both paths finish at the helper's `--text` argument.

**Keyboard correction — source-confirmed.** Virtual-keyboard creation does not accept an arbitrary name argument. `src/protocols/VirtualKeyboard.cpp:11-31,110` derives its name from the creating executable when `misc:name_vk_after_proc` is true (the default at `src/config/values/ConfigValues.cpp:571`), otherwise uses a fixed name. `src/devices/VirtualKeyboard.cpp:47` passes that into `m_deviceName`; `src/managers/input/InputManager.cpp:1123,2157-2169` normalizes/deduplicates it before permission lookup at `1226`. `src/helpers/MiscFunctions.cpp:413-420` folds spaces/newlines/commas and lowercases other characters, but does not generally escape markup delimiters. A keyboard dialog additionally requires a matching ask rule because unknown keyboard keys default to allow (`DynamicPermissionManager.cpp:177-206,222-224`).

**Severity and evidence limit.** Low for unsanitized prompt text. Source confirms markup-bearing text construction and delivery, not the supported markup subset, link handling, clipping, truncation, actual appearance, or successful user deception. The genuine `hyprland-dialog` renderer's pinned source is not supplied in the upstream checkout. Its existence in the host's PATH is not renderer validation. Settling the visual claim requires reviewing the packaged renderer version and a controlled rendering test in an isolated environment, outside the running desktop session. No automatic grant follows from this text alone.

**Minimal fix direction.** Escape dynamic values at the renderer's markup boundary, or render variable spans as literal text; bound displayed lengths while preserving sufficient identity information. Handle newlines according to display context rather than assuming HTML escaping alone removes all presentation ambiguity.

## F-PERM-07

**Claim.** Missing or misclassified `hyprland-dialog` silently disables permission control and allows all requests. **Verdict: partially: lookup weaknesses hold, but the asserted fail-open behavior is contradicted by an earlier null dereference.**

**Decisive evidence — source-confirmed.** `src/helpers/AsyncDialogBox.cpp:15-19` returns null when its lookup fails. The permission caller at `src/managers/permissions/DynamicPermissionManager.cpp:276-282` does this in order:

```cpp
rule->m_dialogBox = CAsyncDialogBox::create(...);
rule->m_dialogBox->m_priority = true;
if (!rule->m_dialogBox) {
    // log about disabling permission control
    rule->m_allowMode = PERMISSION_RULE_ALLOW_MODE_ALLOW;
    return;
}
```

The write through the null shared pointer precedes the check. Installed hyprutils pointer semantics do not rescue it: `/usr/include/hyprutils/memory/SharedPtr.hpp:141-157` makes `operator->()` return `get()`, which returns null for an empty shared pointer. Thus this is source-confirmed undefined behavior with an expected null-dereference crash, not a reachable successful permission fallback. No compositor crash was executed. Even if the null check were moved up, that branch would set only the newly created rule, not a session-wide enforcement-disable flag.

**Complete conditional chain — source-confirmed.** A Wayland client with access to the compositor can submit an otherwise valid capture request (`src/protocols/Screencopy.cpp:64-65,135`), leading to a pending frame (`src/managers/screenshare/ScreenshareSession.cpp:187-193`). Frame processing calls `copy` (`src/managers/screenshare/ScreenshareManager.cpp:27-40`) and the permission API (`src/managers/screenshare/ScreenshareFrame.cpp:162`). When enforcement is enabled, no existing decision covers the client/binary, and the helper check fails, `DynamicPermissionManager.cpp:119-149` calls `askForPermission`; creation returns null and the next line writes `m_priority`. No special malicious request argument or invalid Wayland object is needed for this source defect: the essential extra condition is the missing/unusable helper environment. With enforcement disabled the API returns allow earlier, so this null path is not reached. Existing allow/deny/pending rules also avoid prompt creation.

**Lookup correction — source-confirmed.** `src/helpers/fs/FsUtils.cpp:83-107` tests PATH candidates for existence and regular-file status, then returns the first such candidate's **others-execute bit**. A legitimate executable usable by its owner/group without others-execute may be misclassified. A non-executable regular first candidate prevents checking later directories. The path list is static once first initialized (`FsUtils.cpp:87`), while actual subprocess execution is separate; identity/lookup consistency is not enforced. Merely changing a client's PATH does not change the compositor's path list. An ordinary user cannot normally remove a distribution-owned system executable or uninstall its package without additional authority; the dossier's deletion example is not a Wayland-only capability.

**Warning correction — source-confirmed.** `src/Compositor.cpp:897-913` checks for the helper and shows a fifteen-second warning notification unless `misc:disable_hyprland_guiutils_check` is enabled. That setting defaults to false (`src/config/values/ConfigValues.cpp:597`). Therefore “only an ERR log, no visible warning” is incorrect, although the startup warning is not a safe terminal handling policy for each later failed prompt.

**Other invalid-result cases — source-confirmed.** Missing helper, failed open, and invalid stdout are distinct. `AsyncDialogBox.cpp:105-108,117-119,127-130` can return a null promise on setup/spawn failure; `DynamicPermissionManager.cpp:285-286` calls `then` without checking it, and `src/helpers/defer/Promise.hpp:104-107` accesses promise members. Empty/unrecognized stdout on a successfully opened dialog instead follows F-PERM-01's retained-PENDING path (`DynamicPermissionManager.cpp:299-318`), not this null result and not allow-all.

**Severity and minimal fix direction.** High for the corrected conditional local-client compositor-crash path; it requires enforcement enabled and a missing/misclassified helper or failed dialog setup. Reject Medium “silent session-wide allow” as the current mechanism. Check the dialog and returned promise before dereferencing, close/release failed setup resources, and settle the pending permission as DENY/error with a clear notification. Correct executable discovery using effective execution accessibility and a consistent trusted helper path. Moving the null check alone would activate the existing fail-open branch; change its decision too.
