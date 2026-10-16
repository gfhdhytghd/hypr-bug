# F-PM-01 Unquoted revision handling in shell commands

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Positional revision reaches an unquoted shell command after confirmation; no --rev option exists. User-context execution, not unconditional privileged execution.

**Claim.** The revision supplied when adding a repository is interpolated into a shell command without quoting or a revision-specific validator.

**Evidence — source-confirmed.** `hyprpm/src/main.cpp:107–109,125` reads `command[2]` and passes it to `addNewPluginRepo(command[1], rev)`. `hyprpm/src/core/PluginManager.cpp:139–148` checks only:

```cpp
return !s.contains("'") && !s.ends_with("\\") && !s.starts_with("\\");
```

The add path at `PluginManager.cpp:224–230` formats a Git reset command with a bare final `{}` revision. `PluginManager.cpp:36–44` executes formatted commands using `CProcess("/bin/sh", {"-c", cmd})`. Shell syntax is therefore not excluded by the validator. This is a source-confirmed data-to-shell boundary defect; no injected command was executed.

**Call chain and gates — source-confirmed.** Local CLI positional revision → main's ABI checks (`main.cpp:111–118`) and privilege-cache request (`120–123`) → `addNewPluginRepo` argument/dependency/duplicate checks (`PluginManager.cpp:146–161`) → nonempty recorded header ABI (`173–176`) → explicit repository-trust confirmation (`178–189`) → successful clone with a `.git` entry (`217–222`) → revision reset (`224–230`) → shell. No manifest validation precedes that reset. An attacker must influence the actual revision argument used by a user or script; a remote manifest alone cannot set it.

**Severity and corrections.** Medium: user-context command execution requires a user-initiated install of a deliberately supplied argument and the preceding gates. The dossier's Critical rating and `--rev` example are incorrect: unknown flags are rejected at `main.cpp:81–83`, while revision is positional (`main.cpp:16–17`). Privileged helper calls occur elsewhere; this reset itself is not run as root. The update path single-quotes the stored revision (`PluginManager.cpp:732–741`); manifest commit pins are separately hex-checked and quoted (`275–293`).

**Minimal fix.** Use an argument-vector Git invocation; validate the supported revision syntax and distinguish revisions from options. If only full object IDs are intended, require an exact nonempty supported hash length. Do not treat the existing permissive `validArg` as shell escaping.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
