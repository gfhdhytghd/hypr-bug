# F-SEAT-12 触摸焦点记账是"单槽"模型：per-finger 状态共享一个槽位，合成 up 依赖会被覆盖的监听器

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Shared touch focus and destroy listeners can misroute contacts; physical multi-touch is required, and permanent lock underflow is unproven.

**Verdict: partially. Revised severity: Medium.**

**Claim.** Shared touch focus/listeners fail to represent multiple contacts, leading to lost/misrouted termination events and potentially corrupt touch-lock accounting.

**Evidence — source-confirmed.** `src/managers/SeatManager.cpp:507-529` resets one destroy listener and focus resource for every new down, stores the new surface, and increments a single count. It retains no contact-ID map. Termination resolves the client through the current shared focus (`:535-553`), irrespective of the original down's owner.

```cpp
// SeatManager.cpp:527, 539, 549, 553
m_listeners.touchSurfaceDestroy = surf->m_events.destroy.listen(
    [this, timeMs, id] { sendTouchUp(timeMs + 10, id); });
auto client = m_state.touchFocusResource->client();
t->sendUp(timeMs, id);
m_touchLocks--;
```

At the per-resource level, `src/protocols/core/Seat.cpp:27-56` likewise replaces one current surface/destroy listener per down and maintains only `m_fingers`. Its header confirms a single listener (`src/protocols/core/Seat.hpp:60-68`). No per-ID membership check protects `sendUp`.

**Path and preconditions — source-confirmed.** Physical/device touch events enter `src/managers/input/Touch.cpp:17-62`; gesture-consumed touches and invalid device/monitor cases are filtered (`:30-53`, `:71-103`). For delivered contacts, each down overwrites `m_touchData.touchFocus*` and calls `sendTouchDown` (`:106-147`). There is no first-contact focus latch for all subsequent non-gesture fingers. A later contact on another surface/client can replace the stored focus; a prior contact's eventual real up is forwarded through that later shared target (`Touch.cpp:173-174`; `SeatManager.cpp:539-549`). Thus a client can receive an up for an ID it never received, while the original recipient does not receive its matching up. Surface destruction replaces the one retained synthesized-up hook rather than tracking all live contacts. Physical multi-touch, or a separately authorized input source, is needed; ordinary clients do not send `wl_touch.down` (it is a server event).

**Corrections and severity.** Medium for multi-touch routing/lifecycle correctness affecting multiple clients under these preconditions. F-SEAT-02/03 DnD requests do not themselves manufacture touch contacts or establish this hardware precondition. A replaced destroy listener alone does not prove a permanently missing physical release: the eventual real release may still occur. `m_touchLocks` is a signed int guarded at zero (`SeatManager.hpp:197`; `SeatManager.cpp:536`); the dossier's precise permanent underflow/race sequence is not established, and callback ordering was not executed. The two distinct destroy callbacks can complicate duplicate termination, but that is not a demonstrated underflow. Synthetic timestamps using the original down time plus ten are confirmed; harmful client timing effects are unmeasured.

**Minimal fix direction.** Track live contacts by device/contact ID, original target and destruction listener; terminate each contact exactly once and derive lock counts from those records. Cancel appropriate contacts on target destruction and preserve gesture-consumption policy. Validate using isolated multi-client multi-touch tests; no live input test was run here.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
