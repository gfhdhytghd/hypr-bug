# Hardware validation on Dell XPS, 2026-10-17

Setup:

- Hardware: Dell XPS (Intel Xe2, `xe` driver), eDP LGD 0x0778 panel, 2880x1800, scale 2. The panel's EDID DisplayID block advertises BT2020RGB + SMPTE ST2084 and a desired content max of 530 nits.
- Software: Hyprland fork `main` `e38076ac9`; aquamarine fork `drm-color-pipeline` `8625b68` (SOVERSION 15).
- Settings: `cm = hdr` preset.
- Method: `HDR_OUTPUT_METADATA` was read from the connector with `modetest -M xe -c` and decoded as `struct hdr_output_metadata`.
- Test client: a small EGL client goes fullscreen with a PQ description. It copies the output's primaries and luminances and adds its own mastering metadata: P3-D65, 0.0001–1000 nits, maxCLL 1000, maxFALL 400.
- Direct scanout needed two runtime changes on this setup. Hardware cursors had to be enabled (`cursor:no_hardware_cursors = 0`). The user's quickshell overlay layers had to be stopped. Both were restored afterwards.

## REV-08 — DS HDR metadata

| State | DS | HDR_OUTPUT_METADATA |
|---|---|---|
| idle (composited) | 0 | PQ, panel primaries, maxML 530, minML 0.0005, CLL 0, FALL 0 |
| client fullscreen | active | PQ, P3-D65, maxML 1000, minML 0.0001, CLL 1000, FALL 400 |
| fullscreen toggled off | 0 | output default |
| fullscreen again | active | surface metadata |
| notification overlay shown | 0 | output default |
| overlay gone | active | surface metadata |
| client closed | 0 | output default |
| static client: 1–2 frames, still in the fullscreen transition | 0 | output default |
| static client: 3 frames then idle | active | surface metadata, persists with no further commits |

Metadata always matches the DS state, including for a client that stops committing right after DS entry.

## REV-10 — popup pass

hyprtester on the XPS (nested Wayland backend works there; on the desktop machine it aborts with a 0x0 framebuffer):

- `tiledPopupsSurviveFocusChanges`, `popupOpacityInheritsParentFade`, `popupRespectsPassRedirection`: 3/3 pass.
- Negative control: re-adding `w.reset()` after `renderWindow` in `renderWorkspaceWindows` makes `tiledPopupsSurviveFocusChanges` fail with `Expected parent and both popups exactly once, got 1/0/0`.

## Other

- Unit tests (`ctest`, Debug build on the XPS): 740/740.
- The composited HDR path (PQ client and mpv OpenGL windowed, tonemapped to the 530-nit panel) shows no visual artifacts after the REV-09 shader change.
- New finding: [XPS-01](../XPS-01.md).
