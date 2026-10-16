# F-BOOT-01 Crash-report path creation lacks exclusive / symlink protections

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unsafe report creation is real; exploitation requires an attacker-writable report namespace and a crash. Default private cache directories block other users.

**Claim.** Predictable crash-report creation follows pre-existing links, accepts existing directories without checking ownership, and overwrites existing files without truncating them.

**Evidence — source-confirmed.** `src/debug/crash/CrashReporter.cpp:62–78` selects the nonempty `XDG_CACHE_HOME`, otherwise `HOME/.cache`, appends `/hyprland`, and accepts `mkdir(..., S_IRWXU)` returning `EEXIST`. Lines 80–82 derive the basename from the PID. The decisive operation at line 96 is:

```cpp
reportFd = open(reportPath.getStr(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
```

There is no exclusive-create, no final-component symlink protection, no truncation, and no validation of an already existing file's owner or mode. Buffered report writing starts at `CrashReporter.cpp:100–115`; the destructor flushes at `src/debug/crash/SignalSafe.hpp:73–75,183–192`. Existing file contents are overwritten from offset zero; any longer suffix remains. The `0600` argument applies to newly created files only.

**Trigger and reachability — source-confirmed.** Startup environment/cache-directory state → `src/Compositor.cpp:295–298` installs the crash handler unless disabled → an actual SIGSEGV/SIGABRT reaching that handler → `Compositor.cpp:105–121` calls `createAndSaveCrash` → unchecked directory/name selection → the open and writes above. A pre-existing link or file must be installable in the chosen report namespace, its target must be writable by the compositor UID, and report creation must reach the open. No Wayland request in this dossier is proved to cause the initial crash. Another UID ordinarily cannot inject environment into an already running process or send these signals to it.

**Severity.** Medium under the dossier's explicitly compromised/shared cache-directory prerequisite. This is conditional file corruption and possible report disclosure; there is no demonstrated default cross-user path or arbitrary-content write. With a correctly private cache hierarchy, another UID cannot prepare the report basename. Newly created `0600` files do not become readable merely because another user owns a parent directory; disclosure additionally requires a writable pre-existing destination with permissions permitting that other user's reads. Kernel link protections, directory permissions, and target permissions can independently prevent a proposed redirection.

**Corrections.** Report text begins with a fixed header (`CrashReporter.cpp:102`), rather than attacker-selected interpreter syntax. Plugin metadata (`125–140`) comes from already loaded native code and supplies no new code-execution privilege. Neither turning a report into a valid startup script nor session takeover is established. Lack of `O_TRUNC` preserves a suffix; it does not append. PID predictability alone grants no directory access; redirection still requires preparing the relevant filename before the reporter opens it.

**Minimal fix direction.** Use a previously validated private directory and descriptor-relative exclusive creation with symlink rejection, retrying collisions with fresh names; fail closed on invalid directories and path overflow. Keep directory validation and allocation outside the signal handler where possible. Adding `O_TRUNC` alone worsens destruction of an existing target and does not repair the trust issue. No live crash or redirection test was performed.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
