# HDR community shader numerics

Benign standalone calculation checks for HDR-COM-02/03 in [the validation report](../../_evidence/community-hdr-validation.md). No Wayland socket, compositor reload, screen capture, output creation, KMS state change, or crash client is used.

The runner creates a surfaceless EGL/GLES 3.1+ compute context. It may use the installed GPU driver; it does not imply a software renderer or physical HDR acceptance.

Requirements: Python 3, a C compiler, pkg-config, EGL/GLES development headers/libraries, and a working surfaceless EGL implementation.

Export the unmodified official source at `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` into a temporary directory using an existing clone:

```sh
mkdir -p /tmp/hypr-hdr-source
git -C /path/to/Hyprland archive e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb | tar -xf - -C /tmp/hypr-hdr-source
python agent-report/color-management/_assets/hdr-community-numerics/verify.py --source /tmp/hypr-hdr-source
```

The source tree is read-only. Generated C binaries and shaders use a temporary directory and are removed afterward. `--output /path/to/result.json` optionally saves results. No network access occurs.

A successful run **reproduces the pinned upstream defects**; it is not a passing regression test for a fixed implementation. The script checks baseline source patterns, records source hashes, and deliberately refuses a changed decoding/adjustment implementation rather than silently reusing its old expectations.

The EXT_LINEAR signal 0.0203 produces about 1.624 nit with the hardcoded 80-nit range and about 203 nit with the declared 10000-nit range. A Windows-scRGB control retains its correct 80-nit scale. Multiplying the PQ code for 80 nit by 1.5 decodes to about 809.3 nit; a linear-light multiplier would give 120 nit. These are shader computations, not monitor measurements.
