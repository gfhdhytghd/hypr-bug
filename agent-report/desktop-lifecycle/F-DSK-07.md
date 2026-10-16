# F-DSK-07 Master-layout empty / zero-size arithmetic

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Empty-cycle and single-window claims fail guards; zero work-area smart resizing can produce NaN, without demonstrated crash or allocation exhaustion.

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

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
