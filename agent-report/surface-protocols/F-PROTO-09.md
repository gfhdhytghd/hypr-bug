# F-PROTO-09 Weak configure-ack serial validation

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unsent and repeated serials are accepted; numeric ordering poisons later acknowledgments and mishandles serial wraparound.

**Verdict: confirmed. Severity: Low.**

The XDG surface accepts acknowledgments based only on unsigned numeric ordering, rather than outstanding configure identity. Upstream's additional window acknowledgment tracker also uses numeric ordering, so it does not repair this defect.

**Evidence — source-confirmed.** The client request reaches `src/protocols/XDGShell.cpp:630-635`:

```cpp
if (serial < m_lastConfigureSerial)
    return;
m_lastConfigureSerial = serial;
m_events.ack.emit(serial);
```

`m_lastConfigureSerial` starts at zero (`src/protocols/XDGShell.hpp:229`), is independent of the scheduled/sent serial (`XDGShell.cpp:660-673`), and accepts equality repeatedly. There is no membership test against sent configurations. Subsequent numerically smaller acknowledgments are silently ignored, including valid serials after wraparound. This is an unsigned comparison defect; the dossier's reference to unsigned subtraction is inaccurate because no subtraction occurs here.

**Evidence — source-confirmed.** The emitted event reaches `src/desktop/view/window/WaylandBackend.cpp:162,442-447`. The tracker chooses the latest queued entry with `configure.first <= serial` (`src/desktop/view/window/WindowBackend.cpp:19-27`), consumes entries through it, and returns its size. `WaylandBackend.cpp:318-324` places that size in pending core-surface state, and `src/protocols/types/SurfaceState.cpp:232-233` applies it on commit. Thus an unsent sufficiently large serial can consume existing pending window configurations. It cannot inject an arbitrary attacker-selected size: only a size already present in the pending tracker is returned. An acknowledgment before any tracked configure has no returned size, but still updates the XDG surface's numeric threshold.

**Protocol countercheck — source-confirmed.** Installed `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml:571-574,584-593` requires an `invalid_serial` error for never-sent, reused, or previously consumed serials. `wl_display_next_serial` at `XDGShell.cpp:666` supplies display-wide serials; they are not a monotone, unbounded per-window counter.

**Reachability/severity.** Any connected XDG client can send an arbitrary `uint32_t` acknowledgment on its own object; no special permissions or input serial is involved. Low: protocol/state negotiation corruption of that client's own surfaces, not a source-demonstrated cross-client compromise or memory-safety failure. Mapping/buffer commits are needed to observe the downstream visual scaling effects cited by the dossier (`src/desktop/view/WLSurface.cpp:43-77`); those visual effects were not tested.

**Correction/fix direction.** The tracker path and `SurfaceState` lines have changed from the dossier. Validate exact outstanding XDG serials, consume the acknowledged prefix by issuance order, and have the window tracker use the same identity/order rather than raw unsigned `<=`. Treat stale/repeated/unknown serials as the specified protocol error.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
