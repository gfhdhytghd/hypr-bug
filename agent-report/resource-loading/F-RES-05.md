# F-RES-05 AVIF resource cleanup and allocation checks

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Cleanup and Cairo status defects hold; optional AVIF support and libheif limits constrain the claimed bomb.

**Verdict: partially. Severity: Medium, selected-file resource handling.**

AVIF context/handle/image ownership is incomplete and the final Cairo copy lacks a status check. The dossier's unrestricted huge-image estimate ignores libheif's own limits and its optional build gate.

**Source-confirmed evidence:** `HG:src/image/formats/Avif.cpp:66-89` allocates a context, then returns on file/memory parsing failure **before** `heif_context_free()` (`73-74,85-86`). This is a separate context leak omitted from the dossier. `Avif.cpp:13-20` obtains a primary-image handle without checking that call's result and returns on decode failure without releasing a successfully obtained handle. After successful decoding, the dimension/plane error returns (`25-32`) bypass the image and handle release calls at `61-62`.

`Avif.cpp:34-58` retains libheif's decoded image, allocates an intermediate RGBA vector, and allocates a Cairo surface before copying:

```cpp
auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
if (!CAIROSURFACE)
    return std::unexpected("loading avif: cairo failed");
memcpy(cairo_image_surface_get_data(CAIROSURFACE), rawData.data(), rawData.size());
```

The pointer check does not reject a Cairo error surface. The later `CImage` status check (`HG:src/image/Image.cpp:94-96`) is too late. Once decoding succeeds, the extra full-image copies are real; exact memory use depends on dimensions, decoder storage, and successful prior allocations.

**Source-confirmed guards and chain:** `HG:src/image/Image.cpp:73-77` only dispatches AVIF when built with `HEIF_FOUND`; otherwise it returns an explicit unsupported error. Content must be recognized as AVIF, then be read/decoded by libheif. `/usr/include/libheif/heif_security.h:43-46,64,91-103` documents default pixel and memory limits inherited by new contexts; this decoder does not disable them.

**Executed benign controls:** local libheif 1.23.5 reports `max_image_size_pixels=1073741824`, `max_memory_block_size=4294967296`, and `max_total_memory=4294967296`. A separate Cairo invalid-size call returns a non-null surface with status 32 and null data. These confirm API semantics/defaults, not an AVIF crash or the limits of every distribution's libheif version. No large image was allocated.

**Reachability/severity:** an administrator-managed or user-prefix selected wallpaper file is the native entry; a client cannot submit AVIF to this loader. Malformed recognized input can leak contexts; post-decode failures can leak other owners. The restricted file boundary and existing library limits reduce the original generalized bomb claim.

**Corrections:** an image object is not necessarily created on decode failure, so that branch proves a handle leak, not unconditional handle-plus-image leakage. The claimed three unrestricted enormous allocations are not established. Do not assume a supported AVIF build or a particular decoder's handling of failed primary-handle lookup.

**Minimal fix direction:** acquire context/handle/image through scoped owners, check every acquisition result, enforce a compositor budget within the library's limits, validate Cairo status, and eliminate the intermediate full-image copy where possible.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
