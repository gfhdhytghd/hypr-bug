# F-RES-03 JPEG empty-input and error-path handling

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Cleanup and MIME lifetime defects hold; MIME detection blocks stable empty files, and native decoding is serialized.

**Verdict: partially. Severity: Low, selected-file error-path leaks; latent API hazards.**

JPEG cleanup and MIME-data ownership defects exist in the locked library, but an empty `.jpg` filename does not establish the alleged upstream compositor crash. Concurrent JPEG calls are not present in the audited compositor path.

**Source-confirmed evidence:** `HG:src/image/formats/Jpeg.cpp:23-30` reads the whole file and indexes `bytes[0]`/`bytes[1]` without checking length. However, `HG:src/image/Image.cpp:59-64,83` first dispatches by `formatFromFile`, and `HG:src/image/utils/Format.cpp:10-26,49-66` accepts only recognized image MIME types. Empty input is not classified as JPEG. The span constructor does not dispatch JPEG at all (`Image.cpp:21-40`). Thus the direct decoder's precondition bug is real, while the dossier's stable empty-file wallpaper chain is broken. A concurrent file replacement between sniffing and decoder opening is a different precondition, not proof of the original claim.

**Source-confirmed:** the single global `jmp_buf` at `Jpeg.cpp:10-12` is shared by all decoder calls. `setjmp` returns directly on an error (`40-41`) without releasing a previously created decompressor (`43`) or a surface allocated later (`56`). The Cairo status-failure return (`58-59`) also skips decompressor destruction. The normal path only destroys it at `74`. A selected file recognized as JPEG but rejected by libjpeg **after decompressor creation** reaches a leak; no repeated reload or leak-volume measurement is claimed.

```cpp
if (setjmp(bailoutBuf))
    return std::unexpected("loading jpeg: libjpeg encountered a fatal error");
```

`Jpeg.cpp:72` passes `bytes.data()` into `cairo_surface_set_mime_data` with no destroy callback or retained backing owner. That vector is local (`25`); its bytes do not survive return. The normal raster-to-texture path does not read this MIME attachment, so an exploitable downstream read is not established. The one gatherer thread (`HG:src/resource/AsyncResourceGatherer.cpp:6-7,64-65`) serializes the compositor's native image decoding; the global jump buffer is therefore a library concurrency hazard, not a demonstrated compositor thread collision.

**Executed control:** `file --mime-type` on scratch `empty-file` reports `inode/x-empty`. This corroborates, but is narrower than, the source dispatch guard. No JPEG crasher, oversized allocation, or thread-collision test was run.

**Reachability and corrections:** selected local wallpaper content recognized as JPEG reaches the decoder through the shared chain. Merely naming a zero-byte file `.jpg` does not. The claimed multi-gigabyte successful Cairo allocation is not guaranteed: this decoder correctly checks surface status before obtaining data (`58-62`), unlike F-RES-01. Lock assets do not use it. Native repeated loading and a MIME-data consumer were not found.

**Minimal fix direction:** check input lengths, retain or copy MIME bytes with a destructor, keep error state per decoder, and ensure libjpeg/surface cleanup on every failure. Do not treat `thread_local` alone as complete ownership/error-unwinding repair.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
