# F-SEAT-15 X11↔Wayland 剪贴板桥与粘贴大小上限不在本域；本 fork 无 ClipboardManager

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Pure Wayland selection transfer forwards file descriptors without caching payload; XWayland buffering remains a separate audit boundary.

**Verdict: confirmed. Revised severity: Informational.**

**Claim.** There is no ClipboardManager payload cache in the core Wayland path, so an unbounded paste-payload allocation finding is not established in this domain.

**Evidence — source-confirmed.** A source-name search under `src` finds no `ClipboardManager` class/file. Core receive wraps the incoming descriptor and forwards it to `IDataSource::send` (`src/protocols/core/DataDevice.cpp:44-65`); the ordinary Wayland source validates MIME then emits `sendSend(mime, fd.get())` (`:187-193`). Primary and clipboard-manager Wayland sources follow the same pattern (`src/protocols/PrimarySelection.cpp:81-87`; `src/protocols/DataDeviceWlr.cpp:80-86`; `src/protocols/ExtDataDevice.cpp:80-86`). Selection ownership stores references/listeners, not paste bytes (`src/managers/SeatManager.cpp:697-752`).

```cpp
// DataDevice.cpp:63, 193
m_source->send(mime ? mime : "", std::move(sendFd));
m_resource->sendSend(mime.c_str(), fd.get());
```

**Path and boundary — source-confirmed.** The receiving client requests an offered representation; the compositor passes the destination descriptor to the selected Wayland source; the clients transfer payload through that descriptor. The compositor does retain MIME strings, resources and protocol transport messages, so this is specifically an absence of payload caching, not a claim of zero memory/resource costs. F-SEAT-04 discusses transfer-request pressure separately. X11 sources dispatch into `src/xwayland/XDataSource.cpp:51` and XWM selection-transfer machinery, so payload limits there require the separate XWayland audit; they cannot be settled by this core forwarding path.

**Severity and corrections.** Informational control, confirmed. No compositor-payload OOM claim is retained for a purely Wayland-to-Wayland paste. The dossier's broader statement that cursor SHM is the compositor's only other data buffer is outside this narrow conclusion and should not be generalized to all compositor subsystems. No payload transfer or X11 size test was run.

**Minimal fix direction.** No paste-payload cap is needed on a nonexistent core payload cache. If a cache is introduced, define ownership, quotas and eviction explicitly; separately validate bridge buffering and generic per-client metadata/resource accounting.

## Verification artifacts and limits

All scratch files are under `/home/wilf/data/.claude-scratch/agent-reports/work/09-seat-clipboard/`:

- `seat_schema.c`, `seat_schema`, `seat_schema.log`: **executed**, exported libwayland-server method-schema assertions passed; installed library version 1.26.0.
- `seat_argument_guard.c`, `seat_argument_guard`, `seat_argument_guard.log`: compiled isolated unnamed-socketpair guard control; **execution blocked** at `wl_client_create` with `Operation not permitted`, before request dispatch. An initial assertion failure and the diagnostic rerun both occurred before dispatch. No validation result is inferred from this attempt.
- Report-generation scripts: incremental writing helpers only; they do not modify upstream or contact a compositor.

No live paste, drag, touch, output reconfiguration, cursor rendering, victim flooding, backend allocation test, or full build was performed. Installed protocol XML/header evidence is identified by its absolute path and is distinct from pinned upstream source evidence. The dossier's blanket wlroots behavior/limits were not assumed when the local protocol or caller guards contradicted them. Review was confined to the named dossier; related defects were not expanded into new finding IDs.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
