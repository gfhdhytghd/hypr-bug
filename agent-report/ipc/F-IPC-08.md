# F-IPC-08 Permissive IPC request parsing

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unknown flags and command-prefix aliases are accepted; slashes after a space preserve arguments, and no authorization bypass is demonstrated.

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

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
