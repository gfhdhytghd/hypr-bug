# F-DSK-09 Scrolling fullscreen bookkeeping retains invalid targets

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Expired keys are erased by iterator; weak-pointer identity hashing prevents null from matching arbitrary expired entries.

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

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
