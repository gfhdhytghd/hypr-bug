# F-SEAT-14 serial 存储记账缺陷：enterSerial 永不消费、serials 上限 100 可被挤掉 → serial 重放

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Latest enter serial bypasses consumption for repeated token creation; normal serials are consumed, motion adds none, and move/resize use stronger validation.

**Verdict: partially. Revised severity: Medium.**

**Claim.** Stored enter serials can be replayed indefinitely; a 100-element store and high-frequency motion allegedly also make ordinary serials replayable across activation, warp, and move/resize.

**Evidence — source-confirmed.** `src/managers/SeatManager.cpp:112-131` stores one latest pointer-enter serial separately from ordinary serials. `serialValid` has an early return for that enter serial, while ordinary matched serials are erased when requested (`:134-153`). The default is `erase=true` (`src/managers/SeatManager.hpp:109`).

```cpp
// SeatManager.cpp:142-148
if (container->enterSerial == serial)
    return true;
// ordinary serial match:
if (erase)
    container->serials.erase(it);
```

The per-seat container initializes the special serial to zero and deliberately remembers it (`SeatManager.hpp:174`); the ordinary store cap is 100 (`:11`). Pointer enter updates it (`src/protocols/core/Seat.cpp:182`), and pointer leave does not clear it (`:185-204`).

**Path and preconditions — source-confirmed.** A client that has legitimately received a nonzero pointer-enter serial on a live seat resource can present that still-latest special serial during activation-token commit. `src/protocols/XDGActivation.cpp:35-46` uses the default consuming validator; its special branch nevertheless succeeds without consumption. The token handler does not separately check recent input time/current focus. Each successful commit can register a token; a single token resource rejects a second commit (`:25-33`), so the issue is reauthorization of distinct token creations, not recommitting one resource. Activation removes each issued token when used (`:83-102`), and sent-token bookkeeping has a per-client cap (`:48-59`). Those guards do not make the source serial fresh or consumed.

**Corrections — source-confirmed.** Ordinary stored serials are **not** reusable under default consuming validation. Pointer motion emits no serial (`Seat.cpp:207-216`), so motion alone cannot evict the ordinary store. Eviction can reject an older legitimate event serial but does not cause replay. Pointer warp explicitly calls `serialValid(..., false)` and independently requires exact pointer-focus surface and in-surface coordinates (`src/protocols/PointerWarp.cpp:28-31,56-76`); repeated serial use there is intentional and does not confer background/global warp authority. XDG move/resize use the separate surface-bound pointer-button validator (`src/protocols/XDGShell.cpp:202,227`; `SeatManager.cpp:185-204`), not this generic enter bypass. No active PointerConstraints call to `serialValid` was found. The actual activation API is token `set_serial`/`commit`, then manager `activate`, not the dossier's `request_activate` call.

**Severity and minimal fix direction.** Medium for stale interaction authorization in activation-token issuance; focus stealing policy effects require separate application/focus-path validation, and no live activation was attempted. Keep protocol-specific serial semantics: enforce recent qualifying interaction for token issuance, avoid treating a remembered enter serial as unlimited consuming authorization, and keep pointer-enter reuse valid for cursor operations. Increasing the ordinary ring size does not fix this distinction.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
