# hypr-bug

An agent-maintained issue list for Hyprland, covering the areas I am
interested in. Each entry is a self-contained dossier: audit report,
verification status per finding, and — where applicable — a compilable
reproduction client.

Everything here was produced by AI agents under my supervision: static review
against the relevant specs (Wayland protocol XML, color standards), live
verification in nested/headless sessions, and code reading at pinned commits.
**Treat every claim as machine-generated until upstream confirms it** — each
dossier states its own verification level per finding.

## Index

| # | Area | Summary | Status | Files |
|---|------|---------|--------|-------|
| 1 | HDR / color management (`cm/tonemap-ref-scaling`) | 29-item audit of `color-management-v1` handling; includes 2 Critical always-crash protocol bugs with repro clients | verified (nested-session crashes observed); fix not yet proposed upstream | [`issues/2026-10-color-management-crashes.md`](issues/2026-10-color-management-crashes.md), [`repro/cm-crash-repro/`](repro/cm-crash-repro/) |

*(To be continued as more areas get audited.)*

## Using the reproductions

Each `repro/<name>/` directory is standalone: `bash build.sh` generates the
needed Wayland protocol stubs and compiles the client. Read the linked issue
dossier first — it describes the trigger sequences and the expected crash
signature.

⚠️ Reproductions crash a vulnerable session with unsaved data. Run them
against nested/headless instances only, and keep this repository private
until the corresponding fixes are merged upstream.

## Methodology notes

- Findings are pinned to an exact branch + commit; line numbers refer to that tree.
- Severity was assigned by reachability (any client → whole compositor = Critical).
- Where a claim could not be independently confirmed, it is marked as such in the dossier.

## Disclosure

When reporting upstream, follow Hyprland's
[AI usage policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md)
and the [issue guidelines](https://wiki.hypr.land/contributing-and-debugging/issue-guidelines/):
all interaction with the development team must be done by a human, not by an
agent.
