# F-DSK-08 Scrolling-layout index and size arithmetic

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Empty next() is unsafe in isolation; guarded insertion callers and membership-based lookup leave the proposed client-triggered bounds violation unproved.

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

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
