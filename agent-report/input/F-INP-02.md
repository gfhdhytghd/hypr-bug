# F-INP-02 Release callbacks bypass keybind availability checks

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Armed release callbacks bypass lock checks; drag release stops dragging, rather than starting locked-state movement. Other callback effects depend on configuration.

**Verdict: confirmed. Revised severity: Medium (configuration-dependent execution across a lock/activity boundary).**

Previously armed follow-up callbacks can execute without the availability checks used for new bindings. The dossier correctly identifies that policy gap, but its stock move/resize example does not establish movement while locked, and ordinary execution-only bindings do not automatically acquire release callbacks.

**Evidence — source-confirmed.** A successful press can arm the original callback through `src/keybinds/Manager.cpp:606–613`. The Lua wrapper maps a returned `request_release` field into this follow-up (`src/config/lua/bindings/LuaBindingsToplevel.cpp:235–240`; `src/config/lua/ConfigManager.cpp:1396–1398`). Built-in stateful actions can also request a follow-up using `LuaBindingsDispatchers.cpp:35–43`, e.g. pass/global/drag/resize at `:226`, `:251`, `:665`, and `:673`.

New ordinary binding candidates are checked at `Manager.cpp:470` against `canInvokeNow`, whose `:900–906` checks include enabled/registered/shadowed state, input capture, session lock, and inhibition. Three release paths differ:

- Device removal: `src/managers/input/InputManager.cpp:1525` calls `onDeviceRemoved`; `Manager.cpp:440–443` extracts that device's follow-ups and invokes them before clearing its pressed state.
- Ordinary release: `Manager.cpp:462–466` inserts extracted follow-ups into `hits`, and `:551` explicitly exempts them from the availability check.
- An allowed keyboard event while the backend session is inactive: `Manager.cpp:190–195` extracts all follow-ups, invokes them, and clears state.

The direct drain contains no availability check (`Manager.cpp:624–638`):

```cpp
invokeBind(release.bind, false,
    m_inputState.find(release.key, release.device.lock()));
```

`invokeBind` itself (`:587–621`) directly invokes the same configured callback and sets `m_passPressed` to 0 for that invocation. Lock entry in `src/managers/SessionLockManager.cpp:52–132` changes lock/focus/grab state but does not discard armed callbacks. The subscribed compositor lock callback (`src/Compositor.cpp:430–440`) does not do so either.

**Trigger boundary and prerequisites — source-confirmed.** A configured callback must have run while available and requested a release follow-up; the associated press must remain recorded across lock or session deactivation. A later physical release or device removal reaches that pre-existing callback. An unrestricted virtual-input client has the F-INP-01 access restrictions; device destruction need not satisfy keyboard focus. This cannot select an arbitrary unarmed binding or provide new Lua code. Physical unplug is an alternative trigger. An inactive-session drain occurs on a subsequent accepted keyboard event, not merely because the VT changed.

**Corrections and impact — source-confirmed.** Stock mouse drag/resize releases call `src/config/shared/actions/ConfigActions.cpp:1683–1689`:

```cpp
if (!PRESSED) {
    if (g_layoutManager->dragController()->target())
        g_layoutManager->endDragTarget();
    return {};
}
```

That is drag cleanup, not a new move operation. Likewise global/pass actions use the release phase (`ConfigActions.cpp:1528–1537`, `:1678`); cleanup of held external state may be intentional. The defect is replaying the **entire arbitrary configured callback**, whose unrelated side effects need not be release-safe. An execution-only binding does not request a follow-up by itself; a compound/custom callback must do so. Therefore the source proves boundary-crossing callback execution, not a universal locked-state command primitive or unlock. Severity is Medium because impact depends on previously armed configuration and timing. No locked desktop execution was attempted.

**Fix direction.** Separate release cleanup from replay of general Lua callbacks. On lock/activity transitions, cancel unsafe follow-ups while delivering the narrowly required drag/key/global-shortcut cleanup. Blindly dropping every release or blindly applying every availability condition can itself leave state stuck; define cleanup semantics explicitly.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
