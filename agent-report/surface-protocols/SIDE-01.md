# SIDE-01 get_subsurface on an orphaned subsurface parent dereferences null in t1Parent (compositor SIGSEGV)

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: historical evidence at the revisions below; current-version and physical-display acceptance must be established separately.

## Agent report

Official baseline: `e2ad066c9`; fork comparison: `bd598253a`.

SIDE-01 — High — `get_subsurface` on an orphaned subsurface parent → null dereference in `t1Parent()` → compositor SIGSEGV — Executed (SIGSEGV) — fixed `ada0711d4`

[Original finding, evidence, validation scope and fork status](_evidence/fork-fix-side-findings.md#side-01).

[Existing agent reproduction artifacts](_assets/subsurface-orphan-parent/). No human acceptance is recorded.

## Tracker notes

Repro: [subsurface-orphan-parent](_assets/subsurface-orphan-parent)
