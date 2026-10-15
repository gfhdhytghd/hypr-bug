# Agent reports

Use `<category>/<issue-id>.md`: one Markdown report for each evaluated issue. Include the claim, pinned revision, evidence, automated reproduction/test results, corrections, and limitations. A report may conclude `confirmed`, `partially`, or `rejected`; its presence does not imply human confirmation.

Preserved shared reports and the community triage ledger are in category `_evidence/` directories. Existing agent-generated/run reproduction code and results are in category `_assets/` directories and are linked from individual issue reports. Pending candidates stay in `../pending-evaluation/`.

Human-reviewed and tested material belongs in `../human-confirmed/<category>/<issue-id>/` only after meeting that directory's requirements. Do not relabel agent-only evidence as human-confirmed.
