# hypr-bug — Hyprland wp_color_management v1 crash reproductions & audit

Audit artifacts for the Hyprland branch `cm/tonemap-ref-scaling`
(tip `30e438818`, based on main `6cc0822e2`): a compilable reproduction client
for **two Critical SIGSEGV bugs** in the compositor's `color-management-v1`
protocol handling, plus the full 29-item audit verification report.

## Layout

- `cm-crash-repro/` — reproduction client (`build.sh` produces `./repro`)
- `reports/hyprland-hdr-cm-crash-reports.md` — crash deep-dives + full verification table

## The two guaranteed crashes (any plain client request sequence takes down the compositor)

1. **feedback never becomes inert when its wl_surface dies**
   Client creates a `wp_color_management_surface_feedback_v1`, then destroys
   its own `wl_surface`. On the next output image-description change,
   `onPreferredChanged` dereferences the expired `WP<CWLSurfaceResource>`
   with no guard (`ColorManagement.cpp:460`) → main-thread SIGSEGV.
   (Spec: XML L795-799 — feedback must become inert when the surface is destroyed.)
2. **A failed (not-ready) wp_image_description_v1 remains usable**
   After the output is removed, `get_image_description` returns an object that
   got `failed`, yet it is never removed from the protocol list, carries an
   empty `m_settings` weak pointer, and no `not_ready` error is ever posted:
   - A: calling `get_information` on it → null deref at `ColorManagement.cpp:767`
     crashes the main thread;
   - B: passing it to `set_image_description` poisons the surface; the render
     loop's `isHDR()` (`Renderer.cpp:2571` → `ColorManagement.cpp:378`)
     crashes the render thread.

## Usage

⚠️ Crashes the target hyprland session (unsaved data is lost). Use a nested /
test session only. The compositor needs `render:cm_enabled=1` (default-on in
this branch).

```bash
cd cm-crash-repro && bash build.sh

# crash #1
./repro feedback
#   in another terminal: hyprctl keyword monitor <output>,add,cm,hdr  → Enter

# crash #2A (main thread)
hyprctl output create headless VICTIM
./repro getinfo VICTIM
#   in another terminal: hyprctl output destroy VICTIM → Enter

# crash #2B (render thread)
hyprctl output create headless VICTIM
./repro poison VICTIM
#   in another terminal: hyprctl output destroy VICTIM → Enter
```

Success criteria: the hyprland process dies with `onPreferredChanged` (#1) or
`CColorManagementImageDescriptionInfo` / `isHDR` (#2) at the top of the crash
stack. After the fixes (guards listed in section 6 of each report), the same
sequences must produce `inert` / `not_ready` / `image_description` protocol
errors instead of crashes.

## Disclosure note

Findings come from static review against the protocol spec plus live
verification in nested sessions (see `reports/`). Keep this repo private until
the fixes are merged. If reporting upstream, follow Hyprland's
[AI usage policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md)
and issue guidelines, and interact with the developers yourself.
