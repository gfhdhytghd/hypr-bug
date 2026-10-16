# F-SEAT-13 wl_data_device_manager.get_data_device 完全不使用 seat 参数；数据面无需 seat 即可建立

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Seat is required by typed wire validation; ignored seat storage is single-seat routing, while privileged clipboard-manager delivery is intentional.

**Verdict: rejected. Revised severity: None; single-seat design.**

**Claim.** Ignoring the seat callback argument allows a client that never binds a seat to create a data device, and thereby bypass selection access checks.

**Source-confirmed guard.** `src/protocols/core/DataDevice.cpp:369-387`, `src/protocols/DataDeviceWlr.cpp:180-198`, and `src/protocols/ExtDataDevice.cpp:180-198` do not store or inspect the seat argument. But `/usr/share/wayland/wayland.xml:1101-1106` declares it as a **non-null object of interface wl_seat**. It is not an untyped integer that Hyprland must independently resolve. Libwayland decodes typed objects from the requesting connection's object map before invoking the handler, disallowing null, unknown, wrong-interface, or another connection's objects. The generated server declaration is installed at `/usr/include/wayland-server-protocol.h:3142-3151`.

**Executed metadata control.** The isolated `seat_schema.c` probe in the designated scratch directory linked installed libwayland-server 1.26.0 and checked the actual exported method table:

```text
wl_data_device_manager.get_data_device signature=no arg1=wl_data_device arg2=wl_seat
```

`n` is the new-ID slot and `o` a non-null object slot; the method table explicitly types the latter as `wl_seat`. This verifies installed dispatch metadata, not a Hyprland runtime request. A separate unnamed-socketpair dispatcher control was compiled, but `wl_client_create` failed with `Operation not permitted` in this execution environment before any request dispatch. Its log is retained; **no runtime success of that negative-control test is claimed**.

**Path and semantic corrections — source-confirmed.** Upstream provides one `CWLSeatProtocol` global (`src/managers/ProtocolManager.cpp:163`; `src/protocols/core/Seat.hpp:21` names the seat), and routes devices by client. `dataDeviceForClient` deterministically returns the first matching live entry (`DataDevice.cpp:429-438`); that may deserve separate multi-resource protocol-conformance review, but is not nondeterministic cross-client routing. Core device creation does not immediately deliver an old selection (`:369-387`), and normal offers remain keyboard-focus-routed (`:523-534`). Clipboard-manager protocols do send initial selections (`DataDeviceWlr.cpp:198`; `ExtDataDevice.cpp:198`), which is their intended function; both globals are hidden from security-context sandbox clients (`src/Compositor.cpp:267-271`; `src/managers/ProtocolManager.cpp:360-403`). For permitted unsandboxed managers, requiring a stored seat pointer would not itself add confidentiality authorization.

**Verdict, severity, and fix direction.** Rejected as an access-control/no-seat bypass; no demonstrated security impact from ignoring a valid sole-seat resource. The proposed `WL_DATA_DEVICE_MANAGER_ERROR_INVALID_SEAT` does not exist in the core protocol specification. Explicit seat association would be appropriate for future multi-seat support and broadcasting to multiple same-client device resources merits separate compatibility review, but neither validates this dossier's claimed exploit.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
