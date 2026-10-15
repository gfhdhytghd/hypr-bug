# Side findings from fixing the fork — 2026-10-15

## Scope and result

While fixing the fork's color-management findings and HDR-COM-01 on `gfhdhytghd/Hyprland`, six further upstream defects came up that are **not in any existing list**, plus one correction to an existing entry (F-SEAT-11). Each one was then checked against official upstream source.

| Baseline | Revision |
|---|---|
| Official Hyprland `main` | `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` |
| Fork `main` with the fixes | `bd598253a` |

**Verification levels used below**

- **Executed:** a gtest using the fork's Wayland protocol fixture (`tests/protocols/ColorManagementProtocol.cpp`) was compiled against upstream `src/protocols/core/Subcompositor.cpp` taken verbatim from `e2ad066c9`. It sends real protocol requests through libwayland-server. The probe tests were temporary and are not committed to the fork.
- **Source-confirmed:** the full call chain was read at `e2ad066c9`. It has not been run.

No live session, physical display, or NVIDIA cursor plane was used. The SIDE-01 client in [`repro/subsurface-orphan-parent`](../repro/subsurface-orphan-parent/) was compiled but deliberately **not run** against the live compositor.

| ID | Severity | Finding | Verification | Fork |
|---|---|---|---|---|
| [SIDE-01](#side-01) | High | `get_subsurface` on an orphaned subsurface parent → null dereference in `t1Parent()` → compositor SIGSEGV | Executed (SIGSEGV) | fixed `ada0711d4` |
| [SIDE-02](#side-02) | Medium | `t1Parent()` returns the wrong ancestor for nesting depth ≥ 3 | Executed | fixed `ada0711d4` |
| [SIDE-03](#side-03) | Medium | `posRelativeToParent()` mis-positions subsurfaces at nesting depth ≥ 3 | Executed (21 vs 111) | fixed `475147aec` |
| [SIDE-04](#side-04) | Medium | Hardware cursor not re-rendered when the output image description changes | Source-confirmed | fixed `ca7335ac3`, `7f334993b` |
| [SIDE-05](#side-05) | Low | Cursor surface's own image description is ignored by the software and GPU hardware cursor | Source-confirmed | fixed `7f334993b` |
| [SIDE-06](#side-06) | Low | CPU cursor copy assumes the dumb buffer's stride and size | Source-confirmed | fixed `7f334993b` |
| [F-SEAT-11 correction](#f-seat-11-correction) | — | `flipRB` swaps *after* copying, so the copied pixels are not flipped | Source-confirmed | path replaced in `7f334993b` |

---

## SIDE-01

**`get_subsurface` on an orphaned subsurface parent crashes the compositor.** Severity: **High**. Any client can trigger it; no privileges are needed.

**Lifetime.** Destroying a parent `wl_surface` does not destroy the child's `wl_subsurface`. The child's `CWLSubsurfaceResource` keeps its role, and its `WP m_parent` expires. The role is reset only when the `wl_subsurface` resource itself is destroyed (`~CWLSubsurfaceResource`, `Subcompositor.cpp:93-98`).

**Chain at `e2ad066c9`:**

```
wl_subcompositor.get_subsurface(C, parent = B)        Subcompositor.cpp:163
  B->m_role is SURFACE_ROLE_SUBSURFACE                :182
  subsurface = B's role ->m_subsurface.lock()         :183   (no null check)
  subsurface->t1Parent()                              :184
    surf = m_parent.lock()                            :142   -> nullptr (A is gone)
    while (surf->m_role->role() == ...)               :145   -> null dereference, SIGSEGV
```

**Trigger:**
1. Create surfaces A, B and C.
2. `get_subsurface(B, A)`.
3. `wl_surface.destroy(A)`.
4. `get_subsurface(C, B)`.

[`repro/subsurface-orphan-parent`](../repro/subsurface-orphan-parent/) is a standalone client for this sequence.

**Executed:** the same sequence, sent through the protocol fixture against upstream `Subcompositor.cpp`, kills the test process with SIGSEGV:

```
#0 CWLSubsurfaceResource::t1Parent() [clone .cold]
#1 CWLSubcompositorResource::...{lambda(CWlSubcompositor*, unsigned, wl_resource*, wl_resource*)}
#3 _CWlSubcompositorGetSubsurface
```

With the fork's fix, the same test passes.

**Related.** `posRelativeToParent()` has the same unguarded `surf = m_parent.lock()` → `surf->m_role` pattern (`:121`, `:128`). Its callers (`CWLSurfaceResource::bfHelper`, `CSubsurface::coordsRelativeToParent`) normally start from a live tree, so reachability of that copy was **not** established. The fork's colour-management path (`getPreferredImageDescription`, F-CM-06) reaches `t1Parent()` the same way.

**Fix.** Guard the loop (`while (surf && ...)`). Return `nullptr` when a role's `m_subsurface` has expired. In `get_subsurface`, skip the `t1Parent` comparison when the parent's subsurface is gone.

## SIDE-02

**`t1Parent()` walks the wrong chain for nesting depth ≥ 3.** Severity: **Medium**.

At `Subcompositor.cpp:147`, the loop body reads `sc<CSubsurfaceRole*>(m_parent->m_role.get())`. It should read `surf->m_role`. Every iteration therefore reuses the *direct* parent's subsurface instead of advancing. For A ⊃ B ⊃ C ⊃ D, `D.t1Parent()` returns B instead of A: the visited set stops the loop after the second step.

**Executed:** with A=4, B=5, C=8, D=10, upstream returns surface 5; the expected value is 4.

**Consequences:**
- The `t1Parent == SURF` cycle check in `get_subsurface` (`:188`) compares against the wrong surface, so cycles through deeper chains are not rejected.
- Any feature keyed on the top-level surface uses an intermediate subsurface instead. In the fork this includes the preferred image description.

**Prior mention.** This was first noted as an unnumbered aside in [`06-protocols-shell.md`](../pending-evaluation/2026-10-full-audit/06-protocols-shell.md), with no end-to-end proof. It is now executed and numbered.

**Fix.** Use `surf->m_role` in the loop.

## SIDE-03

**`posRelativeToParent()` mis-positions subsurfaces at nesting depth ≥ 3.** Severity: **Medium**: wrong rendering and input positions for deeply nested subsurfaces.

`Subcompositor.cpp:130` has the same `m_parent->m_role` misuse as SIDE-02. For A ⊃ B ⊃ C ⊃ D, D's position becomes `D + 2·C` instead of `D + C + B`. Depth 2 is unaffected, because the first iteration happens to be correct.

**Executed:**
- Offsets: B = (100, 0), C = (10, 0), D = (1, 0).
- Upstream returns x = **21**; the expected value is **111**.
- The fork returned 21 as well until `475147aec`, which fixes it and adds a regression test.

**Fix.**
- Use `surf->m_role`.
- Guard a null `surf` (see SIDE-01).
- Guard an expired `m_subsurface`.

## SIDE-04

**The hardware cursor is not re-rendered when the output image description changes.** Severity: **Medium**: visible HDR brightness error.

Both hardware-cursor paths encode the cursor for the current `m_imageDescription`:
- The GPU path tags the cursor framebuffer with it (`PointerManager.cpp:705`).
- The CPU path copies sRGB bytes, which is HDR-COM-01.

The cursor buffer is rebuilt only on cursor image changes or on `onMonitorLayoutChange()`, which is reached via `modeChanged` (`PointerManager.cpp:91`) or `monitor.layoutChanged` (`:99`).

Two paths change the description **without** either event:
1. **CM-only rule changes.** `MonitorRule::compare` treats cm, SDR and ICC fields as a soft mismatch (`MonitorRule.cpp:20-31`). `MonitorRuleManager.cpp:166` then calls only `applyMonitorRuleSoft` → `applyCMType` (`Monitor.cpp:648`), which never emits `modeChanged`.
2. **Auto HDR.** `Renderer.cpp:2586-2587` calls `applyCMType` when a fullscreen HDR surface switches the output to PQ.

In both cases a cursor rendered for SDR stays on the plane after the output switches to PQ. Raw sRGB white is then read as PQ code ≈ 1.0, i.e. up to 10000 cd/m². This lasts until the cursor shape next changes. The inverse occurs on the way back to SDR.

**Fix.** Re-run the cursor backend whenever the image description changes, or after any soft rule. Defer it: `applyCMType` can run inside a render pass, where `renderHWCursorBuffer` bails.

## SIDE-05

**The cursor surface's own image description is ignored.** Severity: **Low**.

The renderer picks the source description from `tex->m_imageDescription`, then from `surface->m_current.imageDescription` (`OpenGL.cpp:1442-1460`). Surface textures never carry a description. Neither cursor path passes a surface:
- the GPU hardware cursor: `CTexPassElement::SRenderData{.tex = texture, .box = xbox}`, `PointerManager.cpp:726`;
- the software cursor: `data.tex = texture`, `:770`.

A cursor surface that sets a `wp_color_management_surface_v1` description (e.g. an HDR or wide-gamut cursor) is therefore composited as the default sRGB.

**Fix.** Pass the cursor surface in both render data paths.

## SIDE-06

**The CPU cursor copy assumes the dumb buffer's stride and size.** Severity: **Low**: depends on driver alignment.

`PointerManager.cpp:685` copies `cairo_image_surface_get_height × cairo_image_surface_get_stride` bytes into `buf->beginDataPtr(0)` in one `memcpy`. It never compares that against the dumb buffer's `strides[0]` or the returned size. If a driver pads the dumb-buffer stride beyond `width × 4`, every row after the first lands shifted. If it returns a smaller mapping, the copy writes past it. Common cursor widths (64, 128, 256) are usually aligned already, which hides the bug.

**Fix.** Copy row by row using the buffer's stride, after checking the returned format and size.

## F-SEAT-11 correction

F-SEAT-11 describes the `flipRB` handling as "R/B flicker". The actual order at `e2ad066c9` is:

```
auto& shmBuffer = CCursorSurfaceRole::cursorPixelData(SURFACE);   :623
texData = shmBuffer;                                               :641  (copy)
for (...) std::swap(shmBuffer[i], shmBuffer[i + 2]);               :648  (flips the stored data, not the copy)
```

The pixels sent to the plane are therefore **never** flipped by this render, so an ABGR8888 cursor is shown with red and blue swapped. The stored role data is then flipped in place, so the next render without a fresh `updateCursorShm` damage shows correct colours. The two states alternate from then on.

**Fix.** Flip the copy (`texData`), or convert once in `updateCursorShm`.
