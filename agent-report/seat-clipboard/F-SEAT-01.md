# F-SEAT-01 set_selection 不校验请求客户端键盘焦点、serial 完全忽略 → 后台客户端剪贴板/中键粘贴劫持

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Background selection replacement confirmed; this does not itself disclose the previous clipboard contents.

**Verdict: confirmed. Revised severity: Medium.**

**Claim.** Ordinary selection setters accept requests without checking sender focus or the triggering serial.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:248-262` declares `uint32_t serial` but calls `g_pSeatManager->setCurrentSelection(source)` without using either serial or requester identity; the null-source branch also clears selection without authorization. `src/protocols/PrimarySelection.cpp:114-136` has the same omission, behind `misc:middle_click_paste`. Its default is `true` (`src/config/values/ConfigValues.cpp:594`).

```cpp
// DataDevice.cpp:259-261
source->markUsed();
g_pSeatManager->setCurrentSelection(source);
```

**Path and reachability — source-confirmed.** A connected client's ordinary data-device selection request reaches the setter above, then `src/managers/SeatManager.cpp:697-723`: the old source is cancelled, the global weak selection reference is replaced, and each selection protocol is notified. Core delivery selects the current keyboard-focus client (`DataDevice.cpp:505-520`), rather than authorizing the sender. Primary replacement follows `SeatManager.cpp:726-752` and is distributed using pointer focus (`PrimarySelection.cpp:292-307`). No additional permission or focus guard occurs in these paths. Core and primary globals are available even to security-context sandbox clients (`src/managers/ProtocolManager.cpp:361-387`; `src/Compositor.cpp:267-271`). Valid typed resources still belong to the requesting connection; cross-client object forgery is unnecessary.

**Impact and corrections.** Medium: cross-client clipboard integrity and availability loss, with downstream paste behavior controlled by the receiving application and user. A source's `send` callback receives a destination descriptor for its own offered data; replacement does **not** return the previous owner's clipboard data. The dossier's direct sniffing claim and implied command execution are unsupported. The primary protocol describes keyboard-focus delivery (`/usr/share/wayland-protocols/unstable/primary-selection/primary-selection-unstable-v1.xml:45-50`), so the dossier's assertion that pointer-focus authorization is universally the reference requirement is too simple. No runtime paste was tested.

**Minimal fix direction.** Authorize ordinary selection changes against the appropriate seat interaction/focus and validate serial freshness/order with protocol-compatible rules, including clear requests. Keep explicitly privileged clipboard-manager behavior separate; avoid a universal consume-once rule for unrelated serial uses.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
