# REV-04 Image-description reference objects resolve to the wrong type

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-04 — Low (latent)

`src/protocols/ColorManagement.cpp:778` `fromReference` reads a `wp_image_description_reference_v1` resource's data as `CWpImageDescriptionV1`, and nothing ever calls `setData` on reference objects, so `get_image_description` from a reference always fails. Latent today because Hyprland never creates reference objects.

Fork status: fixed (`ColorManagement.cpp:725,751-753`).

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
