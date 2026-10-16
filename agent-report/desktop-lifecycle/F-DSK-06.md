# F-DSK-06 Expired focus-history references remain reachable

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Workspace GC retains an extra first-monitor record; window GC removes deleted references, and consumers safely use saved identity or checked locks.

**Claim.** Window-history weak references survive garbage collection and crash lookup; workspace-history `drop(1)` retains dead workspaces that back-and-forth blindly dereferences.

**Evidence — source-confirmed.** `src/desktop/history/WindowHistoryTracker.cpp:33–48` calls `gc()` immediately before the weak-pointer loop:

```cpp
std::erase_if(m_history, [](const auto& e) { return !e; });
// historyForWorkspace, after gc:
if (w->m_workspace != ws)
    continue;
```

A deleted object's weak pointer is false and is removed. During destruction the pointer can be true, but `get()` still returns the object; this is documented and implemented in installed `/usr/include/hyprutils/memory/WeakPtr.hpp:154–190,221–226`. No interleaving that destroys a window between this GC and workspace comparison was found; the loop does not itself emit events. The unmap consumer quoted by the dossier also explicitly checks `!w || !w->m_isMapped` (`src/desktop/view/window/Window.cpp:1644–1649`). A generic claim of “destroying flag causes a null pointer here” is false.

The workspace GC really skips its first collected monitor (`src/desktop/history/WorkspaceHistoryTracker.cpp:54–66`):

```cpp
for (auto& mon : monitorCounts | std::views::drop(1)) {
    if (mon == entry.monitor)
        return !entry.workspace;
}
monitorCounts.emplace_back(entry.monitor);
```

But the dossier's “first monitor never gets checked” is also false: its second occurrence is appended to `monitorCounts`, so a third occurrence can match that second element. It can retain two expired records for the first monitor encountered, rather than retaining all such records indefinitely. Other monitors can retain their first record. The entry deliberately stores a separate `State::Workspace::STarget` value (`src/desktop/history/WorkspaceHistoryTracker.hpp:21–25`), populated at tracking (`WorkspaceHistoryTracker.cpp:47–51`).

**Trigger/guard chain — source-confirmed.** Workspace activation → identity/history recording → workspace later deleted → previous-workspace query may return an expired weak handle plus valid saved identity. Back-and-forth uses `findOrCreateWorkspace(PREVIOUS.target)` with a null result check (`src/config/shared/actions/ConfigActions.cpp:185–194`); resolver paths return `.target` (`src/state/workspace/Resolver.cpp:179–190`). Lua uses `workspace.lock()` and a guarded identity lookup (`src/config/lua/bindings/LuaBindingsQuery.cpp:386–394`). None of the found consumers blindly dereferences the expired workspace described by the dossier. Retaining a record for recreation is therefore not by itself a dangling-access defect.

**Dependency evidence — executed.** The assigned scratch contract check confirmed valid-during-destruction versus false-after-deletion semantics. No compositor history stress test was run.

**Verdict and severity.** Partially: the extra first-monitor retention/GC asymmetry is source-confirmed; the claimed window/workspace null-dereference path is rejected. Informational bookkeeping concern, with no confirmed client-triggered crash or unbounded history growth from this mechanism.

**Corrections / minimal fix direction.** Specify how many saved identities each monitor should retain, then implement that policy explicitly without `drop(1)`. Use locked local window handles for clearer ownership if refactoring history access, but do not erase every expired workspace identity indiscriminately because recreation is a supported caller behavior.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
