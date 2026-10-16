# F-PROTO-08 Unbounded region / layer configure bookkeeping

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Complex regions and outstanding layer serials are unbounded; identical rectangles coalesce, and unchanged mapped sizes do not always produce configure events.

**Verdict: partially. Severity: Low.**

The dossier combines unbounded region complexity with unbounded layer configure history. Both retain unbudgeted state, but repeated identical rectangles do not multiply storage and configuration generation has caller guards.

**Evidence — source-confirmed.** `src/protocols/core/Compositor.cpp:71-72` forwards region add/subtract directly to `CRegion`; there is no Hyprland rectangle-complexity budget. `set_input_region` copies the region when the request is handled (`:258-269`), not automatically on every commit. State copies retain it; hit-testing materializes a clipped copy through `src/protocols/types/SurfaceState.cpp:48-55` → `Compositor.cpp:537-539`. A mapped input-eligible surface with a genuinely fragmented region can therefore incur repeated copy/intersection work during normal pointer hit-testing. The region object itself can retain complexity before mapping; no special protocol or permission is required.

**Counterexample — executed (dependency-only).** The bounded pixman control in `dependency_controls.c` under the designated scratch directory performed 10,000 identical rectangle unions and observed **one** stored rectangle. A separate 128-disjoint-rectangle control observed 128 stored rectangles. A second control, `region_controls.cpp`, called the actual installed hyprutils `CRegion::add`/`getRects` methods and independently obtained the same counts. These use hyprutils 0.14.2/pixman 0.46.4, do not instantiate Hyprland, and do not connect to a desktop. `dependency_controls.log` and `region_controls.log` record the results. Hyprutils `Region.hpp:13-18,27-38,63-79` exposes/copies pixman regions. Consequently, the dossier's repeated same-coordinate add example does not establish its memory-amplification claim. Complex non-coalescing regions remain possible; exact whole-compositor CPU costs were not measured.

**Evidence — source-confirmed.** The layer queue is a vector of serial/size pairs (`src/protocols/LayerShell.hpp:89`). Each configure appends at `src/protocols/LayerShell.cpp:195-202`, and acknowledgment removes only the matching entry (`:132-143`), with no cap or age-based pruning:

```cpp
m_serials.push_back({serial, size_}); // LayerShell.cpp:200
// In the ack callback:
m_serials.erase(serialFound);        // :143
```
 The chain is layer state request/commit → `LayerShell.cpp:43-82` → `src/desktop/view/LayerSurface.cpp:300-308,325-354` → `src/render/Renderer.cpp:2892-2893` → `configure` → retained history.

There are important caller conditions. Renderer configure is normally sent only when arranged size differs from previous geometry (`Renderer.cpp:2892`); repeatedly setting an unchanged mapped size does not necessarily generate entries. However, unconfigured/unmapped layer commits reset `m_geometry` before arrangement (`LayerSurface.cpp:304-308`), so positive arranged geometry can keep generating configure entries without a mapped buffer. For a mapped layer, accepted changes of desired geometry can also generate entries without further acknowledgments. A buffer attached before any valid initial ack is rejected (`LayerShell.cpp:49-51`), but this does not cap the bufferless configuration path. The client must drain outgoing configure events to avoid ordinary output-buffer backpressure becoming the practical bound. Layer-shell additionally requires the global access and monitor conditions described under F-PROTO-05.

**Severity/corrections.** Low resource-budget weakness; no measured compositor-wide exhaustion or fixed amplification factor. Region complexity is canonical geometry, not the number of add requests. Layer history can grow even when a client acknowledges newer configurations because only the matched serial is erased, rather than retiring older entries. Do not claim a guaranteed configure per arbitrary size setter without following the commit/geometry guards.

**Fix direction.** Apply measured per-client region-complexity/resource budgets and bound outstanding configuration history, retiring obsolete serials according to issuance order. The dossier's suggested `WL_REGION_ERROR_INVALID_SIZE` is not a defined core protocol error; use an appropriate supported failure policy rather than inventing an enum.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
