# F-SEAT-09 set_position 不校验布局合法性（重叠/越界）→ 监视器布局破坏

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Overlap prohibition and signed-16-bit limits are unsupported; extreme accepted coordinates can exceed integer extents during automatic arrangement.

**Verdict: partially. Revised severity: Low; numeric robustness.**

**Claim.** Accepting overlapping or arbitrary positions is intrinsically invalid, with extreme coordinates causing layout arithmetic failures.

**Evidence — source-confirmed.** `src/protocols/OutputManagement.cpp:489-503` stores signed 32-bit wire coordinates in `Vector2D`; apply copies them (`:395-398`), and the monitor rule override copies the offset (`src/config/shared/monitor/MonitorRuleManager.cpp:62-65`). Position acceptance is real:

```cpp
// OutputManagement.cpp:500-501
m_state.committedProperties |= OUTPUT_HEAD_COMMITTED_POSITION;
m_state.position = {x, y};
```

**Protocol correction — source-confirmed.** `protocols/wlr-output-management-unstable-v1.xml:569-574` defines signed integer global coordinates without an overlap prohibition or signed-16-bit bound. Its head error enumeration (`:539-546`) contains no `INVALID_POSITION`. Core output geometry itself uses signed integer coordinates (`/usr/share/wayland/wayland.xml:2969-2972`, `wl_output.geometry`), not the dossier's claimed signed-16-bit limit. Upstream intentionally applies explicit positions and reports overlap as a warning (`src/state/MonitorPositionController.cpp:21-34`; `src/state/MonitorLayoutController.cpp:40-53`). Merely configuring an overlap is therefore not enough to establish this security finding.

**Residual numeric path — source-confirmed.** Scheduled reload ends in arrangement (`MonitorRuleManager.cpp:193-194`; `MonitorLayoutController.cpp:56-68`). `CMonitor::explicitPosition` returns the configured offset except for its auto-placement sentinel (`src/output/Monitor.cpp:1219-1223`); `moveTo` stores it (`:1648-1654`). For remaining automatically positioned monitors, `MonitorPositionController.cpp:40-56,61-62` accumulates right/bottom extents in **int** variables using `std::max<double>(position + size, oldExtent)`. An accepted coordinate near the positive signed-int limit plus a normal positive monitor extent can exceed that accumulator's range. Conversion back to int is not guarded. This establishes a numeric robustness defect when explicit and automatic placement coexist; it is not evidence that the original position assignment overflows (that assignment uses floating-point vectors).

**Severity and limits.** Low for the narrowed range-handling issue in privileged output configuration. No compositor crash, trapped focus, or lock bypass was executed or inferred from the range issue alone. A configuration where every monitor is explicitly placed does not run this particular auto-placement accumulator loop. The general overlap/forbidden-layout claim is rejected; the numeric-extents subclaim is retained with the specific sink above.

**Minimal fix direction.** Keep position and extent arithmetic in a sufficiently wide representation, validate conversion ranges including output size, and handle unsupported extreme layouts cleanly. Do not add a fictitious protocol error or prohibit all overlaps solely on this dossier's authority.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
