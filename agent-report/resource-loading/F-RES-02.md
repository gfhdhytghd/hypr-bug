# F-RES-02 BMP parser arithmetic and buffer bounds

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Wrong 32-bpp destination and leaks hold; claimed 24-bpp conversion/allocation size mismatch does not.

**Verdict: partially. Severity: Medium, asset-file memory safety.**

The BMP parser has a real incorrect 32-bpp read destination, unchecked arithmetic, and buffer ownership leaks. The dossier's specific claim that the 24-bpp conversion uses an unwrapped size after a wrapped allocation is false.

**Source-confirmed evidence:** `HG:src/image/formats/Bmp.cpp:13-30,32-69` uses unsigned 32-bit dimensions/file sizes and checks a 32-bit product without overflow or stream-state validation:

```cpp
if (imageSize != (width * height * numberOfBitPerPixel / 8))
    return "Unable to parse bitmap header: wrong image size";
```

`Bmp.cpp:111-125` gets Cairo's stride, allocates with the already-evaluated product, and passes `&imageData` rather than the allocated storage to the 32-bpp stream read. This is a read into a pointer-sized local object with a file-derived byte count; valid-looking BMP content is not bounded to that object's capacity. The header acceptance checks do not guard this destination. This is source-confirmed memory corruption, not an executed exploit or established code execution.

`Bmp.cpp:113-116` passes **the same** `height * stride` expression to both allocation and `convertRgbToArgb(..., uint32_t newImageSize)`. Thus the asserted allocation/conversion size mismatch does not follow. Independently, row reflection (`73-84,124`) indexes using the original height/stride and uses signed intermediate products; there is no checked relation to the allocation. Neither a negative/invalid stride nor failed `malloc()` is rejected. The successful `cairo_image_surface_create_for_data()` path (`125`) installs no ownership callback, and the unsupported-bpp return (`119-121`) does not free the allocation.

**Reachability/preconditions:** the shared wallpaper chain can dispatch BMP by content (`HG:src/image/Image.cpp:59,72`) even though the selected asset name ends in `.png`. The parser's own header checks precede the affected reads, but no further width/product/stream guards close them. Local asset substitution is required; a title, app ID, or cursor theme file does not enter this decoder. Severity reflects this restricted boundary; the dangerous decoder operation itself warrants prompt dependency repair.

**Corrections:** the dossier's numerical overflow example is not a verified accepted BMP: its header arithmetic includes multiplication by bits-per-pixel **before division by eight**, and the 24-bpp output count also wraps. No sample, stack overwrite, or heap overwrite was executed. Cairo does not provide a general BMP file loader as the suggested replacement implies.

**Minimal fix direction:** replace the parser or use checked wide arithmetic and strict header/stream validation; read into owned pixel storage; reject invalid strides/allocation failure; keep storage alive exactly as long as the Cairo surface. Test ordinary BMP formats and bounded malformed headers in an isolated dependency test suite.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
