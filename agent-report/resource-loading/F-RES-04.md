# F-RES-04 WebP empty-input and error-path handling

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Cairo failure leak and MIME lifetime hold; empty-file reachability and second full decoder-buffer leak do not.

**Verdict: partially. Severity: Low, selected-file cleanup and latent MIME ownership.**

The WebP failure branch loses an allocated Cairo surface, and MIME bytes have invalid lifetime. Empty-file crash reachability and a second full libwebp output-buffer leak are not supported by the native call path.

**Source-confirmed evidence:** `HG:src/image/formats/Webp.cpp:14-21` reads the whole file and directly indexes four magic bytes. As in F-RES-03, the `CImage` file constructor first requires recognized MIME (`HG:src/image/Image.cpp:59-64,85`; `HG:src/image/utils/Format.cpp:17-18,49-66`). Stable empty input is rejected before this function. The memory constructor does not dispatch WebP (`Image.cpp:32-40`). File replacement after detection remains a distinct file-writer precondition.

`Webp.cpp:29-38` validates WebP features before allocating, and correctly destroys a failed Cairo surface. After successful allocation, it points the decoder at that surface (`47-56`), then returns without destroying the surface if full decode fails (`58-59`):

```cpp
config.output.is_external_memory = 1;
if (WebPDecode(bytes.data(), bytes.size(), &config) != VP8_STATUS_OK)
    return std::unexpected("loading webp: webp decode failed");
```

A feature-readable but incompletely decodable selected WebP reaches this branch. The allocation is owned by Cairo; no owner receives it on the error return. The call to `WebPFreeDecBuffer()` is also bypassed, but `/usr/include/webp/decode.h:201-206` explicitly describes the external-memory mode: the internal output allocation is not used. Missing that call is not proof of a second full-sized buffer leak. `Webp.cpp:62` attaches the local vector's bytes as MIME data without extending their lifetime, the same latent issue as JPEG.

**Reachability/preconditions:** selected local asset content must first pass MIME and feature detection. There is no native client-to-filename route. Decoding failure is a library error branch, not proof that every malformed file leaks a maximum-sized surface, nor proof that configuration reload repeats the allocation.

**Corrections:** reject the zero-byte-by-extension scenario; retain the Cairo failure leak and MIME lifetime finding. No successful WebP-based use-after-free or memory-exhaustion reproduction is claimed.

**Minimal fix direction:** use an owned Cairo surface from creation through error handling, guard the input length, pair decoder cleanup consistently, and attach MIME data only with an owner that survives the surface's use.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
