# F-INP-10 Workspace-creation command rules accept client-provided names

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Empty-workspace commands exist, but ext-workspace advertises no creation capability and implements no create-workspace request handler.

**Verdict: rejected. Revised severity: None.**

The dossier claims an ext-workspace client can choose a new workspace name and thereby trigger a configured empty-workspace command. The command hook exists, but the alleged client creation path is absent at this baseline.

**Evidence and blocking guard — source-confirmed.** `src/workspace/HLWorkspace.cpp:83–96` selects a workspace rule and executes its configured command when `m_wasCreatedEmpty` is true:

```cpp
if (self->m_wasCreatedEmpty)
    if (auto cmd = RULEFORTHIS.m_onCreatedEmptyRunCmd)
        Config::Supplementary::executor()->spawnWithRules(*cmd, self);
```

However, `src/protocols/ExtWorkspace.cpp:29` sends group capabilities **0**. The complete group constructor (`:12–51`) registers destruction callbacks and output notifications, but no create-workspace callback. Workspace handle callbacks (`:129–140`) only destroy, activate, deactivate, and assign existing workspaces. The manager's commit handler (`:257–261`) commits those existing handles; `CExtWorkspaceResource::commit` (`:229–243`) changes activation/placement, not names or creation. A source search of `ExtWorkspace.cpp/.hpp` finds no create-workspace implementation.

**Reachability and corrections — source-confirmed.** A configured empty-workspace command is an intended configuration feature. The dossier jumps from a protocol-defined request to assumed compositor support. Merely defining a request in protocol XML does not connect it to `CHLWorkspace::init`. There is no upstream source chain from the claimed client-supplied name to this execution hook. A client also does not own a separate private workspace namespace as asserted.

**Fix direction.** No fix is justified for this claimed path. If client-driven creation is added later, review capability advertisement, creation authorization, rule matching, and execution-hook policy together. This conclusion does not audit unrelated workspace-creation interfaces.

## Offline checks and audit limits

**Executed:** a standalone C++23 program, [dependency_checks.cpp](/home/wilf/data/.claude-scratch/agent-reports/work/13-input-keybinds/dependency_checks.cpp), linked to the installed Hyprutils **0.14.2**. All compiler outputs and the binary were placed in the required scratch directory; `TMPDIR` was set there for compilation. The source does not create a Wayland connection or use compositor code. The [output log](/home/wilf/data/.claude-scratch/agent-reports/work/13-input-keybinds/dependency_checks.log) records four passing checks:

1. A strong local obtained from a weak pointer pins the owner; destructor-time self-locking returns empty.
2. Destroying a scoped signal-listener handle prevents subsequent delivery on its still-live emitter.
3. Static listener captures are retained by the signal and released when that signal dies.
4. Numeric syntax acceptance does not imply `std::stoi` range acceptance; the probe catches the resulting exception locally.

**Source-confirmed:** the supplied upstream worktree HEAD is `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb`; its tracked/untracked status was clean when checked. Installed Aquamarine interface version is **0.15.1**. Header/library checks describe those installed dependencies, not a historical runtime binary loaded by the user's compositor. The installed ext-workspace specification also states that unsupported creation requests are ignored (`/usr/share/wayland-protocols/staging/ext-workspace/ext-workspace-v1.xml:143–157,212–220`), consistent with F-INP-10's missing handler and zero capability.

No compositor, ASan compositor reproduction, physical hotplug, lockscreen, permission dialog, virtual-input client, or exploit harness was run. No network, GitHub tool, session socket/IPC, live desktop input, full build, source modification, or Git-state mutation was used. Only this report and files under the assigned scratch directory were written. Cross-referenced F-PR2-01/F-PR2-02 belong to the other dossier and are not independently re-evaluated or counted here.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
