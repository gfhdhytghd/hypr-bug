# hdr-com-04-sdr-headroom — probe the color-management headroom an output advertises

`cminfo` is a **read-only** `wp_color_manager_v1` client. It binds the output's
`wp_color_management_output_v1`, gets the image description, calls
`get_information`, and prints the advertised `luminances` (min/max/reference) and
`target_luminance` (min/max) plus `target_max_cll` / `target_max_fall`. It sends
no state and changes nothing.

```
bash build.sh
WAYLAND_DISPLAY=wayland-1 ./cminfo eDP-1
```
The bug signal for HDR-COM-04: on an **SDR** preset (`wide` / `edid`),
`target_luminance.max` ≫ `luminances.reference` — the SDR output advertises the
panel's HDR peak as headroom.

## Empirical reproduction — EXECUTED

Real running **stock upstream Hyprland 0.56.2** (Nix build `efb5099378`), LG eDP
OLED, `XRGB2101010` (10-bit). The preset was switched at runtime with
`hyprctl eval 'hl.monitor{ ... cm="<preset>" ... }'` and `cminfo` re-read each
time; the config was restored with `hyprctl reload` afterwards.

| preset | transfer | `luminances` reference | `target_luminance.max` | headroom | verdict |
|---|---|---|---|---|---|
| `srgb` | gamma (SDR) | 80 | **80** | 1.0× | correct — no headroom |
| `wide` | gamma (SDR) | 80 | **530** | 6.6× | **bug — fake headroom** |
| `edid` | gamma (SDR) | 80 | **530** | 6.6× | **bug — fake headroom** |
| `hdr`  | PQ (HDR)    | 203 | 530 | 2.6× | correct — real HDR headroom |

The two SDR presets (`wide`, `edid`) advertise the panel's 530 cd/m² mastering
peak as `target_luminance.max` while their reference white is 80 cd/m²; `srgb`
(excluded by `applyCMType`) does not. This is the exact mechanism in #13418 /
PR #16525 (the PR author's panel reports a 1060-nit EDID peak → 13.25× ; here the
monitor config caps `max_luminance` at 530 → 6.6×). Color-managed clients
(Chromium/Electron) read the headroom and dim SDR white.

Agent evidence, not human-confirmed. Finding: [`../../HDR-COM-04.md`](../../HDR-COM-04.md).
