# Resource-loading dossier verification

- Baseline: official Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).
- Dossier: [11-resource-loading](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md).
- Date: 2026-10-15.
- Method: read-only source review of the pinned worktree and locally available dependency sources/headers. No desktop connection, compositor execution, full build, network, or GitHub interaction. Evidence is explicitly labelled source-confirmed or executed. Dependency findings are distinguished from code pinned by the Hyprland commit.
- Status: complete. All 12 dossier IDs are covered; dependency provenance is authenticated against the upstream lock file.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-RES-01 | confirmed | Medium (asset files) | `src/render/Renderer.cpp:1519-1533`; HG `src/image/formats/Png.cpp:75-126` | Locked decoder lacks byte budget and Cairo status check; selected local assets, not client filenames, reach it. |
| F-RES-02 | partially | Medium (asset files) | `src/render/Renderer.cpp:1519`; HG `src/image/formats/Bmp.cpp:64,111-125` | Wrong 32-bpp destination and leaks hold; claimed 24-bpp conversion/allocation size mismatch does not. |
| F-RES-03 | partially | Low | `src/render/Renderer.cpp:1519`; HG `src/image/formats/Jpeg.cpp:10-74`, HG `src/image/Image.cpp:59-64` | Cleanup and MIME lifetime defects hold; MIME detection blocks stable empty files, and native decoding is serialized. |
| F-RES-04 | partially | Low | `src/render/Renderer.cpp:1519`; HG `src/image/formats/Webp.cpp:14-64`, HG `src/image/Image.cpp:59-64` | Cairo failure leak and MIME lifetime hold; empty-file reachability and second full decoder-buffer leak do not. |
| F-RES-05 | partially | Medium (asset files) | `src/render/Renderer.cpp:1519`; HG `src/image/formats/Avif.cpp:12-89` | Cleanup and Cairo status defects hold; optional AVIF support and libheif limits constrain the claimed bomb. |
| F-RES-06 | confirmed | Medium | `src/render/Renderer.cpp:1702-1705`; HG `src/resource/AsyncResourceGatherer.cpp:32-72` | Completion before waiter registration can strand synchronous overlay rendering; completion fields also race. |
| F-RES-07 | confirmed | High | `src/protocols/core/Shm.cpp:65-113`; `src/protocols/core/Compositor.cpp:788-824` | Mutable SHM backing is read without fault containment; initial size/layout checks do not prevent later SIGBUS. |
| F-RES-08 | partially | Low | `src/managers/XCursorManager.cpp:241-278,290-323` | Unconfined inherited names and rescans hold; the equals-sign guard rejects the alleged prefixed-key spoofing. |
| F-RES-09 | partially | Informational | `src/managers/XCursorManager.cpp:191-197,525-562` | Symlinks and unrewound retry exist; no privilege escalation, universal EOF failure, or applicable libXcursor overflow is established. |
| F-RES-10 | partially | Medium (nondefault debug) | `src/protocols/ColorManagement.cpp:136-153,481-527`; `src/protocols/ColorManagement.hpp:102-124` | Descriptor leaks hold and debug mode makes them reachable; default rejection and absence of client ICC parsing still hold. |
| F-RES-11 | partially | Informational / Low dependency cleanup | `src/render/Renderer.cpp:1519`; HG `src/image/formats/Svg.cpp:25-95` | Explicitly sized SVG failures leak; native zero-size guard and librsvg policy break claimed file/network/XXE reachability. |
| F-RES-12 | partially | Informational control | `src/helpers/Format.cpp:53-80`; `src/render/Renderer.cpp:1482-1710`; HG `src/egl/Egl.cpp:279-280` | Plain-text titles and fixed content-sniffed assets hold; complete SHM arithmetic assurance, unconditional ICC unreachability, and ownership assumptions do not. |

## Dependency provenance and shared file-entry boundary

**Executed:** the upstream worktree reports HEAD `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb`. **Source-confirmed:** `CMakeLists.txt:151-157` requires **hyprgraphics >= 0.5.1**, rather than vendoring or fixing an exact version for every distribution. However, `flake.lock:86-92` pins hyprgraphics revision `80c7d00f2236732853e390019ff8d79d17fa3612` with NAR hash `sha256-hLh6ALHrGpgYm8viWCo1pO+ErC22YDGdAvVPcfYsXA8=`.

**Executed:** the dossier's extracted Nix source has a different whole-tree hash because three Nix files are absent. The local Nix fetcher cache maps the pinned revision to Git tree `ab6b3acd9b6662c58509804f371f240b3939f029`. That tree was exported, without modifying the cache, into the assigned scratch directory as `hyprgraphics-80c7d00/`. Its computed NAR hash **exactly matches `flake.lock`**. All audited C++ files are byte-identical to the dossier snapshot. File hashes and the comparison are recorded in scratch `dependency-provenance.json`. Below, `HG:` denotes paths in this authenticated dependency tree. Dependency conclusions apply to this locked build; an arbitrary system package satisfying the minimum version needs its own version check.

**Source-confirmed shared chain:** `src/render/Renderer.cpp:1482-1533` selects a fixed `wall0.png`/`wall1.png`/`wall2.png` name, resolves it, constructs `CImageResource`, and enqueues it. `src/render/types.hpp:15-21` lists build-time `DATAROOTDIR`, `/usr/share`, and `/usr/local/share`; `Renderer.cpp:1536-1554` appends `/hypr/<filename>` and chooses an existing path. `HG:src/resource/resources/ImageResource.cpp:24-28` constructs `CImage` from that path. `HG:src/image/Image.cpp:57-96` sniffs the content and dispatches to the decoder; the worker invokes `render()` without an exception handler at `HG:src/resource/AsyncResourceGatherer.cpp:64-72`.

The boundary is **the content of a selected local asset**, not a filename supplied by a Wayland/X11 client. Ordinary installations place these assets in administrator-managed directories, but the source does not verify ownership or require a root-owned `DATAROOTDIR`; user-prefix installations and symlinked assets are possible. A plugin already executes inside the compositor and is not a lower-trust client boundary. Lock-death assets take the different `loadAsset()` path using **Cairo's PNG loader** (`Renderer.cpp:1557-1574,1721-1729`), so they do not establish reachability of the HG JPEG/BMP/WebP/AVIF decoders. The background is cached (`1483-1484`); a configuration reload alone is not proof of repeated decoding.

## F-RES-01

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

## F-RES-02

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

## F-RES-03

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

## F-RES-04

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

## F-RES-05

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

## F-RES-06

**Verdict: confirmed. Severity: Medium, main-loop availability.**

The authenticated dependency can complete a resource before `await()` registers a waiter, leaving the compositor's synchronous text renderer waiting indefinitely. The resource's atomic shared-pointer lifetime does not synchronize its separate completion fields.

**Source-confirmed evidence:** `src/render/Renderer.cpp:1702-1705` performs `enqueue(res)` immediately followed by `await(res)`, without a completion check. `HG:src/resource/AsyncResourceGatherer.cpp:32-36` creates a condition variable and waits only for `awaitingEvent`; `HG:src/resource/resources/AsyncResource.hpp:6-10` initializes that event to false. The worker (`AsyncResourceGatherer.cpp:64-72`) records completion for the waiter only inside this conditional:

```cpp
if (r->m_impl->awaitingCv) {
    r->m_impl->awaitingEvent = true;
    r->m_impl->awaitingCv->notify_all();
}
r->m_ready = true;
```

If completion occurs before waiter registration, no durable event is set. A subsequent `await()` cannot observe `m_ready`, because its predicate never checks that atomic. The job is removed from the queue (`58-60`), so no later normal notification is guaranteed. This is a closed source-level missing-completion-state path, not merely a speculative spurious-wakeup concern. The worker also accesses the unique condition-variable pointer and non-atomic event without `awaitingMtx`; atomic ownership of `res` does not make those accesses race-free.

**Call/input chain and preconditions:** configuration-file errors reach the error overlay (`src/errorOverlay/Overlay.cpp:94-95,98-133`), which passes `STextResourceData` to this synchronous renderer. The optional debug-overlay warning does likewise (`src/debug/Overlay.cpp:356-374`). Resource completion scheduling can interleave with waiter registration during these ordinary operations; no malicious file or external client is necessary. The result is a possible main-thread hang, not demonstrated physical lockscreen acceptance or universal reproducibility.

**Corrections:** the lockscreen TTY string calls the **plain-text overload** (`Renderer.cpp:1729`), which renders synchronously without this gatherer. Window titles and notification text also use that overload. A timeout alone is not a complete correction for the data races or condition-variable lifetime.

**Minimal fix direction:** maintain one durable completion state under the same mutex used by the wait; always publish completion, then notify; give the condition variable stable lifetime; synchronize queue wake/exit flags as well. Verify completion-before-wait, completion-during-wait, and failure completion in dependency tests. No timing-amplification harness was run here.

## F-RES-07

**Empirical reproduction — executed.** Nested harness (2026-10-15): client took the cursor role (virtual-pointer enter + wl_pointer.set_cursor) over a wl_shm buffer whose backing file was ftruncated before the compositor flushed it — SIGBUS in updateCursorShm (`COMPOSITOR_EXITED status=135`).

**Verdict: confirmed. Severity: High, local Wayland-client compositor denial of service.**

Hyprland's custom shared-memory implementation validates initial file size and buffer layout but has no fault-contained access to backing storage whose size can subsequently change. The kernel can deliver SIGBUS during a later compositor read; no source-level guard converts that fault into a client error.

**Source-confirmed evidence:** `src/protocols/core/Shm.cpp:103-113,186-195` checks file size when accepting a pool; `125-135` repeats that check for a requested pool resize. `81,97` map the client's backing file with `MAP_SHARED`. These checks establish a point-in-time file length, not immutable backing. Pool resize rejects shrinking through the protocol, but that does not control changes made through another descriptor for the same file. The code has no shrink-seal requirement or equivalent immutable-copy guarantee.

```cpp
std::tuple<uint8_t*, uint32_t, size_t> CWLSHMBuffer::beginDataPtr(uint32_t flags) {
    return {sc<uint8_t*>(m_pool->m_data) + m_offset,
            m_fmt, sc<size_t>(m_stride) * sc<size_t>(size.y)};
}
```

`Shm.cpp:65-70` returns the mapped bytes directly and provides an empty `endDataPtr()`. `CWLSurfaceResource::updateCursorShm()` intersects damage with buffer bounds (`src/protocols/core/Compositor.cpp:805`), obtains those bytes (`808`), and copies from them (`815-824`). Bounds checking the **mapping** does not ensure its file-backed pages still exist. This direct CPU copy is sufficient to establish the affected read; GPU-driver behavior is not needed to justify it.

**Source-confirmed call and guard analysis:** client-provided shared-memory storage enters the pool/buffer constructors (`Shm.cpp:138-168,186-204`). The surface attach handler resolves a real `wl_buffer` and retains its buffer (`Compositor.cpp:97-129`); regular state processing goes through `scheduleState()` (`607-626`), `CSurfaceStateQueue::tryProcess()` (`src/protocols/types/SurfaceStateQueue.cpp:93-109`), and `commitState()` (`Compositor.cpp:659-673`). SHM is synchronous (`Shm.cpp:50-51`). Surface texture updates consume its data through `src/protocols/types/SurfaceState.cpp:58-69`; cursor-role commits additionally reach the explicit CPU-copy routine. The cursor role is assigned and its cache updated in `src/protocols/core/Seat.cpp:125-137`. Object-interface validation, buffer shared ownership, and pool lifetime prevent several wrong-object/dangling-object cases; they do not preserve the backing file's size.

The protocol is created as a core global (`src/managers/ProtocolManager.cpp:163-167`) and is explicitly allowed even by the sandbox-global filter (`348-365`; `src/Compositor.cpp:267-271`). No screen-capture permission, plugin, system asset write, or special build option is needed for the resource boundary. A client must have access to the compositor's Wayland socket and backing storage whose size remains mutable; this is a **local client**, not an unauthenticated network peer. Storage losing backing before a mapped page is consumed is the failure precondition. No request sequence or fault-triggering client was executed.

**Source-confirmed containment review:** searches of upstream source found no SIGBUS fence or `wl_shm_buffer_begin_access()`/`end_access()` use around these custom mappings. Signal setup handles SIGSEGV/SIGABRT (`src/Compositor.cpp:292-299`), not SIGBUS. Libwayland's API for its own `wl_shm_buffer` objects (`/usr/include/wayland-server-core.h:662-672`) does not automatically protect a separately implemented `mmap` and `memcpy`. Existing mappings stay referenced through `m_pool`; lifetime retention is not fault recovery.

**Corrections:** the dossier's `bufLen` side note is wrong: after returning `data + offset`, the length of this buffer is still `stride * height`, **not** that quantity minus offset. The independent layout check includes offset when comparing against pool size (`src/helpers/Format.cpp:71-80`). Its protection is spatial, not temporal. The claim that this is the only possible client-triggered resource fault in the whole compositor is outside this dossier's proof.

**Minimal fix direction:** adopt fault-contained shared-memory reads consistently across all CPU and upload entry points, with client-local failure handling. Prefer a proven existing SHM-access implementation where integration permits. A point-in-time `fstat()` is insufficient. Requiring seals can be an additional Linux-specific policy but would reject otherwise valid ordinary SHM clients; requiring `F_SEAL_GROW` would also conflict with legitimate pool growth. Do not prescribe mandatory seals as a drop-in protocol-compatible fix.

## F-RES-08

**Verdict: partially. Severity: Low, theme-file parsing/confinement; no demonstrated privilege boundary.**

Inherited theme names are interpolated into paths without confinement and an expanding inheritance graph is repeatedly rescanned. The alleged malformed-key spoofing does **not** pass the parser's later equals-sign guard.

**Source-confirmed evidence:** `src/managers/XCursorManager.cpp:241-257` contains both the suspicious search and the decisive guard:

```cpp
if (line.rfind("Inherits", 8) != std::string::npos) {
    std::string inheritThemes = line.substr(8);
    // trim leading whitespace ...
    if (inheritThemes.at(0) == '=')
        inheritThemes.erase(0, 1);
    else
        continue;
}
```

If `Inherits` starts at any nonzero position up to eight, character eight lies **inside that word**. The extracted suffix therefore starts with a letter, not optional whitespace followed by `=`. For example, the dossier's prefixed-key form leaves `s=...` and is rejected. `rfind` is an unclear way to express the intended prefix test, but the rest of this parser breaks the claimed injection chain. Empty suffixes are checked before `.at(0)` (`243-257`).

For an ordinary accepted `Inherits` key, values are split on commas and trimmed without rejecting separators or parent components (`263-278`). `themePaths()` uses string formatting to append these names under each library path (`290-308`), and `loadTheme()` passes resulting cursor directories to `loadAllFromDir()` (`113-133`). Parent components can therefore leave a nominal theme root if the resulting directory exists. An absolute-looking component does **not** replace the prefix automatically: these are formatted strings, not a filesystem path-join operation with absolute-path replacement semantics.

`XCursorManager.cpp:315-323` repeatedly rescans all discovered names until the set stops growing. Cycles alone converge because of the set; a long finite inheritance chain adds redundant scans, while no unbounded unique-name or processing budget is imposed. This is a potential local theme-loading performance problem, not a measured stat storm or established infinite loop for ordinary finite themes.

**Input chain/preconditions:** startup theme selection comes from `XCURSOR_THEME` (`src/pointer/cursor/CursorManager.cpp:103-118`), and selected-theme reload uses `m_theme`/the environment (`379-394`). `XcursorLibraryPath()` supplies the search roots (`XCursorManager.cpp:208-217`); an accepted theme's local `index.theme` supplies inheritance (`220-284`). A writable selected theme/search root or startup environment control is required. There is no title/app-ID route, filesystem permission bypass, or return of discovered file contents to a remote client. Theme writers can already supply cursor files and links, so an independent security escalation from nonconfinement is unproven.

**Corrections:** retain the path-confinement and redundant-scan observations, withdraw the malformed-key examples and automatic absolute-path reset. The dossier's directory paths/line numbers are stale; current path construction is at `297,303`. No traversal payload was installed or evaluated against the desktop.

**Minimal fix direction:** express the key test directly or use a desktop-entry parser with a validated group/key; maintain a visited set with a bounded traversal. Define whether inherited themes may contain path separators before enforcing confinement, since theme loading is already a user-controlled resource domain.

## F-RES-09

**Verdict: partially. Severity: Informational; optional loader hardening, not a verified memory-corruption vulnerability.**

The loader follows theme symlinks and retries on the same stream without rewinding. These facts do not establish the dossier's arbitrary-file privilege escalation, universal EOF assertion, or a libXcursor-based compositor overflow.

**Source-confirmed evidence:** `src/managers/XCursorManager.cpp:528-543` admits regular files and links, opens them with `fopen()`, and calls `XcursorFileLoadImages()`. Cursor themes conventionally use links for shape aliases; a link following normal filesystem permissions is not by itself a privilege-boundary bypass. The file still must parse as a cursor before `createCursor()` receives images. No arbitrary file data is returned to a client by this code.

```cpp
auto xImages = XcursorFileLoadImages(f.get(), size);
if (!xImages)
    xImages = XcursorFileLoadImages(f.get(), 24);
```

This retry appears at `XCursorManager.cpp:543-551`; there is no rewind. A failed call can consume stream data, but the source does not prove every failure ends exactly at EOF, or that a second call would have succeeded after rewinding. LibXcursor's requested size means a **preferred nominal size**, not an exact-size requirement.

**Executed benign control:** scratch `library-controls.cpp` saves a valid 2×2 cursor with nominal size 24 using libXcursor 1.2.3. Requesting nominal size 48 succeeds and leaves stream position 80. A second read without rewind returns null; after rewind it succeeds. This demonstrates stream-position dependence and nearest-size selection with a valid file. It does **not** exercise a malformed cursor, reproduce Hyprland's failure-only retry, or establish an availability/security impact. Output is in scratch `library-controls.txt`.

**Source-confirmed copy analysis:** `XCursorManager.cpp:191-197` allocates `size_t(width) * height` pixels, then copies the same product times `sizeof(uint32_t)`. There is no identified mismatch in the caller's allocation/copy counts on this 64-bit target. `/usr/include/X11/Xcursor/Xcursor.h:182` defines `XCURSOR_IMAGE_MAX_SIZE` as `0x7fff`; the installed manual also documents the format bounds. Those declarations alone are not a complete parser audit, but historical CVEs are likewise not evidence that this installed library returns internally inconsistent objects. The dossier provides no applicable library version, violated check, or reachable inconsistency proving the claimed overflow.

**Reachability/severity:** only selected local theme contents reach this path, through `themePaths()` and `loadTheme()` as described in F-RES-08. Normal client-supplied cursor surfaces use SHM, not this theme-file parser. No extra privilege is created merely by theme-author-controlled aliases. The original Low vulnerability claim is reduced to an informational record of behavior and possible reliability/hardening improvements.

**Minimal fix direction:** reopen or rewind before a meaningful retry, or remove a redundant size fallback after confirming library behavior. Preserve compatible aliases; if confinement is a product requirement, resolve links within a defined root rather than banning every symlink. An application-level image budget can improve resource predictability; unsupported claims of memory corruption should not be retained.

## F-RES-10

**Verdict: partially. Severity: Medium when the nondefault full-CM debug option is enabled; no default ICC-upload exposure.**

The raw ICC descriptor has no cleanup owner. Contrary to the dossier's unconditional dead-code statement, upstream can instantiate this creator under a configuration-controlled debug mode. Client ICC bytes still do not reach lcms.

**Source-confirmed evidence and gate:** `src/config/values/ConfigValues.cpp:738` defines `debug:full_cm_proto` as false by default and requiring restart. `src/managers/ProtocolManager.cpp:134-135,234-235` passes it to the color-management protocol; `src/protocols/ColorManagement.cpp:872-874` stores it. `ColorManagement.cpp:136-153` rejects creator creation **only when `m_debug` is false**:

```cpp
if (!PROTO::colorManagement->m_debug) {
    r->error(WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
             "ICC profiles are not supported");
    return;
}
```

When enabled, the same callback constructs `CColorManagementIccCreator`. The protocol is enabled by default via `render:cm_enabled` (`ConfigValues.cpp:657`) and allowed by the sandbox protocol whitelist (`ProtocolManager.cpp:399`). The client cannot enable the startup debug flag through an ordinary color-management request; local configuration/launch authority must first open this gate.

`ColorManagement.cpp:523-527` stores the received integer fd with no close or already-set test. `src/protocols/ColorManagement.hpp:102-124` stores it as `int fd = -1`, not `CFileDescriptor`, and declares no custom destructor. Both destruction (`ColorManagement.cpp:487,925-926`) and successful creator completion (`520`) erase the object without closing it. Libwayland's successful fd delivery transfers a descriptor to the implementation; a raw integer member neither closes it nor closes previously overwritten descriptors. Contrast the explicit received-fd ownership at `src/protocols/core/Shm.cpp:186-187`.

**Source-confirmed chain/preconditions:** with color management and the startup debug option enabled, an ordinary connected client can obtain this correctly typed creator; its ICC-file callback then retains received descriptors without cleanup. Repeated setting is also not rejected by the implementation. The protocol's semantic `already_set` requirement (`/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml:1013-1014`) is not an automatic libwayland argument check. Even a single valid set followed by creator destruction lacks cleanup. This is descriptor retention/exhaustion in a nondefault mode, not a demonstrated default-session crash. No descriptor flood was executed.

**Source-confirmed ICC parsing boundary:** `ColorManagement.cpp:489-518` only checks a stored fd/length and logs `FIXME: Parse icc`; it does not map/read the fd or call lcms. Native local-file parsing instead follows monitor-rule `parseICC()` (`src/config/shared/monitor/Parser.cpp:277-283`) → `src/output/Monitor.cpp:653` → `src/helpers/cm/ICC.cpp:277-295`. `ICC.cpp:17-33` reads that configuration-selected file without an application size cap, then `287` opens it with lcms. This is a configuration-selected file boundary, not client ICC upload. There is no justification to infer a current lcms exploit from historical CVEs.

**Corrections:** the dossier's line numbers reflect the fork; the default rejection is conditional upstream, so “unreachable” and “only leaks after future enablement” are wrong. Its suggested 10 MB/display-only parser policy does not match the installed protocol's 32 MB and Display-or-ColorSpace requirements (`color-management-v1.xml:987-1008`). Its broad claim that VCGT boundaries are all self-consistent should not be carried forward: `ICC.cpp:80-81` permits a one-entry table, while `129-131` unconditionally indexes entries 1 and `entries - 2` in logging arguments. This is a source-level counterexample to that side assurance, not a newly expanded client-upload finding.

**Minimal fix direction:** give received descriptors scoped ownership from callback entry, reject duplicate sets while safely disposing of the newly received fd, and close the retained owner on every lifecycle path. Keep the unsupported/default gate until parsing and protocol validation are implemented. Bound local file reads independently and review VCGT logging's short-table accesses. Do not add client-to-lcms claims to this record.

## F-RES-11

**Verdict: partially. Severity: Informational for this compositor; Low dependency cleanup defect when explicitly sized SVG loading is used.**

The SVG decoder leaks owners on error, but the claimed unrestricted local-file/network/XXE primitive is contradicted by the library contract. Upstream's only native image-resource call also does not supply the positive target size required to enter this decoder's rendering path.

**Source-confirmed evidence and policy guard:** `HG:src/image/formats/Svg.cpp:47,80` uses `rsvg_handle_new_from_data()`, without setting a base URI or unsafe parser flags. The installed librsvg 2.62.4 primary API documentation explicitly says that this constructor has **no base URL and no flags** (`/usr/include/librsvg-2.0/librsvg/rsvg.h:1104-1112`). The resource-loading rules (`134-163`) prohibit non-data references without a base URL and prohibit HTTP loading; file references with a base are confined to that directory/subdirectories with canonicalization. Embedded `data:` resources remain allowed (`129-130`), which is not arbitrary local-file disclosure or network access. Parser-limit disabling requires `RSVG_HANDLE_FLAG_UNLIMITED` (`952-957`); this call does not request it.

The absence of an application-side “deny external resources” switch does not override these library guards. No evidence demonstrates unrestricted entity expansion or an XXE bypass in the examined library. The dossier's proposed replacement constructor/flag/property names were not established by the installed API and should not be copied into a fix.

**Source-confirmed cleanup defect:** `HG:src/image/formats/Svg.cpp:32-55` creates a surface and Cairo context before file-read/parse/render failures can return. Those returns bypass the successful cleanup at `58-60`. The data variant has the same problem (`69-88` versus `91-93`). Parse/render errors can also populate `GError*`, which is not freed. Scoped cleanup is absent even though the source includes a scope-guard header.

```cpp
RsvgHandle* handle = rsvg_handle_new_from_data(..., &error);
if (!handle)
    return std::unexpected("loading svg: rsvg failed to read data");
```

**Native reachability guard:** the only `CImageResource` construction in upstream is the path-only wallpaper call (`src/render/Renderer.cpp:1519`). `HG:include/hyprgraphics/resource/resources/ImageResource.hpp:29-31` stores a default-constructed target size; `HG:src/resource/resources/ImageResource.cpp:12-13,24-25` passes it to `CImage`. A default `Hyprutils::Math::Vector2D` is `{0,0}` (`/usr/include/hyprutils/math/Vector2D.hpp:18-22`). Both SVG decoder variants reject dimensions below one **before allocating/rendering** (`Svg.cpp:29-32,66-69`). No native explicitly sized SVG call was found, so replacing a wallpaper with SVG content does not close the asserted failure/rendering chain. Lock assets use Cairo's PNG-only path.

**Preconditions/severity and corrections:** a separate library consumer or in-process plugin must call the explicitly sized SVG API to reach the cleanup defect. Such a plugin is already trusted code in this process. Keep the real dependency error-path ownership defect, but withdraw the unrestricted file/HTTP/XXE allegation and the assertion of a currently reachable client SVG loading primitive. This conclusion is source/API-contract based; no external reference, network request, entity-expansion sample, or SVG failure crasher was executed.

**Minimal fix direction:** give surface/context/handle/error objects scoped owners before any return; preserve librsvg's default resource policy and XML limits. A future explicitly sized API consumer should define its asset trust and memory budget. No unverified external-resource-disable API is needed to address this record.

## F-RES-12

**Verdict: partially. Severity: Informational control record, not an independent vulnerability.**

The principal negative results hold: window titles use plain text, native image loading has fixed local asset names, ICC upload does not reach lcms, and image format dispatch uses content. The dossier overstates the completeness of SHM arithmetic hardening, default-directory ownership, and several renderer/dependency call relationships.

**Source-confirmed control evidence and broken client chains:**

- **Titles:** `src/render/decorations/CHyprGroupBarDecoration.cpp:292,314-318` supplies `metadata().title()` to the string overload. `src/render/Renderer.cpp:1653-1671` invokes `pango_layout_set_text()`, so markup-looking client titles are displayed as text; this route neither builds a file path nor invokes the async markup parser. The async overload has only the error-overlay and static debug-warning call sites (`src/errorOverlay/Overlay.cpp:125-133`; `src/debug/Overlay.cpp:368-374`). Notifications now also use the plain-text overload (`src/notification/NotificationOverlay.cpp:103-111`), contrary to the dossier's markup list. A source search found no native client-title/app-ID-to-`CImage`/`CImageResource` construction. This is a scoped negative result, not a guarantee about arbitrary plugins.
- **File names:** `Renderer.cpp:1489-1509` bounds the chosen default wallpaper to three fixed names and resolves a fixed asset list (`src/render/types.hpp:15-21`; `Renderer.cpp:1536-1554`). `DATAROOTDIR` is a build setting (`CMakeLists.txt:664`), not proof of root ownership; the source also permits symlinks. Lock-death assets use direct Cairo PNG decoding (`Renderer.cpp:1557-1574,1723-1725`), not HG image-resource dispatch. Normal title/app-ID strings do not reach either asset resolver.
- **ICC:** `src/protocols/ColorManagement.cpp:136-153,489-527` implements the conditional debug gate and placeholder creator; neither the default nor debug path parses a client's ICC bytes with lcms. Local monitor-rule files reach lcms through `src/config/shared/monitor/Parser.cpp:277-283`, `src/output/Monitor.cpp:653`, and `src/helpers/cm/ICC.cpp:282-295`. The default rejection is real, but unconditional creator unreachability is not; see F-RES-10.
- **SHM range checks:** `src/protocols/core/Shm.cpp:144-156` rejects unknown formats, nonpositive dimensions/stride, negative offset, and invalid layout. `src/helpers/Format.cpp:71-80` checks multiplication/addition overflow for `stride * height + offset` and bounds that interval within the recorded pool size. `src/protocols/core/Compositor.cpp:805,810-824` intersects cursor damage and sizes its destination to the returned buffer length. These are meaningful spatial controls; they do not prevent F-RES-07's backing-store fault.
- **SHM assurance limit:** the minimum-row-stride check depends on `Hyprgraphics::Egl::minStride()` (`Format.cpp:64-68`). In the authenticated dependency, `HG:src/egl/Egl.cpp:279-280` multiplies width by `uint32_t bytesPerBlock` **before** returning `int`; that product is not checked wide arithmetic (`HG:include/hyprgraphics/egl/Egl.hpp:30-38`). Casting its result to `size_t` in the caller is not a proof that every input width has a correct minimum stride. The partial-row cursor path also uses signed intermediate expressions (`Compositor.cpp:822-823`). Therefore the dossier's blanket proof of “complete layout validation, no arithmetic issue, only residual SIGBUS” is not accepted. No extreme-dimension client input was constructed, and this control record does not claim a separately reproduced overflow.
- **Content dispatch:** `HG:src/image/utils/Format.cpp:10-26,49-74` maps MIME results from `magic_file()` to known formats and returns an error otherwise. It uses `MAGIC_MIME_TYPE | MAGIC_SYMLINK`, loads a magic database per call, and closes the handle with a scope guard. This rejects stable empty files before JPEG/WebP decoder entry, as discussed above, and means changing a suffix does not select a decoder. It does **not** authenticate files, validate every image structure, or provide a race-free snapshot between sniffing and reopening. The format enumeration/dispatch (`HG:include/hyprgraphics/image/Image.hpp:10-20`; `HG:src/image/Image.cpp:70-86`) contains no GIF handler. There is no measured per-load millisecond cost in this verification; the performance number is withdrawn.
- **X11 icons:** `_NET_WM_ICON` is declared as an atom (`src/xwayland/XWayland.hpp:80`), but no reader routing it to the audited decoder was found. An atom declaration is not an icon-loading path. No native `xdg_toplevel_icon` image-file loading implementation was found in this tree.

**Executed controls:** the benign library probe confirms the local PNG limits, Cairo error-surface contract, libheif defaults, and libXcursor closest-size/stream behavior; `file --mime-type empty-file` reports an empty MIME type. These establish API/control facts only. No compositor/client exploit, desktop action, leak stress test, visual result, or device validation was performed.

**Minimal follow-up direction:** preserve the plain-text and fixed-asset boundaries; document the debug ICC gate accurately; validate minimum stride with checked arithmetic before claiming complete SHM layout protection. Keep content identification distinct from decoder safety, and require evidence for runtime/performance assertions.

## Verification limits and disposition

All 12 dossier IDs, including the control record, have been evaluated: **3 confirmed, 9 partially, 0 rejected, 0 inconclusive**. A partial verdict retains only the explicitly identified defect or factual behavior; it does not validate the discarded attack mechanism. F-RES-09 and F-RES-12 are informational records rather than additional confirmed security vulnerabilities.

Priority is the directly client-reachable missing SHM fault containment (F-RES-07), followed by async completion synchronization (F-RES-06) and dependency memory/ownership repair under the stated file boundaries. ICC descriptor ownership is reachable in a nondefault debug configuration and should not be left as dead-code-only cleanup. Dependency evidence is tied to the authenticated locked tree, not assumed from the installed package's version string.

Scratch evidence is confined to `/home/wilf/data/.claude-scratch/agent-reports/work/11-resource-loading/`: authenticated dependency export, `dependency-provenance.json`, `provenance-checks.txt`, `library-controls.cpp`, `library-controls`, `library-controls.txt`, `library-versions.txt`, and benign cursor/empty-file fixtures. The two example reports named by the brief were absent at their specified paths; this report follows the brief's structure directly. No upstream source, dossier, README, system file, desktop state, or Git state was changed. No GitHub interaction occurred.

Repository guidance for any later upstream interaction: consult the [Hypr AI policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md) and [issue guidelines](https://wiki.hypr.land/contributing-and-debugging/issue-guidelines/). The local `AGENTS.md` warns that prohibited AI interaction can lead to severe account sanctions, including an organization ban. This document is a local verification report, not a filed issue or PR.
