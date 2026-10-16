# F-BOOT-08 Session watchdog robustness

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Parent allocations leak and initialized crashes restart without backoff. Early failures stop; the claimed multithreaded-fork deadlock and untrusted child-protocol boundary are unproved.

**Claim.** The watchdog performs unsafe nested process creation, leaks argument strings, restarts indefinitely without backoff, and trusts child lock/unlock messages.

**Evidence — source-confirmed.** `start/src/core/Instance.cpp:47–53` allocates every argument using `strdup` **before** forking. `std::vector<char*>` does not free its pointed-to allocations, and no frees appear after the fork or before return. Thus the long-lived watchdog parent leaks a small argument-sized amount per launch; this is not confined to a disposable child, as the dossier suggests. The child calls `Nix::shouldUseNixGL()` at line 63 and then `execvp` at 66–68; the Nix probe invokes `CProcess::runSync` (`start/src/helpers/Nix.cpp:84–105`) unless flags short-circuit it. PATH/`--path` behavior is covered in F-BOOT-03.

**Restart chain and stopping guards — source-confirmed.** Launcher invocation → `start/src/main.cpp:104–109` creates an instance → pipes and launch (`Instance.cpp:157–181`) → poll and child-event processing (`196–217`) → result at `Instance.cpp:219` → restart decision (`main.cpp:113–120`):

```cpp
return !m_hyprlandInitialized || m_hyprlandExiting; // Instance.cpp:219
// ... main.cpp:113–117 ...
if (!RET) {
    safeMode = true;
    continue;
}
```

`m_hyprlandInitialized` defaults false (`Instance.hpp:32`) and is set by `vax` (`Instance.cpp:129–134`). The compositor sends this marker after its ready path (`src/Compositor.cpp:830–836`). `end` sets the exiting flag (`Instance.cpp:137–140`), and cleanup sends it (`Compositor.cpp:568–572`). Therefore a pre-initialization failure or clean exit stops the launcher. Only a child that has initialized and exits without `end` selects a restart. The restarted child receives `--safe-mode` (`Instance.cpp:34–35`). The same condition must recur in safe mode for an unbounded loop. There is no restart count or delay in that loop.

**Threading and protocol corrections — source-confirmed.** The watchdog's visible worker thread is created **after** fork (`Instance.cpp:76`) and joined before `run` returns (`217`); the prior instance is destroyed before the next launch (`main.cpp:110–111`). The dossier does not establish a concurrently running watchdog thread at fork. Additional dependency-created threads were not proved. Complex child-side library work remains avoidable, but a concrete inherited-lock deadlock does not follow merely from the second process launch. No deadlock was executed.

The event pipe is created by the parent (`Instance.cpp:157–177`) and its write fd is deliberately passed to the compositor (`30–33`). `lock`/`unlock` modify state (`143–152`) and `lockedCrash` becomes `--locked` (`37–38`; `main.cpp:110`). A malicious `--path` executable is the selected trusted child, not an arbitrary external client; it already executes as the same user. No distinct unauthenticated client-to-watchdog path is established by these text messages.

**Severity.** Low for a real parent leak and lack of backoff after repeatedly initialized crashes. Triggering rapid restarts requires an already recurring post-initialization crash that survives safe mode; the dossier supplies no client reproducer for it. CPU saturation, measured log growth, and multithreaded deadlock were not demonstrated. These facts do not justify a new cross-user execution finding.

**Minimal fix direction.** Own argument storage with strings and point argv into stable buffers, precompute launch decisions before fork, and add bounded/backoff restart policy without weakening locked-session recovery. Treat child protocol framing as reliability work rather than assuming a separate authentication boundary. No watchdog or compositor binary was run.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
