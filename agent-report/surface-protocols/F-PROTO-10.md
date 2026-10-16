# F-PROTO-10 Resource type and layer-popup null checks depend on external guards

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Typed nonnullable arguments and resource lifetime prevent the alleged confusion/null input; libwayland older than 1.21 is outside supported dependencies.

**Verdict: rejected. Severity: None (hardening only).**

The dossier treats unchecked `fromResource` casts and layer-popup dereference as an exploitable type/null confusion, primarily on an old or modified libwayland. No such trigger is established on supported upstream dependencies; local defensive checks would still be reasonable hardening.

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:120-122,369-371,655-657,715-717` and `src/protocols/core/Compositor.cpp:326-328` rely on generated binding user data and assume the incoming resource's interface. The layer handler at `src/protocols/LayerShell.cpp:120-129` indeed dereferences the returned popup without an additional null branch. Those observations are accurate, but do not establish attacker control over the C++ pointer type.

The relevant signatures are typed: pinned `protocols/wlr-layer-shell-unstable-v1.xml:287-297` declares a nonnullable `object interface="xdg_popup"`; installed `xdg-shell.xml:506-508` declares popup new-ID, nullable typed XDG parent, and nonnullable typed positioner. Header documentation `/usr/include/wayland-util.h:102-144` describes type/nullability metadata used by the demarshaler. Core `wl_buffer`/XDG-toplevel IDs do not satisfy an `xdg_popup` argument. IDs are resolved in the sending client's resource namespace, not as arbitrary cross-client pointers. New-ID handling creates a server resource and is not a client-supplied C++ object pointer.

**Generated-binding corroboration — executed generation, source-confirmed inspection.** The installed hyprwayland-scanner 0.4.6 generated bindings into the designated scratch directory only. Its `wlr-layer-shell-unstable-v1.cpp:262-264,286` retains the popup interface and nonnullable `"o"` signature; `xdg-shell.cpp:460-463,481` retains typed `"n?oo"`. This is a local dependency-generation check, not a pinned upstream generated-file citation. CMake uses the scanner at `CMakeLists.txt:498-513` and requires version 0.3.10 or later (`:294`). The installed libwayland 1.26.0 binary also contains diagnostics for wrong object type, nonnullable null objects, and invalid object IDs.

**Lifetime countercheck — source-confirmed.** Popup role resources are strongly owned by the protocol (`src/protocols/XDGShell.hpp:301`). Both destroy handlers remove that role (`XDGShell.cpp:54-66,966-967`); there is no independent code path here that makes a still-live role resource's C++ object null while keeping its wire ID dispatchable. Locally generated `xdg-shell.cpp:987-1025` installs the wrapper user data, clears/destroys its resource on wrapper destruction, and clears its resource pointer during resource destruction. A subsequent request naming a destroyed ID is rejected by resource lookup. Destroying just the content XDG surface leaves a valid popup role object with an invalid *content* reference: that is F-PROTO-02, not a null `fromResource(popup_)` result.

**Dependency boundary.** Pinned `CMakeLists.txt:265,274` requires `wayland-server >= 1.22.91`; the dossier's `< 1.21` premise is outside upstream's supported build. No untyped object argument on these reported paths was identified. Replacing the protocol library with one that omits its validation is a changed trust/dependency assumption, not evidence against this baseline. `wl_resource` is opaque in the public server headers; checking a hypothetical public `res->interface` field is not an appropriate suggested patch. Prefer `wl_resource_instance_of` or equivalent supported checks.

**Execution limitation.** The synthetic decoder control could not create even a toy `wl_client`: `wl_client_create` returned `EPERM` in this environment before dispatch. Therefore wrong-type/null rejection was **not runtime-tested** here. The saved harness/log explicitly record this limitation; zero executed callbacks is not treated as a passing guard test. The initial assertion-based attempt also stopped at client creation, and an optional `strace` diagnostic was unavailable. No permission changes or desktop access were attempted.

**Severity/fix direction.** None for the claimed upstream exploit; optional hardening is to validate user data/interface and handle null at adapter boundaries. A distinct, supported-runtime route producing a correctly typed live popup resource without its protocol object would be needed to reopen this claim.

## Unnumbered controls

### Move/resize validation — confirmed; Informational

**Evidence — source-confirmed.** `src/protocols/XDGShell.cpp:17-31` enumerates permitted resize edges, and `:213-217` reports an invalid-resize-edge error before continuing. Move and resize both check decoded seat ownership against the requesting client (`:193-205,219-230`) and call `CSeatManager::pointerButtonSerialValid`. That function rejects absent seat/surface/zero serial, searches the seat container's recorded pointer-button serials, and validates the recorded surface's tree (`src/managers/SeatManager.cpp:185-204`). The client request therefore cannot reach the move/resize event emission solely by inventing an arbitrary serial or foreign seat. No failure or fix is established on these paths. This control does not apply to popup `grab`, whose callback omits analogous checks (F-PROTO-03).

### Positioner dimensions and version gates — confirmed; Informational

**Evidence — source-confirmed.** Positioner size and anchor-rectangle requests reject nonpositive dimensions at `src/protocols/XDGShell.cpp:685-700`:

```cpp
if UNLIKELY (x <= 0 || y <= 0) { /* error; return; */ } // :686
if UNLIKELY (w <= 0 || h <= 0) { /* error; return; */ } // :695
```

The WM capabilities event is gated on toplevel version 5 (`:165-173`), and adding SUSPENDED state requires version 6 (`:452-464`). These guards break the particular invalid-dimension/older-version paths listed by the dossier. No change is indicated by this control; it does not assert that every positioner enum/state invariant is validated.

### Buffer scale and damage arithmetic — confirmed; Informational

**Evidence — source-confirmed.** Core surface `set_buffer_scale` rejects `scale <= 0` (`src/protocols/core/Compositor.cpp:226-239`). Both surface- and buffer-damage requests call `addSafeDamage` (`:217-223`), which rejects nonpositive dimensions, widens before adding endpoints, clamps the far endpoints to `INT32_MAX`, and rejects empty results (`:25-36`):

```cpp
const int64_t x2 = std::min<int64_t>(sc<int64_t>(x) + w, INT32_MAX); // :29
```

Thus the cited damage endpoint path does not perform the claimed kind of unchecked `int32_t` addition, and a nonpositive scale does not reach later division/modulo. This does not validate independent viewporter/region coordinate conversions outside the control's scope. No fix is indicated for these guarded operations.

### Subsurface ancestry — partially; Low functional defect, unproven security escalation

**Evidence — source-confirmed.** `src/protocols/core/Subcompositor.cpp:160-190` checks live surface/parent, direct self-parenting, unassigned child role, and top-level-parent equality. Both `posRelativeToParent` and `t1Parent` have visited vectors (`:119-150`). However, both loop bodies select the role from the fixed initial parent rather than the advancing cursor:

```cpp
auto subsurface = sc<CSubsurfaceRole*>(m_parent->m_role.get())->m_subsurface.lock(); // :130 and :147
```

The condition tests `surf->m_role` (`:128,145`), while the body reads `m_parent->m_role`. On a normal sufficiently deep acyclic subsurface chain, advancing beyond the immediate parent causes the body to revisit that same parent's parent; the visited check eventually terminates with an intermediate ancestor instead of the actual top-level surface. The position traversal can likewise add the same offset more than once. This is a source-confirmed functional bug on valid nested client surfaces, not evidence that the visited lists correctly traverse the whole graph. `t1Parent` participates in parent admission (`:182-188`) and core commit bookkeeping (`src/protocols/core/Compositor.cpp:175-179`); offset traversal participates in surface-tree enumeration (`Compositor.cpp:455-463`).

**Boundary.** The dossier expressly left a cycle-check bypass unconstructed. No end-to-end bypass or compositor hang is promoted here, and this protocol-subsurface graph does not prove a `CPopup` parent cycle. Minimal fix: use the advancing `surf` role, validate locked parent/subsurface pointers at each hop, preserve visited detection, and add a bounded valid multilevel-ancestry regression test. No nested-surface client was run. The rating concerns wrong ancestry/geometry; any security escalation needs separate evidence.

### Deferred configure lifetime — confirmed; Informational

**Evidence — source-confirmed.** Toplevel state application and XDG configure scheduling use `doLaterLock` (`src/protocols/XDGShell.cpp:471-488,660-668`), owned by `UP<SEventLoopDoLaterLock>` members (`src/protocols/XDGShell.hpp:170,227`). Their captured `this` therefore has an associated cancellation owner. The lock's destructor calls `removeDoLater(seq)` (`src/managers/eventLoop/EventLoopManager.cpp:25-27`), which erases the queued entry and removes an empty idle source (`:240-250`). Resource destruction destroys the unique lock. A deferred callback surviving its resource is not established from these cited paths. No fix is indicated by this control; unrelated callbacks are outside this conclusion.

### Foreign-toplevel request inventory and lifetime — partially; Informational

**Evidence — source-confirmed.** `src/protocols/ForeignToplevel.cpp:5-33` registers handle destroy and manager destroy/stop. WLR foreign-toplevel registers activation (`src/protocols/ForeignToplevelWlr.cpp:23-33`), fullscreen/unfullscreen (`:35-65`), maximize/unmaximize (`:67-83`), minimize/unminimize (`:85-109`), and close (`:111-118`), as well as destroy (`:20-21`). This contradicts the dossier's statement that destroy/fullscreen/unfullscreen are the only client requests.

The state-changing WLR callbacks first lock/check `m_window` (`:24-27,36-39,59-62,68-71,77-80,86-92,99-105,112-115`). Fullscreen additionally handles nullable output, validates the resolved output resource, and checks monitor expiry (`:41-50`). For these cited request paths, a destroyed window stops processing rather than dereferencing a missing window. The WLR protocol intentionally exposes controls over other advertised windows to clients allowed to bind its global; that design alone is not a new isolation defect. Ordinary unsandboxed clients can see privileged globals, whereas security-context sandboxed clients are filtered (`src/Compositor.cpp:267-271`, `src/managers/ProtocolManager.cpp:348-403`), and foreign-toplevel/layer-shell are absent from the unprivileged whitelist.

No exploitable failure is established by this control, and no fix is indicated beyond correcting the request inventory in the dossier. This is a targeted verification of the cited handlers, not a blanket proof that all foreign-toplevel code is safe.

## Verification artifacts and limits

Scratch directory: `/home/wilf/data/.claude-scratch/agent-reports/work/06-protocols-shell/`.

- `dependency_controls.c` and `dependency_controls.log`: bounded pixman canonicalization controls succeeded; synthetic libwayland decoder setup was unavailable with `EPERM`, before any handler dispatch.
- `region_controls.cpp` and `region_controls.log`: actual installed hyprutils `CRegion` canonicalization controls succeeded with the same one/128 rectangle counts.
- `xdg-shell.cpp/.hpp` and `wlr-layer-shell-unstable-v1.cpp/.hpp`: generated with installed hyprwayland-scanner 0.4.6; inspection confirms preservation of XML object-interface/nullability metadata and wrapper destruction semantics for that generator.
- Installed versions checked: wayland-server 1.26.0, pixman 0.46.4, hyprutils 0.14.2. These describe inspection dependencies, not a running desktop's loaded libraries.

The upstream HEAD was verified as the exact baseline. No complete Hyprland build, compositor instance, Wayland desktop connection, input capture, GPU/fence test, or visual acceptance test was performed. High-impact conclusions are labeled source-confirmed and must not be represented as runtime reproductions. No repository source, README, dossier, other report, or Git state was changed by this verification.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
