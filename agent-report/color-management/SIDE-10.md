# SIDE-10 Runtime render:fp16_sdr_tf change retags the cached blur and work buffers without invalidating them

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `12f242511`; fork comparison: `5d9395bfa` (fixed in `b0136905e`).

SIDE-10 — Low — Source-confirmed

Found while auditing toggle paths for SIDE-09.

`render:fp16_sdr_tf` has no refresh hook (`src/config/values/ConfigValues.cpp`, no `.refresh` flag). With FP16 on an SDR preset, `CMonitor::workBufferImageDescription()` switches between the monitor TF and linear on the next call. `CMonitor::resources()` then calls `CMonitorResources::setImageDescription()`, which retags the blur FB and all pooled work buffers with the new description but leaves their contents in the old encoding, and neither sets `m_blurFBDirty` nor damages the output (`src/output/MonitorResources.cpp:52-62`).

Effect: the precomputed blur (tiled windows) and any work-buffer regions not covered by the next frame's damage are decoded with the wrong transfer function until something else invalidates them. Description changes that go through `CMonitor::applyCMType()`, ICC reload or `handleDSleave()` are not affected, since those paths set `m_blurFBDirty` and damage the monitor themselves.

Fork fix `b0136905e`: `CMonitorResources::setImageDescription()` marks the blur FB dirty and damages the monitor whenever the description actually changes (the same pattern `enableMirror()` already uses).

Upstream status: same code at `12f242511`; not fixed.
