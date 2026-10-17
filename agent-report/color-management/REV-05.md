# REV-05 Protocol bookkeeping: v1-only preferred_changed sent to v2, stale preferred id, uninitialised member

- Agent confirmation: `confirmed` (source review).
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: static source evidence at the revisions below; runtime and physical-display acceptance must be established separately.

## Agent report

Official baseline: `25f09b26f`; fork comparison: `e5a03b6a5`.

REV-05 — Low

(a) `src/protocols/ColorManagement.cpp:891` sends the deprecated `preferred_changed(0)` to every feedback including v2 clients (which should get `preferred_changed2`); (b) `m_currentPreferredId` is updated only in `get_preferred_parametric`, not `get_preferred`; (c) `src/protocols/ColorManagement.hpp:73` `m_hdrMetadata` is left uninitialised (project guideline violation). Also the "destroy previous description" branch at `:256` is dead because `m_imageDescription` is never assigned.

Fork status: fixed / removed.

[Batch overview](../render/_evidence/upstream-review-2026-10-17.md).
