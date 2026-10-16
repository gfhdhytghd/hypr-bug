# F-RES-11 SVG external references and error-path cleanup

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Explicitly sized SVG failures leak; native zero-size guard and librsvg policy break claimed file/network/XXE reachability.

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

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
