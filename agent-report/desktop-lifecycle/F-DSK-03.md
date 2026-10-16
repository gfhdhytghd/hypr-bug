# F-DSK-03 Swallow matching can cross workspace / process trust boundaries

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Cross-workspace ancestry matching is real and opt-in; legitimate descendant matching does not establish a process trust-boundary bypass.

**Claim.** Ancestry-based swallowing can hide a terminal from another workspace and redirect input, allegedly crossing a process trust boundary.

**Evidence — source-confirmed.** `src/desktop/view/window/WindowSwallowController.cpp:47–94` walks up to 25 current parent PIDs, searches all mapped/input-accepting windows, filters by configured application-ID and exception-title regexes, and selects a candidate. There is no workspace or monitor equality test:

```cpp
currentPid = getPPIDof(currentPid);
// across windowState()->windows():
if (w->backend().pid() == currentPid)
    candidates.push_back(w);
```

The feature requires both enabled swallowing and a nonempty configured regex (`:50–53`). `src/config/values/ConfigValues.cpp:577` defaults swallowing to false. The Wayland backend gets its PID through `wl_client_get_credentials`, not a window-provided title/app-ID (`src/desktop/view/window/WaylandBackend.cpp:111–116`).

`reserveCandidate()` checks incoming relations and group policy (`WindowSwallowController.cpp:96–114`). Standalone activation removes the swallowee target and hides it (`:117–130`); grouped activation validates membership and replaces the slot (`:134–148`). Standalone toggle can move the revealed window to the swallower's workspace (`:200–209`).

**Input/behavior chain — source-confirmed.** A process with actual matching ancestry maps a window → `mapWindow()` reserves/activates swallowing (`src/desktop/view/window/Window.cpp:1428,1434`) → candidate filtered by user policy → terminal hidden or group member replaced. For a standalone relation the candidate selection can cross workspaces; for a group relation `replaceMember()` aligns the new member with the group's workspace (`src/desktop/view/Group.cpp:219–221`). Thus the dossier overgeneralizes the final placement of grouped windows.

**Reachability and boundary.** This requires an explicitly enabled feature, a matching regex, a mapped candidate, and ancestry visible at mapping time. A process already launched from a terminal is exactly the descendant relationship the feature implements. `src/helpers/MiscFunctions.cpp:167–188` obtains current ancestry from the OS (on Linux, `/proc/<pid>/status`). The dossier does not demonstrate an unrelated Wayland process forging that authenticated PID relationship or bypassing a security permission. Reparenting does not retain a historical terminal in the current PPID chain. Visual credential collection/input capture was not tested and is not established by source alone; clients can already present deceptive windows within their ordinary session capabilities.

**Verdict and severity.** Partially: cross-workspace matching/hiding and ancestry breadth are real; a new process-security boundary bypass and demonstrated credential theft are not. Low, opt-in UI/policy concern rather than the claimed generic Medium vulnerability.

**Corrections / minimal fix direction.** Consider an explicit same-workspace/monitor policy and visible swallow-state indication. Restricting to direct children changes ordinary wrapper/shell workflows and should be a deliberate compatibility decision, not an assumed security requirement.

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
