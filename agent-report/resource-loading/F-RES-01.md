# F-RES-01 PNG decoding lacks decoded-size limits

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Locked decoder lacks byte budget and Cairo status check; selected local assets, not client filenames, reach it.

**Verdict: confirmed. Severity: Medium, restricted to selected local asset content.**

The locked PNG decoder has no application-level decoded-byte budget, and it copies into a Cairo surface before checking its status. The default-wallpaper path above reaches it when the selected asset's contents are recognized as PNG.

**Source-confirmed evidence:** `HG:src/image/formats/Png.cpp:75-102` obtains `size_t` dimensions and allocates a full RGBA buffer:

```cpp
const size_t W = png_get_image_width(png, info);
const size_t H = png_get_image_height(png, info);
rawData.resize(W * H * 4);
```

`Png.cpp:121-126` then allocates a second surface and tests only its pointer:

```cpp
auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
if (!CAIROSURFACE)
    return std::unexpected("loading png: cairo failed");
memcpy(cairo_image_surface_get_data(CAIROSURFACE), rawData.data(), rawData.size());
```

Cairo's status-bearing error surface is not rejected here; the `CImage` status check (`HG:src/image/Image.cpp:94-96`) runs **after** this copy and therefore cannot protect it. Allocation exceptions from the full-image vector also escape the worker's `render()` call. This establishes resource exhaustion and an invalid-destination copy on a Cairo allocation/size error, without needing a running compositor test.

**Reachability/preconditions:** the logo/background must be enabled (`misc:disable_hyprland_logo`, `Renderer.cpp:1486-1492`) and the selected local asset must contain the problematic PNG. No client-controlled image filename or network entry was found. Administrative or installation-directory write access, or deliberate loading of an untrusted replacement asset, is required; this supports Medium rather than the dossier's unconditional High.

**Corrections:** libpng is not unrestricted up to the format's theoretical dimension maximum: local `/usr/include/pnglibconf.h:223-224` defines default width/height limits of 1,000,000. These are still not a usable compositor byte budget. The multiplication here is `size_t`, not an established 32-bit truncation. A bare IHDR is not a demonstrated complete decoder input; no giant allocation was executed. The claimed double allocation occurs only if decoding reaches surface creation. The dossier's `misc:disable_wallpaper` and lock-reload descriptions do not match this upstream path.

**Minimal fix direction:** apply checked dimension/decoded-byte limits before allocations, verify `cairo_surface_status()` before obtaining data, copy with the actual destination stride, and contain worker exceptions as resource failures. Bound memory without allocating giant test images.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
