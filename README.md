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

| # | Area | Summary | Status | Reports | Repro clients |
|---|------|---------|--------|---------|---------------|
| 1 | HDR / color management (`cm/tonemap-ref-scaling`) | 29-item audit of `color-management-v1` handling; 2 High always-crash protocol bugs | verified (nested-session crashes observed); no upstream fix yet | [overall table](reports/color-management-verification.md) | [cm-feedback-inert](repro/cm-feedback-inert/) · [cm-failed-desc](repro/cm-failed-desc/) |
| 2 | Full-codebase sweep @ `397cb0efb` — fork CM/HDR, IPC/hyprctl, hyprpm, config/Lua/ICC, XWayland, xdg/layer-shell, misc protocols | 52 findings across 7 dossiers; top class: SIGBUS any-client crashes, IPC Lua `eval`/`dispatch` injection (fork regression), X11 format-confusion heap OOB read, popup UAF / self-parent freeze, hyprpm `sh -c` injection + sudo path traversal | **static analysis only — none reproduced yet** | [batch index](pending-evaluation/2026-10-full-audit/README.md) | — |

Raw agent output awaiting triage (pre-verification snapshots) is parked in
[`pending-evaluation/`](pending-evaluation/).

*(To be continued as more areas get audited.)*

## Using the reproductions

Each `repro/<name>/` directory is one bug: a standalone client
(`bash build.sh`) plus its `REPORT.md` deep-dive (trigger analysis, spec
citations, fix proposal). The overall audit status for the whole area lives in
`reports/`.

⚠️ Reproductions crash a vulnerable session with unsaved data. Run them
against nested/headless instances only, and keep this repository private
until the corresponding fixes are merged upstream.

## Methodology notes

- Findings are pinned to an exact branch + commit; line numbers refer to that tree.
- Severity was assigned by reachability (any client → whole compositor = High).
- Where a claim could not be independently confirmed, it is marked as such in the dossier.

## Disclosure

When reporting upstream, follow Hyprland's
[AI usage policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md)
and the [issue guidelines](https://wiki.hypr.land/contributing-and-debugging/issue-guidelines/):
all interaction with the development team must be done by a human, not by an
agent.
