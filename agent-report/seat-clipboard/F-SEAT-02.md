# F-SEAT-02 wl_data_device.start_drag 无 serial/按钮/焦点校验 → 无按钮事件的注入式拖放

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Drag activation ignores implicit-grab serial and origin focus; downstream drop still depends on input and target behavior.

**Verdict: confirmed. Revised severity: Medium.**

**Claim.** Drag initiation fails to validate the active implicit grab and its origin/serial.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:264-279` resolves the supplied source, warns on reuse, marks it as DnD, and invokes `initiateDrag`; `serial` is unused. There is no focus, active-button, or origin-grab check. The core protocol requires an active implicit grab matching the origin and serial (`/usr/share/wayland/wayland.xml:945-947`).

```cpp
// DataDevice.cpp:276-278
source->m_dnd = true;
PROTO::data->initiateDrag(source, icon ? CWLSurfaceResource::fromResource(icon) : nullptr,
                          CWLSurfaceResource::fromResource(origin));
```

**Path and reachability — source-confirmed.** An ordinary client request with live same-client typed resources reaches `initiateDrag` (`DataDevice.cpp:557-664`), installs a cursor override, assigns `currentSource`, and clears pointer focus while retaining DnD focus. `updateDrag` resolves the actual DnD-focused surface/device and offers data (`:666-712`). No implicit-grab validation is inserted downstream. A missing target prevents offer delivery (`:673-680`), but does not authorize initiation. Input release listeners (`:595-611`) call `dropDrag`; success additionally depends on an accepted/received offer (`:737-778`).

**Impact and corrections.** Medium for unauthorized global input/drag-state interference; combine with F-SEAT-03 only when separately assessing that availability defect. Unsolicited state activation is source-confirmed; arbitrary file reads, copying protected files, or application command execution are not. A client cannot place a normal surface arbitrarily using `set_cursor`; target selection still follows compositor hit testing. No input or target application was exercised. The existing button-serial facility (`src/managers/SeatManager.cpp:156-204`, recorded/cleared by `src/protocols/core/Seat.cpp:239-245`) is useful evidence that a suitable validation mechanism exists, not evidence that drag currently calls it.

**Minimal fix direction.** Validate the matching origin-bound active implicit grab before any DnD state mutation, covering supported pointer, touch, and tablet initiation. Validate icon role and legitimate null-source semantics independently; do not require pointer-only focus for every input modality.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
