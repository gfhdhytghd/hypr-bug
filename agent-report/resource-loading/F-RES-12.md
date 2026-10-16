# F-RES-12 Control record: resource-loading negative results (not a defect)

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Plain-text titles and fixed content-sniffed assets hold; complete SHM arithmetic assurance, unconditional ICC unreachability, and ownership assumptions do not.

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

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
