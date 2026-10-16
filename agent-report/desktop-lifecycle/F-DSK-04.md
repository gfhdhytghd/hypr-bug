# F-DSK-04 Swallow restore / toggle uses invalid workspace state

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unmap clears swallow relations before workspace reset; destruction fallback locks endpoints and guards workspace access, defeating the claimed stale-state path.

**Claim.** Swallower unmap leaves an active relation after clearing its workspace; later restore/toggle dereferences null or writes to a freed space during destruction.

**Evidence — source-confirmed.** Some standalone helpers do assume a nonnull swallower workspace (`src/desktop/view/window/WindowSwallowController.cpp:188–189,204–208`). Their lifecycle ordering is omitted in the dossier. `src/desktop/view/window/Window.cpp:1624` calls `m_swallowing->onUnmap()` **before** marking the window unmapped (`:1676`) and **before** `onUnmap()` (`:1747`) clears `m_workspace` (`:543`). Controller unmap saves relations, restores the relevant endpoint and calls `clear()` on both (`WindowSwallowController.cpp:358–379`):

```cpp
if (OUTGOING && OUTGOING != INCOMING) {
    if (OUTGOING->state != SRelation::STATE_PENDING)
        /* restoreAfterSwallower(...) */;
    clear(OUTGOING);
}
```

`clear()` removes both endpoints' relation references when lockable (`:342–355`). A subsequent toggle finds no relation (`:251–254`). The early return is `Window.cpp:1608–1611`, not line 1624; it does **not** execute the later workspace reset, so it does not itself create the alleged “workspace reset, relation retained” state.

The final restoration path explicitly guards workspace access (`WindowSwallowController.cpp:309–313`):

```cpp
const auto WORKSPACE = SWALLOWER && SWALLOWER->m_workspace ?
    SWALLOWER->m_workspace : SWALLOWEE->m_workspace;
if (WORKSPACE)
    g_layoutManager->newTarget(SWALLOWEE->windowTarget(), WORKSPACE->space());
```

`restoreStandalone()` first locks and checks both endpoints (`:164–168`); a swallower whose destructor is running cannot be locked under the installed hyprutils semantics. `restoreAfterSwallower()` checks the surviving swallowee's mapped state (`:283–286`). `CWindow::~CWindow()` calls the controller while its members still exist (`Window.cpp:165–175`), before automatic member destruction; this is not inherently a half-destroyed controller access. A target owns its space with an `SP`, not a weak/raw pointer (`src/layout/target/Target.hpp:79`), and `CSpace::workspace()` locks its parent (`src/layout/space/Space.cpp:113–115`).

**Trigger/guard chain — source-confirmed.** Client unmap → backend unmap → swallow restoration/clear while workspace remains live → normal group/target removal → mapped false → workspace reset. Later toggle cannot follow the cleared relation. Destroy invokes cleanup again; failed endpoint locks and the guarded surviving-workspace fallback prevent the alleged null dereference at lines 309–313. The proposed workspace-destruction UAF has no demonstrated ownership release/reentrancy chain.

**Verdict and severity.** Rejected for the described unmap/destroy path. This does not assert that helper methods accept arbitrary corrupted relations; it establishes the guard breaking the supplied trigger.

**Corrections / minimal fix direction.** The dossier reads unmap ordering backwards and omits the explicit fallback guard. Additional workspace checks in standalone helpers are reasonable hardening; moving all restoration into deferred work is not justified by this evidence and could change focus/layout behavior.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
