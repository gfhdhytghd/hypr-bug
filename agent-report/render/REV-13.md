# REV-13 Shader compile/link failures leak GL objects and are cached forever

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-13 — Low

`src/render/Shader.cpp`: `compileShader` returns 0 on a dynamic compile failure without `glDeleteShader`; `createProgram` returns early on fragment failure leaking the compiled vertex shader, and the link-failure path leaks `prog`. `CHyprOpenGLImpl::getShaderVariant` (`src/render/OpenGL.cpp:2593-2596`) logs the failure but still caches the shader with program 0, so every later draw with that feature set silently runs without a program and is never retried.

Fork status: **not fixed**.

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
