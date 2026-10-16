# Desktop lifecycle and layout verification

- Baseline: official Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).
- Dossier: [10-desktop-lifecycle](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md).
- Date: 2026-10-15.
- Method: read-only source review of the pinned upstream tree and installed hyprutils 0.14.2 headers, plus one isolated dependency-contract check. No compositor execution, desktop IPC, full builds, network access, or git changes. Evidence labels distinguish source-confirmed facts from executed isolated checks. Hypothetical invalid internal state does not establish a reachable client-triggered defect.
- Executed artifact: `/home/wilf/data/.claude-scratch/agent-reports/work/10-desktop-lifecycle/dependency_contracts.cpp` and its adjacent executable. It checks pointer identity/destruction semantics and listener ownership/order only; it is not a vulnerability reproducer.

## Summary

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-DSK-01 | inconclusive | Unrated; High if reachable | `Window.cpp:1608–1669,1809–1825`; `XDGShell.cpp:520–543` | Destroy lacks fallback group cleanup, but ordinary role/X11 teardown unmaps first; no client-reachable bypass established. |
| F-DSK-02 | partially | Informational; conditional High | `Group.cpp:71–81,234–265` | Non-progress loop exists for an already expired first member with multiple entries; a reachable stale-member producer remains unproved. |
| F-DSK-03 | partially | Low / opt-in policy | `WindowSwallowController.cpp:47–94,117–148`; `ConfigValues.cpp:577` | Cross-workspace ancestry matching is real and opt-in; legitimate descendant matching does not establish a process trust-boundary bypass. |
| F-DSK-04 | rejected | None | `Window.cpp:1624,1676,1747,543`; `WindowSwallowController.cpp:281–386` | Unmap clears swallow relations before workspace reset; destruction fallback locks endpoints and guards workspace access, defeating the claimed stale-state path. |
| F-DSK-05 | rejected | None | `Window.hpp:298–317`; `WLSurface.cpp:31–32` | Listeners are RAII handles; backend ownership and automatic member destruction prevent the claimed post-window callback. |
| F-DSK-06 | partially | Informational | `WindowHistoryTracker.cpp:33–48`; `WorkspaceHistoryTracker.cpp:54–75` | Workspace GC retains an extra first-monitor record; window GC removes deleted references, and consumers safely use saved identity or checked locks. |
| F-DSK-07 | partially | Low / configuration-dependent | `MasterAlgorithm.cpp:221–261,965–993,1077–1186` | Empty-cycle and single-window claims fail guards; zero work-area smart resizing can produce NaN, without demonstrated crash or allocation exhaustion. |
| F-DSK-08 | partially | Informational / latent bounds risk | `ScrollingAlgorithm.cpp:140–213,259–298,912–915,1917–1945` | Empty next() is unsafe in isolation; guarded insertion callers and membership-based lookup leave the proposed client-triggered bounds violation unproved. |
| F-DSK-09 | rejected | None | `ScrollingFullscreenHandler.cpp:453–470,537–565` | Expired keys are erased by iterator; weak-pointer identity hashing prevents null from matching arbitrary expired entries. |
| F-DSK-10 | rejected | None | `Target.cpp:20–41`; `Space.cpp:61–67`; `MonocleAlgorithm.cpp:142–149` | Assignment guards and remove-before-move prevent the proposed duplicates; Space removal erases all matches and monocle skips dead targets. |
| F-DSK-11 | confirmed | Low / layout robustness | `Space.cpp:77–106`; `ReservedArea.cpp:53–66`; `MasterAlgorithm.cpp:1077–1177` | Configured reservations/gaps can produce zero or negative work areas; geometry propagation is confirmed, while compositor crash and output-protocol abuse remain unverified. |
| F-DSK-12 | inconclusive | Unrated; High if crash reachable | `FocusState.cpp:129–183,272–278`; `Monitor.cpp:447–472` | Pinned focus lacks a monitor guard; startup mapping is guarded and client self-pinning is unsupported. Monitor-removal event ordering still needs validation. |
| F-DSK-13 | rejected | None | `Window.cpp:562–574`; `EventLoopManager.cpp:215–235` | Weak validity differs during destruction, but idle execution cannot overlap synchronous window destruction on the compositor event loop. |
| F-DSK-14 | rejected | None | `Group.cpp:27–56,176–185,318–338`; `ConfigActions.cpp:967–978` | All group creators supply a live singleton; replacement validates both windows and identity. Expired-slot claims require the unproven F-DSK-01 state. |

Verdict totals: **1 confirmed, 5 partially, 6 rejected, 2 inconclusive**. The confirmed item is Low geometry robustness; no client-triggered crash, hang, UAF, or credential theft was reproduced.

## F-DSK-01

**Claim.** Destruction bypasses group removal, leaving expired weak members that later group visibility/workspace/focus operations dereference.

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:1809–1825` calls swallow cleanup, unassigns the surface, clears backend listeners, removes the window target and removes the window from state, without explicitly removing group membership. `unmapWindow()` does remove it (`:1668–1669`), but returns early if the surface does not exist or the window is already unmapped (`:1608–1611`). `src/desktop/view/Group.cpp:414–426,461–470` and `Window.cpp:1064–1075` directly access stored weak members. Thus **if** a previously grouped window really reaches deletion without unmap cleanup, later accesses are unsafe.

The omitted role-level guard matters. `src/protocols/XDGShell.cpp:520–543` emits XDG unmap before destroy for a mapped XDG surface, including underlying `wl_surface` destruction:

```cpp
if (m_mapped)
    m_events.unmap.emit();
m_mapped = false;
m_surface.reset();
m_events.destroy.emit();
```

`src/desktop/view/window/WaylandBackend.cpp:131–135,151–160` forwards those signals. `Window.cpp:1624–1669` processes swallowing and removes group membership before `m_isMapped` becomes false (`:1676`). Null-buffer unmap follows the same chain (`XDGShell.cpp:567–570`). The core surface's destructor emitting only destroy (`src/protocols/core/Compositor.cpp:295–298`) does not itself prove skipped **XDG** unmap: the XDG surface-destroy listener supplies it. XDG registers this listener before creating the window (`XDGShell.cpp:533,593`); the window installs its own surface listener later (`Window.cpp:126–127`, `src/desktop/view/WLSurface.cpp:169`).

X11 also explicitly unmaps before surface dissociation (`src/xwayland/XSurface.cpp:86–93,112–115`; `src/xwayland/XWM.cpp:157–164,1235–1243`). `XWM.cpp:68–77`'s destroy notification handler itself lacks that fallback, but showing that a genuinely mapped/grouped window can reach it without the expected preceding unmap remains necessary.

**Dependency evidence — executed.** The isolated `dependency_contracts.cpp` check under the assigned scratch directory passed with installed hyprutils 0.14.2: listeners ran in registration order; a weak pointer remained valid and returned its object during the destructor, became false/null after deletion, and could not be locked during destruction. This corrects the dossier's conflation of destroying and deleted pointers. The test does not execute Hyprland or prove any resource-destruction ordering there.

**Reachability / limits.** Grouping requires a configured group rule or compositor action; ordinary Wayland map/unmap and ordinary X11 unmap do not establish the bad state. The dossier's proposed batching/disconnection race supplies no concrete role/backend state transition defeating the guards. A compositor event-loop ordering/lifecycle test demonstrating a retained grouped member after skipped unmap would settle this; none was performed.

**Verdict and severity.** Inconclusive as a reachable defect. The missing defensive fallback and unsafe consumer assumptions are source-confirmed; the client-to-expired-member producer is not. A proven local-client compositor crash would be High, not Critical.

**Corrections / minimal fix direction.** Do not label arbitrary surface destruction an established bypass. If hardening this lifecycle, make final membership cleanup idempotent and handle expired entries by stored identity; preserve ordinary unmap/focus/fullscreen ordering. Do not blindly add layout resurrection to final destruction.

## F-DSK-02

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

## F-DSK-03

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

## F-DSK-04

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

## F-DSK-05

**Claim.** Raw-`this` backend/surface callbacks survive window destruction unless `destroyWindow()` explicitly unregisters them, enabling later callbacks into freed memory.

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:133–150` does capture `this`, but `src/desktop/view/window/Window.hpp:298–317` owns the backend with `UP<IWindowBackend>` and owns every listener with `CHyprSignalListener`. The listener aggregate is declared after the backend, so normal member destruction releases the handles before destroying the backend. Installed hyprutils 0.14.2 `/usr/include/hyprutils/signal/Signal.hpp:21–22,48–55` stores ordinary listeners weakly and explicitly documents the lifetime contract:

```cpp
std::vector<CWeakPointer<CSignalListener>> m_vListeners;
[[nodiscard("Listener is unregistered when the ptr is lost")]]
CHyprSignalListener listen(...);
```

`src/desktop/view/WLSurface.cpp:31–32` calls `destroy()` from its destructor; `:140–156` resets the destroy-listener handle and resource. Even if its early return applies, automatic member destruction still releases the handle. The backend is owned by the window, whereas XWM owns the lower-level X surface; these are different objects (`src/xwayland/XWM.cpp:60–64`).

**Trigger/guard chain — source-confirmed.** Client protocol/X events → lower-level resource signal → owned backend → window-owned listener. Window destruction releases the handles and owned backend; later resource events cannot reach those released listeners. The only source call to `windowState()->removeSafe()` is `Window.cpp:1825`. The alleged monitor/workspace teardown calls do not exist; `windowState()->clear()` appears in compositor shutdown (`src/Compositor.cpp:579,595–607`), with shutdown already set.

**Verdict and severity.** Rejected; no demonstrated post-destruction UAF. This does not certify every possible synchronous callback reentrancy; it refutes the dossier's specific surviving-listener argument.

**Corrections / minimal fix direction.** Explicit reset is not the only unregister mechanism. No defect fix is required for this claim; locking a weak self in callbacks can be optional lifetime hardening where synchronous ownership release is possible.

## F-DSK-06

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

## F-DSK-07

**Claim.** Master orientation cycles divide by zero or index outside their vector, and zero population counts/geometry generate nonfinite positions and compositor DoS.

**Evidence — source-confirmed: cycle claim rejected.** `src/layout/algorithm/tiled/master/MasterAlgorithm.cpp:965–971` fills the fallback whenever the resulting vector is empty, regardless of whether `vars` was nonnull:

```cpp
if (vars != nullptr)
    buildOrientationCycleVectorFromVars(cycle, vars);
if (cycle.empty())
    buildOrientationCycleVectorFromEOperation(cycle);
```

The fallback adds every orientation (`:931–934`; enumeration `src/layout/algorithm/tiled/master/MasterAlgorithm.hpp:26–31`). Invalid tokens therefore do not reach a zero modulus. The complete callers (`MasterAlgorithm.cpp:770–775`) pass only `+1`, or `-1` with the default five-orientation fallback. Arbitrary large negative steps are not an input. The negative remainder formula is not a general-purpose wrap implementation, but those alleged step values are unreachable from these callers. There is no `masters:orientationcycle` dispatcher here; orientation changes are layout messages.

**Evidence — source-confirmed: population-count claims narrowed.** `calculateWorkspace()` returns without a master node (`:1027–1031`), so `MASTERS` is positive when master averages are used. A zero `STACKWINDOWS` does produce floating-point division at `:1079`, but stack placement returns at `:1185–1186`; no stack nodes are accumulated at `:1086–1090` in that case. Merely evaluating this unused infinity is not the claimed integer SIGFPE or propagated stack geometry. The first-window branch explicitly tests `WINDOWSONWORKSPACE == 1` (`:221–225`), so the else-branch division by `WINDOWSONWORKSPACE - 1` (`:261`) is not reached for one window.

**Evidence — source-confirmed: narrower geometric defect retained.** Work-area dimensions can be zero after reservation/gaps (F-DSK-11). `recalculate()` reaches `calculateWorkspace()` without a geometry-size guard when the workspace is not fullscreen (`MasterAlgorithm.cpp:508–517`). With multiple targets, smart resizing enabled (default true, `src/config/values/ConfigValues.cpp:793`), and zero extent on the split axis, the accumulated size for an occupied master/stack class is zero (`MasterAlgorithm.cpp:1077–1090`). Its placement loop computes a zero-over-zero ratio:

```cpp
nd->percSize *= WORKAREA.h / masterAccumulatedSize;
HEIGHT = masterAverageSize * nd->percSize;
```

This is `:1170–1177` for the ordinary vertical stack; the analogous width calculation is `:1133–1140`. The single-window return (`:1094–1114`) does not cover the multi-target case. Nonfinite geometry is submitted to `setPositionGlobal()`; `src/layout/target/Target.cpp:10–18` stores and rounds it without a finite-value check. No source in these steps establishes the dossier's later giant allocation or memory-corruption endpoint.

**Input/guard chain — source-confirmed.** User-controlled layout/gap configuration → `CSpace::recheckWorkArea()` → zero work-area extent → multi-target master recalculation → smart-resize normalization with zero numerator and denominator → nonfinite `percSize` and requested target geometry. Under ordinary positive-size geometry the alleged zero *population* endpoints are blocked as above. A client merely mapping/unmapping windows does not supply arbitrary layout-cycle integers or guarantee the degenerate geometry. No runtime crash or graphics-allocation test was performed.

**Verdict and severity.** Partially: configuration-dependent nonfinite geometry is real; the empty-cycle SIGFPE, large-negative indexing, single-window forced-floating explanation and demonstrated DoS are rejected. Low correctness/robustness defect, sharing the geometric prerequisite of F-DSK-11 rather than an independently demonstrated client-crash vulnerability.

**Corrections / minimal fix direction.** Validate positive finite work-area dimensions before normalization; guard accumulated sizes and initialize unused averages only when needed. Normalize signed indices correctly if exposing general step inputs in future. Adding `max(1, population)` everywhere would hide invariants and would not fix zero geometric extent.

## F-DSK-08

**Claim.** Scrolling movement/unmap exposes empty-column unsigned underflow, out-of-range insertion offsets, and underflowing height indices.

**Evidence — source-confirmed.** `src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.cpp:290–298` has no empty guard:

```cpp
for (size_t i = 0; i < targetDatas.size() - 1; ++i) {
    if (targetDatas[i] != w)
        continue;
    return targetDatas[i + 1];
}
```

On an already empty vector its bound underflows and the first subscript is invalid. Indexed `add()` likewise assumes `after` is in range (`:140–147,174–181`), and `idx()` returns zero on a miss (`:197–203`). Those are real internal API weaknesses.

The dossier overlooks important callers. The `next()` resize call is guarded by `targetDatas.size() > 1` (`:912–915`). Its focus call (`:1463–1468,1497–1499`) starts from `dataFor()`; that function returns a record only by iterating the currently stored column records (`:1917–1945`). An empty column cannot supply that record. Removing its last target unregisters the column (`:259–260`, `:350–358`). Input events are serialized; “map/unmap race” alone does not show reentrancy between lookup and this next call.

Height indexing has the guard the dossier says is missing (`:205–213`):

```cpp
if (targetDatas.empty())
    return 0;
// ...
return targetDatas.size() - 1;
```

Unindexed add appends before calculating `size()-1` (`:136–137,169–171`). Directional moves use indexed insertion only when the destination column is nonempty (`:1092–1095,1115–1118`); otherwise they append. Drop insertion obtains both its column and target from a successful `dataFor()` (`:732–733,760–763`). A not-found `idx()==0` is a valid insertion position for any nonempty vector, so it does not by itself explain the alleged out-of-bounds offset. Column-level index helpers are distinct: `SScrollingData::idx()` returns `-1` (`:341–347`), not zero.

**Trigger/guard chain — source-confirmed.** Layout focus/resize/movement → current target lookup → record in a nonempty registered column → valid neighbor/index operation. The claimed failure requires retaining or manufacturing a record pointing to an empty/unregistered column, or another invalid caller offset. No such producer was established from the dossier's moves/unmaps. Lower-level empty-column behavior is unsafe, but that is not a closed client-to-memory-corruption chain.

**Verdict and severity.** Partially: missing defensive bounds checks exist; the stated height-index underflow is guarded, and client reachability of the remaining invalid state is unproved. Informational latent bounds risk, not a confirmed Medium memory-corruption finding. No malformed-state or desktop reproducer was executed.

**Corrections / minimal fix direction.** Add an early empty return to `next()`, validate insertion offsets, and give `idx()` a distinct not-found result. Keep target/column/controller size invariants explicit. A regression test should cover valid last-member removal and destination selection; direct invocation on fabricated invalid state alone would not demonstrate client reachability.

## F-DSK-09

**Claim.** `removeFsTarget(nullptr)` nondeterministically erases an expired or live key and invalidates the iterator saved by fullscreen synchronization.

**Evidence — source-confirmed.** `src/layout/algorithm/tiled/scrolling/ScrollingFullscreenHandler.cpp:464–470` handles expired/invalid targets locally:

```cpp
const auto TARGET = it->first.lock();
if (!TARGET || !TARGET->window() || TARGET->space() != getSpace() ||
    !m_scrollingAlgorithm->dataFor(TARGET, true)) {
    removeTargetFromList(it); // it = m_fsTargets.erase(it), line 453
    continue;
}
```

The helper saving `std::next(it)` (`:455–458`) is only reached later, with a live `TARGET` strong reference. `removeFsTarget()` performs `m_fsTargets.find(target)` and returns on a miss before the null branch (`:547–555`). The container is `std::unordered_map<WP<ITarget>, ...>` (`src/layout/algorithm/tiled/scrolling/ScrollingFullscreenHandler.hpp:87`). Installed `/usr/include/hyprutils/memory/WeakPtr.hpp:193–199,279–283` compares and hashes the control-block identity (`impl_`), not the result of `get()` or `lock()`.

**Trigger/guard chain — source-confirmed.** Fullscreen changes/layout synchronization → iteration → expired keys erased using their actual iterator; live keys passed by strong pointer to exact-identity lookup. An empty `SP` becomes an empty weak key, whose identity differs from every expired nonempty weak key. Therefore it cannot select one of several expired keys or erase a live key. The warning comment at `:539–545` is inconsistent with these actual pointer semantics and is not evidence of nondeterminism.

**Verdict and severity.** Rejected; no arbitrary erase, stale-iterator path, or resulting fullscreen corruption established by the claimed mechanism.

**Corrections / minimal fix direction.** Correct the misleading comment and optionally reject null immediately for clarity. This is documentation/defensive cleanup, not a confirmed memory-safety fix. No desktop execution was performed.

## F-DSK-10

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

## F-DSK-11

**Claim.** Work-area computation can leave nonpositive dimensions that reach layout arithmetic, potentially producing nonfinite geometry.

**Evidence — source-confirmed.** `src/layout/space/Space.cpp:77–106` starts with the monitor box minus reservations, takes the configured/global or workspace-rule outer gaps, applies them and stores the resulting box:

```cpp
reservedFloatGaps.applyip(floatWorkArea);
reservedGaps.applyip(workArea);
m_workArea = workArea;
m_floatingWorkArea = floatWorkArea;
```

There is no positive-size clamp. `src/desktop/reserved/ReservedArea.cpp:53–66` simply subtracts the sums of opposing reservations:

```cpp
other.w -= m_topLeft.x + m_bottomRight.x;
other.h -= m_topLeft.y + m_bottomRight.y;
```

Its constructors clamp individual reservations to nonnegative values (`:10–16`), which does not cap their sum against the box. `src/output/Monitor.cpp:1689–1694` supplies the logical monitor box and applies reservations the same way. Global outer gaps have no monitor-relative upper bound (`src/config/values/ConfigValues.cpp:170`; `src/config/values/types/CssGapValue.hpp:11–15`; `src/config/lua/types/LuaConfigCssGap.cpp:18–27,33–45`). A legal positive gap can exceed the currently available extent; a later smaller output is not necessary to establish this arithmetic.

**Input-to-defect chain — source-confirmed.** User configuration or workspace-rule gaps/reservations → `CSpace::recheckWorkArea()` → unchecked subtraction → stored nonpositive `m_workArea` → `CSpace::recalculate()` invokes the algorithm (`Space.cpp:149–153`). Master reads this area directly (`src/layout/algorithm/tiled/master/MasterAlgorithm.cpp:1054`) and can generate nonfinite normalization when the relevant extent is zero (F-DSK-07). Monocle submits the area directly as its layout box (`src/layout/algorithm/tiled/monocle/MonocleAlgorithm.cpp:158–161`). Dwindle's aspect comparison divides height by width (`src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.cpp:182–188`), but that comparison alone does not prove propagated NaN or a crash.

**Reachability and preconditions.** The retained defect is configuration/geometry robustness: a valid monitor/workspace, configured gaps/reservations consuming its available extent, and layout recalculation. Small-resolution migration can expose it after otherwise ordinary settings. Ordinary client-supplied window geometry is not shown to directly choose the monitor-relative gaps. The dossier's optional output-management attack and claimed allocation exhaustion are not needed for this result and were not verified. Default gaps are finite and modest; ordinary monitor dimensions do not automatically produce the issue. No output changes or compositor execution were performed.

**Verdict and severity.** Confirmed at Low for unchecked nonpositive work-area geometry and its layout propagation. Confirmation does not extend to a local-client crash, driver behavior, memory corruption, or huge allocations. F-DSK-07's narrower NaN case shares this prerequisite.

**Corrections / minimal fix direction.** `gaps_in` affects per-window geometry later; this particular space computation uses outer and floating gaps plus monitor reservations. Enforce a well-defined positive minimum or reject/suspend layout for unusable work areas, including floating areas, and protect normalization against nonfinite inputs. Preserve a useful diagnostic rather than silently feeding invalid boxes downstream.

## F-DSK-12

**Claim.** A pinned client obtains focus while the focused monitor is null, dereferencing `m_focusMonitor`; the dossier suggests an X11 ABOVE hint can self-pin it.

**Evidence — source-confirmed.** `src/desktop/state/FocusState.cpp:165–166` has the unguarded access:

```cpp
if (pWindow->m_state & WINDOW_STATE_PINNED)
    pWindow->m_workspace = m_focusMonitor->m_activeWorkspace;
```

The earlier raw-focus guard rejects null/unmapped windows (`:129–155`), but does not check the focused monitor before this assignment. `rawMonitorFocus(nullptr)` clears it (`:272–278`); the last-output disconnect also directly resets focused window/surface/monitor and then clears the monitor's active workspace (`src/output/Monitor.cpp:447–460`). Thus null focused-monitor state exists in source; it is not merely an impossible pointer value.

The proposed startup producer is blocked: mapping chooses/focuses a monitor and returns before setting `m_isMapped` if no valid monitor/workspace exists (`src/desktop/view/window/Window.cpp:1156–1168`). Pinning is a compositor action with floating/fullscreen, monitor and workspace checks (`src/config/shared/actions/ConfigActions.cpp:286–323`), or a configured pin rule (`Window.cpp:1296–1299`), with pinned-parent inheritance (`src/protocols/XDGShell.cpp:595–596`). No source mapping `_NET_WM_STATE_ABOVE` to `WINDOW_STATE_PINNED` was found. The X11 backend state-request fields carry fullscreen/maximize/minimize, not a pin request (`src/desktop/view/window/X11Backend.cpp:188–207`).

**Input/guard chain — source-confirmed up to an ordering gap.** Existing configured/action-pinned mapped window → last-output removal clears focused monitor → a later focus attempt would reach the unguarded pinned assignment if the window remained mapped and focus were processed before recovery. Normal managed-client activation is policy-gated (`Window.cpp:824–835`); raw/full focus additionally checks mapped state and workspace (`FocusState.cpp:75–78,129`). A fallback monitor is requested on removal (`src/state/FallbackState.cpp:26–39`) and initialized in deferred event-loop work (`:118–143`). That recovery must be considered, rather than assuming a stable monitorless desktop.

**Reachability / missing evidence.** A lifecycle/event-order test is needed to establish that a surviving pinned window's accepted focus request can run in the monitorless interval, and whether fallback creation/workspace migration always precedes it. The dossier supplies no such ordering evidence. Configured pinning is a prerequisite; the alleged self-pin hint is not supported. No physical/virtual monitor manipulation, IPC, or live focus test was run.

**Verdict and severity.** Inconclusive. An unguarded nullable-state access is source-confirmed, but an accepted externally triggered focus in the required interval is not. A confirmed compositor crash would be High; the dossier's Low label would then understate availability impact, while current evidence does not justify assigning a confirmed crash severity.

**Corrections / minimal fix direction.** Guard/lock the focused monitor before the pinned assignment, and define focus behavior while no active workspace is available. Add a lifecycle regression covering removal/recovery with pinned windows. Do not describe the ABOVE hint or startup mapping as established ways to reach the access.

## F-DSK-13

**Claim.** The size-animation begin callback queues raw-`this` work whose weak-boolean check permits execution against a partially destroyed window.

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:562–574` contains the quoted `doLater([this, self = m_self] { if (!self) return; sendWindowSize(); })`. Installed `/usr/include/hyprutils/memory/WeakPtr.hpp:154–190,221–226` confirms that boolean validity remains true during destruction, whereas `lock()` rejects that state. It also shows why “expired always means operator-> returns null” is incorrect: `get()` checks the data pointer, not the destroying flag.

`src/managers/eventLoop/EventLoopManager.cpp:215–235` queues work and registers a Wayland **idle** callback; only that callback runs the queued functions:

```cpp
m_idle.fns.emplace_back(NEW_SEQ, fn);
m_idle.eventSource = wl_event_loop_add_idle(m_wayland.loop, ...);
// idle callback:
for (auto& f : fns) {
    if (f.second)
        f.second();
}
```

**Trigger/guard chain — source-confirmed.** Mapped-window size animation → enqueue idle work → next idle dispatch. A synchronous window destructor completes before control returns to the same event loop. If destruction happened in between, the retained weak handle has no live data and `!self` returns. If the window remains alive, the callback accesses a live object. The inspected `CWindow` destructor (`Window.cpp:165–175`) does not dispatch a nested event loop. No background-thread destruction or nested-loop trigger was established.

**Verdict and severity.** Rejected as a reachable destroying-window UAF. Different pointer semantics alone do not produce concurrent execution. This is source reasoning, not a runtime race test.

**Corrections / minimal fix direction.** Capturing only the weak handle and calling through `if (auto w = self.lock())` is clearer and provides a strong lifetime lease, but is defensive hardening rather than proof of the claimed crash. Animation ownership is not an independently established exploit path.

## F-DSK-14

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

