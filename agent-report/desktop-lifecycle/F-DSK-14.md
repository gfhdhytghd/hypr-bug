# F-DSK-14 Group initialization / replacement boundary assumptions

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: All group creators supply a live singleton; replacement validates both windows and identity. Expired-slot claims require the unproven F-DSK-01 state.

**Claim.** Empty IPC arguments create empty groups, replacement overwrites expired slots, and current-member switching dereferences expired members.

**Evidence — source-confirmed.** `src/desktop/view/Group.cpp:27–56` assumes a nonempty member list, but the complete upstream call-site search finds only `src/config/shared/actions/ConfigActions.cpp:976,1818` and `src/desktop/view/window/Window.cpp:1450`. They supply `{window}`, `{PWINDOWINDIR}`, and `{m_self}` respectively. Actions reject missing windows (`ConfigActions.cpp:968–970,1804–1812`); the map path has already established the mapped window (`Window.cpp:1161–1168,1449–1450`). No IPC API accepts an arbitrary member vector.

`Group.cpp:176–185` guards replacement:

```cpp
if (!oldWindow || !newWindow || oldWindow == newWindow)
    return;
const auto ITR = std::ranges::find(m_windows, oldWindow);
if (ITR == m_windows.end() || newWindow->grouping().group() == m_self)
    return;
```

A live `oldWindow` strong pointer cannot equal a genuinely deleted member's different control-block identity. The slot index is obtained from that successful search. `setCurrent()` also first checks `!m_target->window()` (`:319`), then clamps the requested index (`:331`). Its subsequent visibility loop/lock assumes the ordinary live-membership invariant; it is not robust to arbitrary preexisting corruption.

**Trigger/guard chain — source-confirmed.** IPC/config group action → window extraction/query → nonnull singleton creation. Empty action arguments do not become an empty group vector. Swallow replacement → two locked live windows → identity search → replacement. Member switching could fail only after an additional stale-membership defect; F-DSK-01 does not establish that prerequisite here.

**Verdict and severity.** Rejected as an independent client-reachable finding. Internal API preconditions are real, but the alleged input-to-empty-vector chain and expired-slot replacement mechanism are false.

**Corrections / minimal fix direction.** Optional nonempty/live-member assertions and checked locks can harden the group API. If a stale-member producer is later proved, fix that producer and group cleanup together; do not count these dependent consequences as separate proven vulnerabilities.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
