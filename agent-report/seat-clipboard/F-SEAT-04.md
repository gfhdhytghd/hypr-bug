# F-SEAT-04 wl_data_offer.receive 无一次性约束、无速率限制、core 路径未 accept 自动代接受 → 对 source 客户端的 fd/事件洪泛 DoS

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Repeated forwarding exists; repeat receives are legal, MIME validation exists, and victim exhaustion is unproven.

**Verdict: partially. Revised severity: Low; victim DoS unproven.**

**Claim.** Repeated receive requests and automatic acceptance cause descriptor/event flooding, allegedly violating one-receive semantics and allowing arbitrary MIME requests.

**Evidence — source-confirmed.** `src/protocols/core/DataDevice.cpp:44-65` checks source liveness and dead state, optionally notifies acceptance, and forwards a descriptor every time. `m_recvd` records transfer activity rather than enforcing a quota. Equivalent forwarding exists at `src/protocols/DataDeviceWlr.cpp:14-28`, `src/protocols/ExtDataDevice.cpp:14-28`, and `src/protocols/PrimarySelection.cpp:15-29`.

```cpp
// DataDevice.cpp:187-193
if (std::ranges::find(m_mimeTypes, mime) == m_mimeTypes.end()) {
    // log and return
    return;
}
m_resource->sendSend(mime.c_str(), fd.get());
```

**Guards and protocol correction — source-confirmed.** Source implementations validate MIME membership before emitting `send` (`DataDevice.cpp:175-193`; `DataDeviceWlr.cpp:80-86`; `PrimarySelection.cpp:81-87`; `ExtDataDevice.cpp:80-86`). The compositor wraps the received fd in `CFileDescriptor`, so the local receive-handler descriptor is not simply leaked. `/usr/share/wayland/wayland.xml:626-629` explicitly permits multiple receives and preemptive examination before acceptance/drop. The WLR and ext data-control XML also permit multiple receives (`protocols/wlr-data-control-unstable-v1.xml:246-257`; installed `ext-data-control-v1.xml:244-255`). A blanket one-receive limit would break legitimate behavior. Automatic source acceptance is real, but lack of prior acceptance alone is not proof of an invalid receive.

**Path and limits — source-confirmed.** A client already holding a live offer can request an advertised MIME repeatedly; each request reaches the underlying source's send event. Ordinary core offers are focus-routed and invalidated on keyboard focus changes (`DataDevice.cpp:537-544`). F-SEAT-01 replacement does not itself grant a background client an offer for the old selection. Clipboard-manager protocols deliberately receive selections but have security-context visibility restrictions (F-SEAT-13). The exact effect on a victim depends on libwayland queue backpressure and the source application's descriptor handling; no victim process or transport saturation was executed. Default descriptor limits, guaranteed source crash, and unbounded compositor fd growth are not established.

**Severity and minimal fix direction.** Low for the residual cross-client request-amplification/hardening concern; the dossier's Medium victim-exhaustion endpoint remains unproven. If measurements in an isolated test environment establish harmful amplification, bound outstanding transfers and enforce fair per-client service while preserving legal multiple-MIME transfers. Keep MIME validation and descriptor ownership intact.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
