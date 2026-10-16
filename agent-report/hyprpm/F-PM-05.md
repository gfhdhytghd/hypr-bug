# F-PM-05 Recursive submodule fetching trusts repository configuration

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Recursive fetch exists, but Git blocks ext by default and confirmation precedes clone; historical Git vulnerabilities require separate version-specific evidence.

**Claim.** Merely putting an external-helper submodule URL or a historical Git exploit pattern in a repository yields code execution during recursive cloning, outside confirmation.

**Evidence and breaking guard — source-confirmed.** Recursive operations do exist at `hyprpm/src/core/PluginManager.cpp:217,230,293,724,866`, and there is no hyprpm-specific submodule-URL audit. However, no command sets a Git protocol policy that enables the dangerous external transport. The installed dependency is Git 2.56.0 (version query executed; no repository clone was executed). Its primary documentation, `/usr/share/man/man1/git-config.1.gz`, decompressed lines `10039–10081`, specifies safe transport defaults, `ext = never`, and a `user` policy preventing noninteractive recursive use of other transports such as `file`. `/usr/share/man/man1/git-submodule.1.gz:154–163` additionally states custom submodule update commands are not copied from `.gitmodules` for security.

At `PluginManager.cpp:186–189`, the confirmation guard is:

```cpp
if (input.size() > 0 && input[0] != 'Y' && input[0] != 'y') {
    std::println(stderr, "Aborting.");
    return false;
}
```

The subsequent Git invocation delegates transport policy to Git; lack of a redundant hyprpm denial does not remove Git's default denial.

**Call chain and reachability — source-confirmed.** Local add request → ABI/dependency/state gates described in F-PM-01 → repository-author trust prompt (`178–189`) → recursive clone (`217`) → Git's protocol decision → refusal of the dossier's external transport under default policy. Update lacks another repository confirmation but uses the same dependency policy (`724`). A repository's `.gitmodules` alone does not change the caller's global protocol allowance. A user or administrator who already opted into permissive Git policies changes this precondition; hyprpm is not shown to do so.

**Severity and corrections.** No independently confirmed hyprpm vulnerability for the asserted trigger. Clone precedes manifest review, but it follows explicit repository trust confirmation, contrary to the title's claim. Reliance on CVE-2024-32002 would require an affected Git/platform/configuration and an exact reproduction against that dependency, not merely a recursive-clone call. This report neither assumes every installed Git is patched nor attributes an untested dependency CVE to e2ad066c9. A custom unsafe policy would warrant a separate conditional assessment, not this unconditional High finding.

**Minimal fix direction.** Defense in depth may pin conservative protocol policies and require a supported patched Git. Such hardening should not be presented as proof that Git's existing defaults allow the proposed attack.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
