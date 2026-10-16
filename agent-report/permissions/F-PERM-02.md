# F-PERM-02 Spawned applications inherit non-CLOEXEC file descriptors

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Dialog pipe inheritance is real and spawn lacks descriptor cleanup; Xwayland sockets use CLOEXEC and its notify writer is scoped to startup.

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

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
