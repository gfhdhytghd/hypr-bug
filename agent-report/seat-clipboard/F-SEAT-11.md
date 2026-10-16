# F-SEAT-11 光标 shm 像素数据拷贝无尺寸/格式校验；CPU 光标路径每帧原地翻转角色缓冲

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Seat / clipboard / drag and drop. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: C8/RGB565 explanation rejected; CPU cache mutation confirmed, with an unchecked padding tail in channel swapping.

**Verdict: partially. Revised severity: High (conditional CPU-path memory safety).**

**Claim.** Cursor SHM copies assume four bytes per pixel without validation, large cursors amplify resources, and the CPU cursor path mutates stored color channels each render.

**Evidence and guarded copy path — source-confirmed.** A cursor-role surface commit updates synchronous texture data and then cursor SHM (`src/protocols/core/Compositor.cpp:659-673`). Assigning the cursor role also calls this updater (`src/protocols/core/Seat.cpp:127-134`). `updateCursorShm` checks SHM availability, clips damage to buffer size, obtains `bufLen`, and resizes the cache (`Compositor.cpp:788-810`). Full damage copies `bufLen`; partial damage uses `4 * x` and `shmAttrs.stride` (`:815-824`).

The omitted caller-side checks matter: `src/protocols/core/Shm.cpp:144-156` accepts only advertised formats and validates the layout using `NFormatUtils::isShmBufferLayoutValid`. The advertised list contains only 32-bit formats (`:220-231`), not C8 or RGB565. Layout validation checks minimum stride, multiplication/addition bounds, and pool containment (`src/helpers/Format.cpp:53-80`). `beginDataPtr` returns `stride * height` (`Shm.cpp:65-66`). Therefore the dossier's non-32-bit SHM partial-copy overread is not reachable through these advertised core SHM buffers at this baseline. The CPU hardware path additionally rejects formats other than ARGB8888/ABGR8888 (`src/pointer/PointerManager.cpp:627-636`).

**Color mutation — source-confirmed.** With CPU cursor buffers selected, an empty texture data copy leads to the role-cache fallback (`PointerManager.cpp:617-623`). Ordinary synchronous surface textures are created without `keepDataCopy` (`src/protocols/types/SurfaceState.cpp:58-67`; default `false` in `src/render/Renderer.hpp:190`), so this fallback is meaningful. The exact order is:

```cpp
// PointerManager.cpp:640-649
texData = shmBuffer; // a vector copy, conditional on shmBuffer.data()
// ...
for (size_t i = 0; i < shmBuffer.size(); i += 4)
    std::swap(shmBuffer[i], shmBuffer[i + 2]);
```

The current output copy has **not** been converted by the subsequent swap. Instead the role cache is changed, so another render without refreshed SHM can alternate the channel ordering. This agrees with the existing tracker side-finding correction, independently rechecked at this baseline. `cursorPixelData` returns the role's compositor-owned vector (`Seat.cpp:623-627`; `Seat.hpp:31-42`), not a writable view of client SHM. The dossier's implied shared client/render buffer is incorrect.

**Cursor activation/render path — source-confirmed.** The current-focus guard is `src/managers/SeatManager.cpp:678-690` (F-SEAT-10). Its cursor event reaches `src/managers/input/InputManager.cpp:100,769-792`, with cursor-override policy checked at `:789-790,808-809`, then `src/render/Renderer.cpp:3074-3085` and `src/pointer/cursor/CursorManager.cpp:141-147` forward the selected surface. `src/pointer/PointerManager.cpp:258-302` updates backend state on initial selection and later surface commits. Backend update skips disabled/DPMS-off monitors and nonintersecting cursors, and respects software-cursor locks/policy (`:402-428`); an eligible hardware attempt reaches `renderHWCursorBuffer` (`:483-500`) and the CPU fallback above. These are real preconditions, not a claim that every connected client can force this path on every GPU configuration.

**Additional padding-bound correction — source-confirmed.** The advertised-format guard does not prove every byte in the cache is part of a whole pixel. `Shm.cpp:65-66` sizes it from stride times height, while `Format.cpp:64-80` checks minimum stride and total containment but imposes no four-byte stride divisibility. The ABGR swap loop (`PointerManager.cpp:647-650`) accesses `i + 2` under only `i < shmBuffer.size()`. Therefore an accepted padded layout can leave a tail that this loop indexes outside the vector. No guard at cursor-cache copy, synchronous texture upload (`src/render/gl/GLTexture.cpp:32-65`), or CPU fallback establishes the missing tail bound. This is a narrower memory-safety issue in the same reviewed CPU mutation path, not the dossier's C8/RGB565 partial-copy mechanism. It is source-confirmed; no crashing input, sanitizer reproducer, or compositor execution was constructed.

**Preconditions and limits.** CPU mode is selected by `cursor:use_cpu_buffer` and GPU policy (`PointerManager.cpp:549-551`); both CPU-buffer and hardware-cursor policies default to auto (`src/config/values/ConfigValues.cpp:684,707`), and hardware cursors must actually be enabled/usable; hardware-plane size checks run before copying (`:553-564`), so arbitrary oversized cursors do not necessarily reach that hardware path. The earlier role-cache resize has no cursor-specific budget and can use substantial memory for a valid large SHM buffer. Nevertheless size/format/pool validation exists, small damage normally copies damaged rectangles rather than proving full-image copying on every commit, and no measured CPU/OOM threshold is available. CPU mode, compatible format, usable plane and cache fallback are required for the color bug; it is not a universal every-frame software-cursor defect.

**Severity and minimal fix direction.** High conditional on the CPU hardware-cursor fallback for the source-confirmed out-of-vector access; no compositor crash or exploitation beyond the invalid access is claimed. Channel-order corruption alone is Low, and unquantified allocation pressure remains a hardening concern. Normalize into a checked, tightly packed destination using actual source stride and width, bound all channel accesses to complete pixels, and preserve the source role cache. Keep format/layout validation and plane-size fallback; consider explicit allocation budgets. No GPU, physical display, or large allocation was exercised.

[Full dossier-level verification report](_evidence/full-audit-09-seat-clipboard.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/09-seat-clipboard.md). These remain agent evidence, not human-confirmed material.
