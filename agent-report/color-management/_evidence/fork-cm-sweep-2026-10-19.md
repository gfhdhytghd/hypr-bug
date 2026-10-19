# Fork CM/HDR sweep 2026-10-19

Five parallel Codex agents audited the fork at `b0136905e` (shader math, color-management protocol, render-intermediate description bookkeeping, output/KMS incl. aquamarine `8625b681e`, config/ICC/fork regressions), skipping everything already tracked here. Each reported finding was then re-verified by Claude against the source; one further defect (CMS-09) was found during that verification. No fork-introduced regression was found; all 13 are present upstream at `12f242511`.

| Issue | Area | Severity | Fork fix |
|---|---|---|---|
| [CMS-01](../CMS-01.md) Synchronized subsurface image descriptions take effect before the parent commit | protocol | Medium | not fixed (architectural) |
| [CMS-02](../CMS-02.md) get_preferred hands out the output's ICC description, which surfaces then decode as gamma 2.2 | protocol / ICC | Medium | `57dc260a2` (partial) |
| [CMS-03](../CMS-03.md) ICC 3D LUT sampled uniformly in linear light crushes shadows | ICC / shader | Medium | `ab41322e6` |
| [CMS-04](../CMS-04.md) Soft ICC change or removal keeps the previous profile's VCGT | config / ICC | Medium | `db14ca3b7` |
| [CMS-05](../CMS-05.md) Changing only sdr_eotf is ignored on reload | config | Medium | `db14ca3b7` |
| [CMS-06](../CMS-06.md) Config reload never re-reads an unchanged ICC path or the VCGT setting | config / ICC | Low | `db14ca3b7` |
| [CMS-07](../CMS-07.md) Downscaled built-in wallpaper loses its encoding and can clip | render intermediates | Medium | `e04ff4074` |
| [CMS-08](../CMS-08.md) Background clears never update the SDR mirror attachment | render intermediates | Medium | `e04ff4074` |
| [CMS-09](../CMS-09.md) Clear colors (misc:background_color) are written unconverted into HDR / linear work buffers | render intermediates | Medium | `e04ff4074` |
| [CMS-10](../CMS-10.md) Groupbar gradient textures are untagged and follow the SDR EOTF policy | decorations | Low | `9171f2d62` |
| [CMS-11](../CMS-11.md) Border gradients re-encode with gamma 2.2 what was decoded as piecewise sRGB | decorations / shader | Low | `9171f2d62` |
| [CMS-12](../CMS-12.md) Withdrawing HDR support leaves PQ HDR_OUTPUT_METADATA over SDR pixels | output / KMS | Medium | `9736f2fe6` |
| [CMS-13](../CMS-13.md) CTM interop fallback disables required shader CM | output / KMS | Medium | `5bcfb19b9` |

Fork fixes: `ab41322e6`, `db14ca3b7`, `e04ff4074`, `9171f2d62`, `9736f2fe6`, `5bcfb19b9`, `57dc260a2` (fork `main` `57dc260a2`). Unit tests: 763 passing; shader variants (surface / border × ICC × tonemap × mirror × source TF, 32 total) validated with glslang. No runtime or physical-display validation.
