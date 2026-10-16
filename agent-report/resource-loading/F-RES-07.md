# F-RES-07 Shared-memory image backing can produce SIGBUS

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`, plus empirical reproduction in the nested harness; current-version and physical-display acceptance must be established separately.

- Empirical: Empirically reproduced: nested compositor SIGBUS (status 135) in the cursor-role SHM read path after the backing file was truncated before flush.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Mutable SHM backing is read without fault containment; initial size/layout checks do not prevent later SIGBUS. [EXECUTED nested SIGBUS]

**Empirical reproduction — executed.** Nested harness (2026-10-15): client took the cursor role (virtual-pointer enter + wl_pointer.set_cursor) over a wl_shm buffer whose backing file was ftruncated before the compositor flushed it — SIGBUS in updateCursorShm (`COMPOSITOR_EXITED status=135`).

**Verdict: confirmed. Severity: High, local Wayland-client compositor denial of service.**

Hyprland's custom shared-memory implementation validates initial file size and buffer layout but has no fault-contained access to backing storage whose size can subsequently change. The kernel can deliver SIGBUS during a later compositor read; no source-level guard converts that fault into a client error.

**Source-confirmed evidence:** `src/protocols/core/Shm.cpp:103-113,186-195` checks file size when accepting a pool; `125-135` repeats that check for a requested pool resize. `81,97` map the client's backing file with `MAP_SHARED`. These checks establish a point-in-time file length, not immutable backing. Pool resize rejects shrinking through the protocol, but that does not control changes made through another descriptor for the same file. The code has no shrink-seal requirement or equivalent immutable-copy guarantee.

```cpp
std::tuple<uint8_t*, uint32_t, size_t> CWLSHMBuffer::beginDataPtr(uint32_t flags) {
    return {sc<uint8_t*>(m_pool->m_data) + m_offset,
            m_fmt, sc<size_t>(m_stride) * sc<size_t>(size.y)};
}
```

`Shm.cpp:65-70` returns the mapped bytes directly and provides an empty `endDataPtr()`. `CWLSurfaceResource::updateCursorShm()` intersects damage with buffer bounds (`src/protocols/core/Compositor.cpp:805`), obtains those bytes (`808`), and copies from them (`815-824`). Bounds checking the **mapping** does not ensure its file-backed pages still exist. This direct CPU copy is sufficient to establish the affected read; GPU-driver behavior is not needed to justify it.

**Source-confirmed call and guard analysis:** client-provided shared-memory storage enters the pool/buffer constructors (`Shm.cpp:138-168,186-204`). The surface attach handler resolves a real `wl_buffer` and retains its buffer (`Compositor.cpp:97-129`); regular state processing goes through `scheduleState()` (`607-626`), `CSurfaceStateQueue::tryProcess()` (`src/protocols/types/SurfaceStateQueue.cpp:93-109`), and `commitState()` (`Compositor.cpp:659-673`). SHM is synchronous (`Shm.cpp:50-51`). Surface texture updates consume its data through `src/protocols/types/SurfaceState.cpp:58-69`; cursor-role commits additionally reach the explicit CPU-copy routine. The cursor role is assigned and its cache updated in `src/protocols/core/Seat.cpp:125-137`. Object-interface validation, buffer shared ownership, and pool lifetime prevent several wrong-object/dangling-object cases; they do not preserve the backing file's size.

The protocol is created as a core global (`src/managers/ProtocolManager.cpp:163-167`) and is explicitly allowed even by the sandbox-global filter (`348-365`; `src/Compositor.cpp:267-271`). No screen-capture permission, plugin, system asset write, or special build option is needed for the resource boundary. A client must have access to the compositor's Wayland socket and backing storage whose size remains mutable; this is a **local client**, not an unauthenticated network peer. Storage losing backing before a mapped page is consumed is the failure precondition. No request sequence or fault-triggering client was executed.

**Source-confirmed containment review:** searches of upstream source found no SIGBUS fence or `wl_shm_buffer_begin_access()`/`end_access()` use around these custom mappings. Signal setup handles SIGSEGV/SIGABRT (`src/Compositor.cpp:292-299`), not SIGBUS. Libwayland's API for its own `wl_shm_buffer` objects (`/usr/include/wayland-server-core.h:662-672`) does not automatically protect a separately implemented `mmap` and `memcpy`. Existing mappings stay referenced through `m_pool`; lifetime retention is not fault recovery.

**Corrections:** the dossier's `bufLen` side note is wrong: after returning `data + offset`, the length of this buffer is still `stride * height`, **not** that quantity minus offset. The independent layout check includes offset when comparing against pool size (`src/helpers/Format.cpp:71-80`). Its protection is spatial, not temporal. The claim that this is the only possible client-triggered resource fault in the whole compositor is outside this dossier's proof.

**Minimal fix direction:** adopt fault-contained shared-memory reads consistently across all CPU and upload entry points, with client-local failure handling. Prefer a proven existing SHM-access implementation where integration permits. A point-in-time `fstat()` is insufficient. Requiring seals can be an additional Linux-specific policy but would reject otherwise valid ordinary SHM clients; requiring `F_SEAL_GROW` would also conflict with legitimate pool growth. Do not prescribe mandatory seals as a drop-in protocol-compatible fix.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
