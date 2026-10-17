# REV-15 Informational: renderer architecture and performance observations

- Agent confirmation: `none` (informational record, not a defect).
- Human confirmation: `none`.

Official baseline: `25f09b26f`. Observations from the 2026-10-17 review:

- Backend abstraction is nominal: `Renderer.hpp` includes `OpenGL.hpp`; GL calls in `ElementRenderer.cpp`, `WobbleTransformer.cpp`, `Monitor.cpp`, `SystemInfo.cpp`, `Compositor.cpp`, `MonitorResources.cpp`; `RT_VK` is aspirational.
- `IHyprRenderer` (~3.6k lines, ~100 members) also owns cursor state, layer-shell arrangement, lockscreen assets, HDR infoframes; `renderWindow`/`renderMonitor` ~280 lines each; 10+ `friend`s.
- Shader variants compile synchronously on first use mid-frame (no prewarm / `glProgramBinary` cache); each variant creates its own identical quad VAO/VBO.
- Per-draw heap allocation (`makeUnique`/`makeShared` pass elements) and `dynamicPointerCast` after a type-tag switch.
- Blur damage expansion uses ad-hoc factors (`Pass.cpp:197-212`, `FIXME: why does this break on * 1.F`); opaque regions overlapping live blur are not occluded.
- `end()` binds every unused pooled FB to invalidate it; final CM + screen shader are two full-screen passes.
- Dead code: `CRenderPass::empty()`, `SFragShaderDesc`, `fanVertsFull`, `eMonitor*RenderFBs`.
- AGENTS.md style: absolute include in `Renderer.hpp:36`; raw `new`/`delete` in glslang include callbacks.

[Batch overview](_evidence/upstream-review-2026-10-17.md).
