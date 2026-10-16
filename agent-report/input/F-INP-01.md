# F-INP-01 Virtual keyboard events can reach execution keybinds

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Input devices / keybinds. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unrestricted clients reach configured binds; security-context sandbox clients are filtered. Arbitrary command execution and universal sandbox escape are not established.

**Verdict: partially. Revised severity: Medium (input authorization/policy exposure for unrestricted clients).**

The dossier claims every Wayland client can use a virtual keyboard to run arbitrary commands through user bindings, including a Wayland sandbox escape. Reaching existing configured bindings is source-confirmed for unrestricted connections; the universal client, sandbox-escape, and arbitrary-command conclusions do not follow.

**Evidence — source-confirmed.** `src/Compositor.cpp:267–271` supplies the guard omitted by the dossier:

```cpp
if (!PROTO::securityContext->isClientSandboxed(client))
    return true;
return !g_pProtocolManager || !g_pProtocolManager->isGlobalPrivileged(global);
```

`src/Compositor.cpp:288` installs this global filter. The whitelist in `src/managers/ProtocolManager.cpp:360–403` excludes both virtual-input managers. Thus security-context clients cannot access those globals through the normal registry/bind path. `src/protocols/SecurityContext.cpp:152–164` records clients accepted through the security-context listener. This protection is specific to that integration; an application sandbox given an unrestricted compositor connection is not automatically classified this way.

For an unrestricted connection, virtual-keyboard creation reaches the device setup in `src/protocols/VirtualKeyboard.cpp:164–176` and `src/managers/input/InputManager.cpp:1109–1112`. `InputManager.cpp:1215–1226` enables keybinds by default and consults the keyboard permission rule. `src/managers/permissions/DynamicPermissionManager.cpp:194–224` honors matching allow/deny/ask rules before falling back to keyboard ALLOW. This is a default policy, not an absence of all permission handling.

`InputManager.cpp:1681` rejects disabled/disallowed keyboards. `:1684–1700` excludes the grabbing IME's own virtual keyboard from binding actions, then otherwise routes input to the keybind manager. `src/keybinds/Manager.cpp:198–199` checks `m_allowBinds`; `:470` and `:551` apply `canInvokeNow` to ordinary candidates. `:904–905` blocks ordinary bindings during session lock. Device selectors, submaps, and modifier matching also apply (`src/keybinds/Bind.cpp:162–167`, `:222–238`). Accepted callbacks reach `Manager.cpp:606` and `Bind.cpp:241–242`.

Configured execution actions use a captured configuration string (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:180–205`) and the executor ultimately launches that string (`src/config/supplementary/executor/Executor.cpp:173–205`). The virtual-keyboard request supplies input events, not the execution string. A binding that launches a fixed program proves only execution of that program. Additional application interaction or a configuration-dependent callback would be required to establish attacker-chosen command execution; neither is demonstrated here.

**Reachability and corrections — source-confirmed.** An unrestricted session client, a usable virtual keyboard, no deny rule, an enabled device with binds allowed, and a matching available binding are required. `input:virtualkeyboard:share_states=2` is the default (`src/config/values/ConfigValues.cpp:436`), but disabling shared state alone does not establish device trust or disable all bindings. The default `misc:name_vk_after_proc` is **true**, not a universal common name (`ConfigValues.cpp:571`). `InputManager.cpp:2157–2169` also disambiguates simultaneous HID names. Permissions remain keyed by the resulting device name (`:1226`), not authenticated per-connection trust; this is a hardening concern, not proof that every device shares one approval.

The VT subclaim is narrower and real: internal key handling precedes ordinary candidate gating (`Manager.cpp:215–216`), and `:868–885` can ask an available backend session to switch VT without a lock or virtual-device check. It does not unlock the existing session; a backend session and a different VT are required.

**Fix direction.** Define a deliberate trust policy for virtual input on unrestricted connections, preserve the existing security-context filter, and gate binding/VT privileges separately where appropriate. Bind permissions to authenticated identity rather than treating a display name as identity. Do not label this Critical RCE or a demonstrated sandbox escape.

[Full dossier-level verification report](_evidence/full-audit-13-input-keybinds.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/13-input-keybinds.md). These remain agent evidence, not human-confirmed material.
