# F-SEAT-10 pointer.set_cursor 的 serial 校验被注释禁用 → 光标图像注入无输入合法性约束

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Serial validation is disabled, but current client focus is enforced; focus-loss cursor injection is unsupported.

**Verdict: partially. Revised severity: Low.**

**Claim.** Disabled cursor serial checks allegedly permit stale-serial cursor changes even after pointer focus has left the client.

**Evidence — source-confirmed.** `src/protocols/core/Seat.cpp:119-137` resolves the cursor surface, checks its role, and calls `CSeatManager::onSetCursor`. At `src/managers/SeatManager.cpp:678-690`, serial validation is commented out, but an active guard remains:

```cpp
if (!m_state.pointerFocusResource || !seatResource ||
    seatResource->client() != m_state.pointerFocusResource->client())
    return;
// serialValid(...) is commented out
m_events.setCursor.emit(SSetCursorEvent{surf, hotspot});
```

**Path and reachability — source-confirmed.** A client's cursor request reaches this handler; an arbitrary/obsolete serial is accepted only if the requesting seat resource's client is the current pointer-focus client. Once focus belongs to another client, the handler returns before the cursor event. There is no demonstrated interleaving that bypasses this synchronous comparison; merely calling the request close to a focus change is not a closed race argument. Libwayland resource ownership also prevents substituting another client's pointer or surface object. Ordinary clients can legitimately choose, hide, and animate their own focused cursor.

**Protocol correction.** `/usr/share/wayland/wayland.xml:2186-2188` requires the **latest pointer-enter serial**, not an arbitrary button serial and not single-use authorization. `CWLPointerResource::sendEnter` records that serial via `nextSerial(..., true)` (`Seat.cpp:182`), but the shared seat-level serial store can be overwritten across multiple pointer resources. Blindly restoring generic `serialValid(..., erase=true)` is not the correct fix. Focused clients do not need a new button event for each cursor update.

**Severity and minimal fix direction.** Low for a real conformance/freshness defect; cross-client cursor-image injection after focus loss and phishing impact are unproven. Track each pointer resource's latest enter serial and validate it together with appropriate current focus/current-cursor rules. Preserve repeated legal updates using that serial. No visual or live focus-race test was run.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
