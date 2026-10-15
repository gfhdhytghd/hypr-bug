# HDR-COM-01 CPU-copy hardware cursor bypasses output color management

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: historical evidence at the revisions below; current-version and physical-display acceptance must be established separately.

## Agent report

Official baseline: `e2ad066c9`; fork comparison: `3d52e94fd`.

**Scope:** hardware cursors on a PQ output when `cursor:use_cpu_buffer=1`, or automatic CPU-buffer selection on NVIDIA. Software cursors and the GPU-rendered hardware-cursor path are not the finding.

**Community:** [Hyprland #14419](https://github.com/hyprwm/Hyprland/discussions/14419), [#14028](https://github.com/hyprwm/Hyprland/discussions/14028), [hyprcursor #95](https://github.com/hyprwm/hyprcursor/issues/95). The ordinary hardware-cursor fix [PR #15905](https://github.com/hyprwm/Hyprland/pull/15905) was merged, but does not cover the CPU-copy branch. The remaining branch is also unchecked in [the official tracker](https://github.com/hyprwm/Hyprland/issues/9064).

**Closed source chain:**

1. [PointerManager.cpp:549–551](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/pointer/PointerManager.cpp#L549-L551) selects CPU buffers explicitly, or automatically for NVIDIA.
2. [Lines 617–693](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/pointer/PointerManager.cpp#L617-L693) obtains cursor bytes, applies a Cairo geometric resampling, copies the ARGB bytes into the scanout buffer, and returns. There is no source/output image-description conversion in this branch.
3. [Lines 702–726](https://github.com/hyprwm/Hyprland/blob/e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb/src/pointer/PointerManager.cpp#L702-L726) tags the framebuffer with the output image description and renders the cursor through the color-managed renderer, but runs only after the CPU branch has returned. Its comment explicitly identifies raw sRGB cursor data being interpreted in a PQ output as the problem.

**Numeric implication, not a panel measurement:** opaque sRGB white copied unchanged remains code 1.0. If that plane is interpreted in the output PQ encoding, code 1.0 denotes 10,000 cd/m²; 80 cd/m² would encode to approximately 0.485857. The panel/driver's realized brightness and blending are unmeasured, so no measured nit value or burn-in claim is made.

**Local status:** the pinned fork has the same selection, early return, and later framebuffer-tagging lines at `src/pointer/PointerManager.cpp:549–726`. Its other HDR fixes do not cover this branch. No live cursor-plane test was performed.

**Fix direction:** color-convert the CPU buffer to the output encoding, or use a correctly managed fallback when that conversion cannot be provided. Preserve the distinction between CPU and GPU buffer paths when testing a future fix.

[Full validation, exclusions, baselines and limitations](_evidence/community-hdr-validation.md).
