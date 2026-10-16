# F-BOOT-02 Crash signal handler invokes PATH-resolved shell commands

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Crash reporting invokes shell helpers and interpolates the executable path; environment/path control is required. Independent client-to-code-execution escalation is unproved.

**Claim.** Crash reporting invokes shell commands through inherited PATH, and inserts the compositor executable pathname into another shell command without quoting; signal-context allocation can also compromise report generation.

**Evidence — source-confirmed.** `src/debug/crash/SignalSafe.hpp:144–157` forks and invokes the fixed shell binary with a command string:

```cpp
char const* const argv[] = {"/bin/sh", "-c", cmd, nullptr};
execv("/bin/sh", cc<char* const*>(argv));
```

`src/debug/crash/CrashReporter.cpp:167,171` supplies the GPU and OS pipelines, containing bare helper names. On Linux `CrashReporter.cpp:209` canonicalizes `/proc/self/exe`. Lines 226–232 interpolate its result and run it through `execAndGet`:

```cpp
const auto CMD = std::format("addr2line -e {} -Cf {}", FPATH.c_str(), addrs);
const auto ADDR2LINE = execAndGet(CMD.c_str());
```

The Clang branch uses `llvm-addr2line` with the same pathname handling. `src/helpers/MiscFunctions.cpp:158–164` constructs `CProcess("/bin/sh", {"-c", cmd})`. Canonicalization resolves filesystem names; it does not make their characters safe shell syntax. Ordinary spaces can already break symbolization, independently of malicious input.

**Trigger and preconditions — source-confirmed.** Inherited PATH or the installation/launch pathname → default signal registration (`src/Compositor.cpp:295–298`) → a subsequent fatal signal → `handleUnrecoverableSignal` (`105–121`) → report creation/open (`CrashReporter.cpp:62–98`) → shell pipelines → backtrace/path formatting → shell symbolizer invocation. The latter also requires the reporter to survive earlier crash-context operations. PATH helper replacement requires an executable in a searched location; pathname interpretation requires a suitably named executable installation. These are launch/session trust prerequisites, not data delivered in an ordinary Wayland request. The session is normally unprivileged (`src/main.cpp:207–214` rejects superuser launch without an explicit override).

**Robustness — source-confirmed, failure not executed.** The reporter explicitly acknowledges allocations and possible deadlock at `CrashReporter.cpp:177–181`, and already allocates a plugin vector at lines 125–130. `Compositor.cpp:108–117` resets the fatal handlers and arms a 15-second alarm that aborts. This limits many hangs but cannot guarantee a complete report or safe recovery. Signal registration uses `signal`, with no alternate-stack setup found in `src/` or `start/`; stack exhaustion remains a report-reliability concern. Merely saying that every `fork` or `execv` call is itself inherently unsafe would be inaccurate; allocation, library state, and the post-fork operations require separate analysis.

**Severity.** Medium: real shell-interpretation and diagnostic-reliability defects with strong setup prerequisites. A controlled PATH is already widely honored during session startup (F-BOOT-03), so the dossier does not establish a new cross-user execution boundary merely by delaying helper execution until a crash. Arbitrary executable-path characters are mishandled, but no attack payload, forced compositor crash, or session compromise was executed.

**Corrections.** `/bin/sh` itself is absolute; its helper commands are searched. The initial fatal-signal trigger is assumed, not demonstrated by this dossier. Another user's ability to write a session unit or profile is not an ambient permission. Plugin loading already grants native execution and cannot be counted as an independent exploit prerequisite with lesser privilege.

**Minimal fix direction.** Collect system metadata during normal operation. Invoke any remaining symbolizer with a fixed executable and an argument vector, passing the pathname as one argument. Keep the fatal handler minimal and move complex report work to a separate process or normal-context mechanism; consider an alternate signal stack. No executable payload or desktop interaction was used.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
