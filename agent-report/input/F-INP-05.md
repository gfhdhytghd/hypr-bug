# F-INP-05 Shared keyboard pressed-key state can remain stuck

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Held shared keys suppress physical presses/releases while the device persists; removal leaves a recoverable stale key, not permanent keyboard loss.

**Verdict: partially. Revised severity: Medium (persistent-device input denial; recoverable residue after removal).**

A shared keyboard holding a key can suppress subsequent physical releases and duplicate presses in seat delivery. The dossier's permanent-disability claim conflates a still-held live device with stale aggregate state after that device is removed.

**Evidence — source-confirmed.** The virtual wrapper updates its per-keyboard pressed state before forwarding each event (`src/devices/VirtualKeyboard.cpp:24–26`); `src/devices/IKeyboard.cpp:450–467` inserts/removes the code. The default sharing policy enables non-IME virtual keyboards (`src/config/values/ConfigValues.cpp:436`; `VirtualKeyboard.cpp:49–51`), with the IME exclusion in `src/managers/input/InputManager.cpp:1802–1816`.

After binding processing and keyboard-event-handler dispatch (`InputManager.cpp:1699–1706`), merged delivery checks `shareKeyFromAllKBs` at `:1713–1724`. A release is dropped if another enabled, sharing, non-ignored keyboard still reports that code down (`:1927–1948`). The seat-output aggregate then drops duplicate presses (`:1731–1736`):

```cpp
if (CONTAINS && pressed)
    return;
if (!CONTAINS && !pressed)
    return;
```

Only a forwarded release removes that code from `CInputManager::m_pressed` (`:1738–1741`). Keyboard removal (`:1522–1545`) removes the device and its keybind/event-handler state but does not reconcile this seat-output aggregate. The protocol's optional teardown release is off by default (`ConfigValues.cpp:438`; `src/protocols/VirtualKeyboard.cpp:137–142`). `CKeyboardEventHandlerStack::onKeyboardRemoved` only erases handler-owned keys (`src/managers/SeatManager.cpp:82–83`), not the aggregate.

**Reachability and bounded state analysis — source-confirmed, not executed against a compositor.** An eligible unrestricted virtual keyboard, default sharing, an enabled/allowed device, and a key that reaches normal seat delivery are sufficient. The simplest case has no IME grab, input capture, consuming binding, or active keyboard handler. The first forwarded press inserts the code. While another live device reports it down, physical release does not remove it, and later physical presses are duplicates. Applications may also repeat that initial held key; this is not necessarily silent loss of every character.

Once the holding keyboard is removed from `m_keyboards`, it is no longer visited by the sharing loop. If no remaining device holds that code, the next physical press can be dropped because the aggregate is stale, but its subsequent release removes the residue. The following press can work normally. The same reasoning applies to a held physical keyboard being unplugged. Thus stale state is a real cleanup defect and can cost an initial press/release cycle after removal, but it is not an indefinitely unrecoverable key after the source disappears.

Keyboard focus transitions obtain their pressed array from that same aggregate (`src/managers/SeatManager.cpp:272`), so simply switching focus is not guaranteed to repair the state. Binding processing happens before seat deduplication (`InputManager.cpp:1699–1700`); loss of normal text input does not prove all bindings stop working. During session lock, ordinary bindings are gated but merged seat delivery still exists, so availability interference with lockscreen text entry is plausible from the same source path. No lockscreen/password interaction was performed.

**Severity and corrections.** Medium reflects a live local virtual-input source disrupting session input, without a demonstrated sandbox bypass or permanent lockout. The prerequisite global-access restrictions in F-INP-01 apply. The dossier's test wording itself says destroying the connection restores input, contradicting its stronger permanent claim. The protocol's own `m_pressed` list and the wrapper's list are distinct; removing the wrapper eliminates its contribution to the merge even if the aggregate still needs repair.

**Fix direction.** Track per-device contributions to forwarded seat state and synthesize a release on removal only when no remaining device owns that key. Apply the trust policy consistently to shared key/modifier state. Merely clearing every key on any removal would incorrectly release keys still physically held elsewhere.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
