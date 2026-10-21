# CMS-14 Stale HDR_OUTPUT_METADATA after a crash in HDR leaves the next (safe-mode) session overexposed

- Agent confirmation: `confirmed` (source review of Hyprland and aquamarine).
- Human confirmation: `none`; reported by the user (crash in HDR → safe mode → bright, overexposed picture on upstream Hyprland); runtime reproduction with KMS property dumps still pending.
- Scope: static source evidence at the revisions below.

## Agent report

Official baselines: Hyprland `c7216a980`, aquamarine `be8909b`; fork comparison: Hyprland `49ba7360a`, aquamarine `8625b681e`.

CMS-14 — High (the session is unusable on affected displays until a modeset or HDR toggle)

A compositor that crashes while signalling HDR can't clean up, so the connector keeps `HDR_OUTPUT_METADATA` (PQ, EOTF=2) and `Colorspace` (BT2020_RGB) until the next DRM master writes them. Safe mode starts with the default config, i.e. SDR, so the display must be taken out of HDR by the new instance.

- `Colorspace` is fine: aquamarine force-writes connector properties on its first commit (`forceConnProps`, `src/backend/drm/impl/Atomic.cpp`).
- `HDR_OUTPUT_METADATA` is only written when the compositor staged HDR metadata (`DRM.cpp`: `data.hdrMetadata` only set when `AQ_OUTPUT_STATE_HDR` is committed). aquamarine's output state starts zero-initialized and never reads the kernel's current value.
- Upstream Hyprland stages `NO_HDR_METADATA` on the first frame (`INITIAL_SDR`, `src/render/Renderer.cpp` `handleFullscreenSettings`), but upstream aquamarine turns it into a blob with EOTF=0 instead of clearing the property. KMS keeps sending a Dynamic Range and Mastering InfoFrame, and some sinks stay in their HDR picture mode → overexposed SDR. The same applies to every runtime HDR→SDR switch upstream.
- Fork regression: Hyprland `a6d9d876d` (REV-08) only stages metadata that differs from the output state. On a fresh instance that state is zero and equals `NO_HDR_METADATA`, so the first SDR frame sends nothing and the crashed instance's PQ metadata stays untouched (worse than upstream; the fork's aquamarine `743db96`, which writes 0 for EOTF=0, never gets the chance). The `supports_hdr = -1` path from `9736f2fe6` had the same blind spot.

Runtime HDR→SDR switches are not affected in the fork: `inHDR()` reads the signalled metadata, so `HDR_CHANGED` triggers and aquamarine clears the property.

Fork fixes:
- aquamarine `4abfcab`: write `HDR_OUTPUT_METADATA` from the state on the first commit (SDR clears the property), like `Colorspace`, independent of the compositor. Gamma/degamma LUTs and CTM were already re-sent on the first commit (it's a modeset; unknown CTM state is cleared).
- Hyprland `d0e4219aa`: stage SDR metadata unconditionally on the first frame, also when the monitor doesn't support HDR; bumps the aquamarine lock.

Upstream status: Hyprland `c7216a980` and aquamarine `be8909b` still send an EOTF=0 blob; not fixed. Related upstream attempt: hyprwm/Hyprland#16244 ("Fix stale HDR metadata on initial SDR", closed as an AI-policy violation).
