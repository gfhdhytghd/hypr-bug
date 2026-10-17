# XPS-01 Strict v1 CLL/FALL validation kills Mesa Vulkan WSI clients (`set_max_fall(0)`)

- Agent confirmation: `confirmed` (runtime, Dell XPS / Intel Xe2, fork `e38076ac9`).
- Human confirmation: `none`.
- Scope: fork only. The check was added by fork commit `dcc8b1da1` ("protocols/cm: validate content light levels when creating descriptions"); upstream does not perform it.

## Observation

`mpv --vo=gpu-next` (Vulkan, Mesa 26 WSI) playing an HDR10 clip is disconnected immediately:

```
{mesa vk display queue} -> wl_registry.bind(66, "wp_color_manager_v1", 1, ...)
-> wp_image_description_creator_params_v1#92.set_primaries_named(6)
-> wp_image_description_creator_params_v1#92.set_tf_named(11)
-> wp_image_description_creator_params_v1#92.set_max_cll(703)
-> wp_image_description_creator_params_v1#92.set_max_fall(0)
-> wp_image_description_creator_params_v1#92.set_mastering_display_primaries(...)
-> wp_image_description_creator_params_v1#92.set_mastering_luminance(0, 703)
-> wp_image_description_creator_params_v1#92.create(new id wp_image_description_v1#93)
wl_display#1.error(nil, 5, "Content light levels are outside the permitted range")
```

## Analysis

`src/protocols/ColorManagement.cpp` (creator `create`) raises `invalid_luminance` for a version-1 creator when max_fall <= min L of the mastering range. Mesa binds `wp_color_manager_v1` at version 1 and forwards `VkHdrMetadataEXT.maxFrameAverageLightLevel = 0`, which CTA-861.3 defines as "unknown", as `set_max_fall(0)`.

The color-management-v1 text for version 1 does require the error (`max_fall` must be greater than min L), so the compositor is spec-compliant and the Mesa WSI is at fault. In practice, though, every Mesa-Vulkan HDR client on a v1 binding is killed.

## Options

1. Keep strict validation and report to Mesa: the WSI should omit `set_max_cll`/`set_max_fall` when the value is 0.
2. Treat 0 as "unset" (CTA-861.3 semantics) before the v1 range check. This deviates from the literal v1 text but matches what v2 relaxed.
