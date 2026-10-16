# F-DSK-02 Group destruction can loop on expired members

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Non-progress loop exists for an already expired first member with multiple entries; a reachable stale-member producer remains unproved.

**Claim.** Group destruction spins forever when removing an expired first member, reachable after F-DSK-01.

**Evidence — source-confirmed.** `src/desktop/view/Group.cpp:71–81` loops as follows:

```cpp
while (!m_windows.empty()) {
    if (m_windows.size() == 1) {
        remove(m_windows.at(0).lock());
        break;
    }
    remove(m_windows.at(0).lock());
}
```

`remove()` searches for equality with its `PHLWINDOW` argument and returns without erasing on a miss (`:234–244`); erasure occurs only after successful member cleanup (`:251–265`). Installed `/usr/include/hyprutils/memory/WeakPtr.hpp:175–177,193–199` confirms that an expired member locks to an empty `SP` but keeps its nonempty identity, so it does not compare equal to that empty `SP`.

**Dependency evidence — executed.** The scratch dependency-contract check confirmed that expired and empty weak keys are different identities. No infinite-loop or compositor reproducer was run.

**Trigger/guard chain — source-confirmed.** Group action → `Config::Actions::toggleGroup()` (`src/config/shared/actions/ConfigActions.cpp:967–978`) → `group()->destroy()` → removal loop. A first member already deleted, plus **at least two stored members**, makes the loop fail to progress. With exactly one stored expired member, the explicit `break` prevents an infinite loop, although cleanup still fails. The dossier omitted that distinction.

**Reachability.** Producing the stale first member is the same unproved prerequisite as F-DSK-01. There is no independent client input here that inserts expired members or turns a live member into one. Normal unmap removes membership before deletion. Consequently the source proves the behavior on invalid internal state, not an externally reachable hang.

**Verdict and severity.** Partially: the conditional non-progress bug is real; the asserted client-triggered hang is unverified. Informational hardening priority on current evidence; High only if a reachable stale-member producer is demonstrated.

**Corrections / minimal fix direction.** Prune expired members using their stored weak identities before calling live-window cleanup, and guarantee loop progress. Adding a null guard to `remove()` alone would merely preserve the non-progress problem. Treat this as dependent on F-DSK-01 rather than a separately demonstrated attack.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
