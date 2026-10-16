# F-PERM-01 Permission decisions trust dialog output and executable lookup

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Dialog stdout controls decisions and malformed completion leaves PENDING; executable substitution additionally requires control of the compositor’s helper search path.

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

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
