# F-RES-10 Unreachable ICC upload path retains file descriptors

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Descriptor leaks hold and debug mode makes them reachable; default rejection and absence of client ICC parsing still hold.

**Verdict: partially. Severity: Medium when the nondefault full-CM debug option is enabled; no default ICC-upload exposure.**

The raw ICC descriptor has no cleanup owner. Contrary to the dossier's unconditional dead-code statement, upstream can instantiate this creator under a configuration-controlled debug mode. Client ICC bytes still do not reach lcms.

**Source-confirmed evidence and gate:** `src/config/values/ConfigValues.cpp:738` defines `debug:full_cm_proto` as false by default and requiring restart. `src/managers/ProtocolManager.cpp:134-135,234-235` passes it to the color-management protocol; `src/protocols/ColorManagement.cpp:872-874` stores it. `ColorManagement.cpp:136-153` rejects creator creation **only when `m_debug` is false**:

```cpp
if (!PROTO::colorManagement->m_debug) {
    r->error(WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
             "ICC profiles are not supported");
    return;
}
```

When enabled, the same callback constructs `CColorManagementIccCreator`. The protocol is enabled by default via `render:cm_enabled` (`ConfigValues.cpp:657`) and allowed by the sandbox protocol whitelist (`ProtocolManager.cpp:399`). The client cannot enable the startup debug flag through an ordinary color-management request; local configuration/launch authority must first open this gate.

`ColorManagement.cpp:523-527` stores the received integer fd with no close or already-set test. `src/protocols/ColorManagement.hpp:102-124` stores it as `int fd = -1`, not `CFileDescriptor`, and declares no custom destructor. Both destruction (`ColorManagement.cpp:487,925-926`) and successful creator completion (`520`) erase the object without closing it. Libwayland's successful fd delivery transfers a descriptor to the implementation; a raw integer member neither closes it nor closes previously overwritten descriptors. Contrast the explicit received-fd ownership at `src/protocols/core/Shm.cpp:186-187`.

**Source-confirmed chain/preconditions:** with color management and the startup debug option enabled, an ordinary connected client can obtain this correctly typed creator; its ICC-file callback then retains received descriptors without cleanup. Repeated setting is also not rejected by the implementation. The protocol's semantic `already_set` requirement (`/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml:1013-1014`) is not an automatic libwayland argument check. Even a single valid set followed by creator destruction lacks cleanup. This is descriptor retention/exhaustion in a nondefault mode, not a demonstrated default-session crash. No descriptor flood was executed.

**Source-confirmed ICC parsing boundary:** `ColorManagement.cpp:489-518` only checks a stored fd/length and logs `FIXME: Parse icc`; it does not map/read the fd or call lcms. Native local-file parsing instead follows monitor-rule `parseICC()` (`src/config/shared/monitor/Parser.cpp:277-283`) → `src/output/Monitor.cpp:653` → `src/helpers/cm/ICC.cpp:277-295`. `ICC.cpp:17-33` reads that configuration-selected file without an application size cap, then `287` opens it with lcms. This is a configuration-selected file boundary, not client ICC upload. There is no justification to infer a current lcms exploit from historical CVEs.

**Corrections:** the dossier's line numbers reflect the fork; the default rejection is conditional upstream, so “unreachable” and “only leaks after future enablement” are wrong. Its suggested 10 MB/display-only parser policy does not match the installed protocol's 32 MB and Display-or-ColorSpace requirements (`color-management-v1.xml:987-1008`). Its broad claim that VCGT boundaries are all self-consistent should not be carried forward: `ICC.cpp:80-81` permits a one-entry table, while `129-131` unconditionally indexes entries 1 and `entries - 2` in logging arguments. This is a source-level counterexample to that side assurance, not a newly expanded client-upload finding.

**Minimal fix direction:** give received descriptors scoped ownership from callback entry, reject duplicate sets while safely disposing of the newly received fd, and close the retained owner on every lifecycle path. Keep the unsupported/default gate until parsing and protocol validation are implemented. Bound local file reads independently and review VCGT logging's short-table accesses. Do not add client-to-lcms claims to this record.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
