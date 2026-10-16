# F-DSK-10 Duplicate layout targets / stale layout state

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Assignment guards and remove-before-move prevent the proposed duplicates; Space removal erases all matches and monocle skips dead targets.

**Claim.** Repeated moves/fullscreen/group replacement duplicate layout targets, leaving ghosts that are dereferenced or keep windows input-blocked after removal.

**Evidence — source-confirmed.** `src/layout/space/Space.cpp:39–58` and `src/layout/algorithm/tiled/monocle/MonocleAlgorithm.cpp:70–96` lack local insertion deduplication, and monocle removes its first matching record. The public assignment path provides the missing invariant, however (`src/layout/target/Target.cpp:20–41`):

```cpp
if (m_space == space && !m_ghostSpace)
    return;
if (m_space && !m_ghostSpace)
    m_space->remove(m_self.lock());
m_space = space;
if (space && HAD_SPACE)
    space->move(m_self.lock(), focalPoint);
else if (space)
    space->add(m_self.lock());
```

Only that function calls `CSpace::add()`/`move()` in the reviewed upstream source. `CLayoutManager::newTarget/removeTarget` route through it (`src/layout/LayoutManager.cpp:20–29`). Floating transitions likewise remove before reinsertion (`src/layout/algorithm/Algorithm.cpp:84–91`). Ghost assignments intentionally do not register the target (`Target.cpp:44–50`); group replacement removes the new member's old target before setting its ghost (`src/desktop/view/Group.cpp:205–217`).

**Trigger/guard chain — source-confirmed.** Move/fullscreen/group operations → layout manager/target assignment → same-space no-op or old-space removal → one insertion. Ordinary geometry movement (`LayoutManager.cpp:106–110`) calls the existing algorithm's move operation, not `CSpace::move()` registration. Repeated movement is therefore not a duplicate-insertion primitive.

The dossier also misstates removal: `Space.cpp:62` uses `std::erase_if`, deleting **all** matching and dead weak entries. Monocle recalculation checks both target and window (`MonocleAlgorithm.cpp:144–149`), so an already freed window is not blindly written through. Removal resets the monocle input block (`:88–92`), and its destructor resets surviving windows (`:45–55`).

**Verdict and severity.** Rejected for the proposed reachable duplicate/ghost defect. Lack of duplicate checks in an internal insertion function alone is insufficient.

**Corrections / minimal fix direction.** No functional fix established. Optional assertions on insertion uniqueness would document the invariant; preserve intentional ghost-space semantics rather than adding indiscriminate filtering.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
