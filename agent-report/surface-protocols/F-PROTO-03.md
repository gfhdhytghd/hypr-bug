# F-PROTO-03 Popup grab does not validate ownership

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Cross-client grab allowlisting is confirmed; passive keylogging and permanent grab retention are not. Input delivery still needs hit-testing and focus.

**Verdict: partially. Severity: Medium.**

The dossier alleges that an unrelated client's popup can join an existing popup grab and steal input. Cross-client membership is source-confirmed; unconditional keyboard theft and permanent grab ownership are overstated.

**Evidence — source-confirmed.** The popup `grab` callback at `src/protocols/XDGShell.cpp:79-82` ignores seat/serial semantics. When an owner is already present, `addOrStartGrab` has no client, parent-stack, or seat comparison:

```cpp
m_grabbed.emplace_back(popup);                    // :983
m_grab->add(popup->m_surface->m_surface.lock());   // :985
if (popup->m_parent)
    m_grab->add(popup->m_parent->m_surface.lock());// :987-988
```

A client can only name its own correctly typed objects through normal Wayland decoding. That does not prevent this issue: the protocol stores one global grab shared by all clients (`src/protocols/XDGShell.hpp:303-306`), so the added popup and parent are the requesting client's own objects. The code, rather than a forged resource ID, crosses the client-isolation boundary. `CSeatGrab::add` merely appends, and `accepts` only checks membership (`src/managers/SeatManager.cpp:878-884`).

**Evidence — source-confirmed.** Client grab request → shared grab list insertion → input hit-test → `src/managers/input/InputManager.cpp:561-578` no longer rejects that client's surface → normal pointer focus/motion (`:601-603,712-715`) and button delivery (`:910-911`). Keyboard focus additionally passes the grab membership check in `src/desktop/state/FocusState.cpp:238-241`; actual focus selection remains subject to window visibility, geometry, input regions, and focus policy (`InputManager.cpp:635-705`). The default `input:follow_mouse` is 1 (`src/config/values/ConfigValues.cpp:380`). A mapped, input-eligible attacker surface under the pointer can therefore receive events during another client's grab instead of being excluded by that grab. Joining alone does not immediately call `refocusGrab`, and events delivered to the victim are not copied to the attacker.

**Guards/corrections — source-confirmed.** First-grab installation checks the session lock (`SeatManager.cpp:755-758`); keyboard surface focus has a session-lock check too (`FocusState.cpp:235-236`). No lockscreen bypass is established. `XDGShell.cpp:991-998` ends the seat grab and clears grabbed entries when the owner is destroyed; the grab callback also dismisses/clears entries at `:928-933`. `m_grabOwner` is weak, so its failure to reset explicitly is a stale-bookkeeping concern, not a strong ownership leak or proof of perpetual input capture. The ignored seat still must decode as a live `wl_seat`; an arbitrary integer object ID is not sufficient.

**Severity.** Medium for cross-client grab isolation failure with hit-test/focus/user-interaction preconditions. No two-client input capture or keyboard observation was executed. This does not support passive keylogging of unrelated windows or a general privilege escalation.

**Fix direction.** Validate the input serial and seat, enforce a same-client valid popup-parent grab stack, and explicitly clear owner and membership on dismissal. Keep ordinary focus and session-lock guards.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
