# F-RES-09 Cursor loading symlink and retry behavior

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Symlinks and unrewound retry exist; no privilege escalation, universal EOF failure, or applicable libXcursor overflow is established.

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

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
