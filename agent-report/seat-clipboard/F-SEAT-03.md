# F-SEAT-03 拖放结束依赖 target 客户端自愿 `offer.finish`，无超时/取消兜底 → dndActive 永久为真，全局丢弃按钮事件

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Post-drop source retention keeps dndActive true and suppresses client pointer buttons until explicit cleanup.

**Verdict: confirmed. Revised severity: High.**

**Claim.** After a successful Wayland drop, a destination that never finishes can keep the compositor's ordinary pointer-button delivery disabled.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:737-766` sends drop and leave, removes the visual override, then calls `cleanupDndState(false, false, false)`. That function removes input listeners but preserves both current source and focused device (`:715-734`). The activity predicate is solely `return !!m_dnd.currentSource` (`:852-854`).

```cpp
// SeatManager.cpp:438-440
if (!m_state.pointerFocusResource || (PROTO::data && PROTO::data->dndActive()))
    return;
```

**Path and preconditions — source-confirmed.** An established drag with a live source and Wayland destination passes `wasDragSuccessful` when a non-dead matching offer has either `m_recvd` **or** `m_accepted` (`DataDevice.cpp:769-778`). On the ordinary release-driven drop path, the retained source keeps the activity predicate true; subsequent `CSeatManager::sendPointerButton` calls return before any client button event. `offer.finish` normally invokes complete/abort (`:71-76`); `completeDrag` resets state (`:789-798`). There is no timeout in this state machine. Source destruction aborts (`:478-480`), and the references are weak (`src/protocols/core/DataDevice.hpp:175-179`, `m_dnd.currentSource`); the source must remain alive. X11 destinations explicitly clear state (`DataDevice.cpp:751-758`) and are excluded.

**Recovery and corrections — source-confirmed.** `abortDndIfPresent` calls `abortDrag` (`:856-859`); Escape handling calls it at `src/keybinds/Manager.cpp:212-213`, before ordinary hotkey handling; keyboard eligibility/translation guards precede it (`:198-205`). Starting another drag first aborts the old one (`:559-562`). Merely changing DnD focus calls `updateDrag` (`:547-554`), which does not clear the source, so the dossier's suggestion that focus change necessarily recovers is incorrect. Offer destruction sends DnD-finished to the source (`:80-85`) but does not directly reset this compositor state. This is a source-closed state-retention and button-suppression defect, not an executed desktop freeze.

**Severity.** High: a local client participating in DnD can withhold completion and cause desktop-wide loss of normal Wayland pointer buttons; compositor shortcuts and explicit recovery remain possible. It is not a compositor crash or an irrevocable lockout.

**Minimal fix direction.** End the input grab at drop independently of transfer lifetime. Preserve only transfer bookkeeping until finish, with cancellation on endpoint destruction and bounded cleanup. Do not keep the global input-suppression predicate true while awaiting destination cooperation.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
