# F-BOOT-09 Core-dump visibility depends on system policy

- Agent confirmation: `none`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: inconclusive — Abort can request a core, but production limits, collection policy, contents, and other-user readability are unverified. Directory permissions alone do not establish disclosure.

**Claim.** Because Hyprland does not suppress core dumps, aborting can disclose session memory to unrelated local users under permissive system coredump policy.

**Evidence — source-confirmed.** `src/Compositor.cpp:105–121` resets the SIGABRT/SIGSEGV handlers, runs reporting, and calls `abort`; the alarm fallback also aborts (`112–117`). `src/debug/crash/CrashReporter.cpp:45–50` aborts on a reporter error. Searches of `src/`, `start/`, and `systemd/` found no `RLIMIT_CORE`, `PR_SET_DUMPABLE`, `LimitCORE`, or explicit coredump-policy implementation. The watchdog's `prctl` at `start/src/core/Instance.cpp:57` is `PR_SET_PDEATHSIG`, not dumpability control. Existing resource-limit code manages `RLIMIT_NOFILE` (`src/Compositor.cpp:145–173`). The shipped `systemd/hyprland-session.target:1–6` is a target, not a service setting dump policy.

**Potential input/trigger chain — source-confirmed until the system boundary.** An actual fatal signal → reporter/default abort path → kernel and any configured external core collector → possibly stored process memory. Hyprland constructs/exports its instance identifier at `Compositor.cpp:211–213`; the executor handles workspace and exec-rule tokens at `src/config/supplementary/executor/Executor.cpp:131–132,165–168,194–196`. This supports the possibility of session state in memory. It does not prove any particular credential was present in a particular core. Exec-rule tokens are registered with a one-second lifetime at line 131, and neither an instance identifier nor a Wayland display name by itself substitutes for filesystem access authorization.

**Unclosed boundary.** Whether a dump is collected, what is included/retained, and who can read it depend on the launcher, kernel, collector, file mode/ACL, and service policy. Merely placing a dump in a publicly readable directory does not make a private file readable. An unrelated UID's access to the dump is not established by any upstream source path. Same-UID or administrative debugging access must not be presented as a cross-user confidentiality break. No production limits, kernel core settings, storage permissions, or process memory were inspected in this review.

**Verdict and severity.** Inconclusive for the alleged disclosure; informational as an upstream record. The absence of a program-level restriction and the abort path are verified. Treating normal crash diagnostics as a confirmed Low vulnerability would require additional policy/access evidence. No current system default is assumed.

**What would settle it.** In an authorized disposable environment, document effective launcher/kernel/collector policy, resulting file/ACL access for a different unprivileged UID, and the actual retained sensitive data. This can be assessed separately from the running desktop. A private collector or absent dump closes the proposed disclosure path for that deployment.

**Minimal fix direction and corrections.** Document dump privacy expectations and offer an explicit privacy/debugging policy if needed. A hard zero `RLIMIT_CORE` is not interchangeable with clearing dumpability, especially with an external pipe collector; lowering the core-size limit does not itself disable ptrace, whereas clearing dumpability has broader debugging effects. Blanket suppression is a product/diagnostics tradeoff, not a demonstrated mandatory security fix from this dossier alone.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
