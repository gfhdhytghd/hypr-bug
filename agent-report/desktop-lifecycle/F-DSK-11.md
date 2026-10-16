# F-DSK-11 Negative work-area geometry reaches layouts

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Desktop / group / workspace lifecycle. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Configured reservations/gaps can produce zero or negative work areas; geometry propagation is confirmed, while compositor crash and output-protocol abuse remain unverified.

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

[Full dossier-level verification report](_evidence/full-audit-10-desktop-lifecycle.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md). These remain agent evidence, not human-confirmed material.
