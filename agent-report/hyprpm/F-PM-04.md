# F-PM-04 Plugin sources lack integrity / revision enforcement

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unauthenticated transports and unsigned updates are accepted; fixed revisions receive resets, Git checks objects, and ext execution is disabled by default.

**Claim.** Fetches do not authenticate publisher signatures or enforce the previously stored hash as a lock; the dossier adds automatic external-helper execution and unconditional malicious plugin loading.

**Evidence — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:217,724` passes user/stored URLs to `git clone --recursive` without a transport allowlist or signature-verification operation. `PluginManager.cpp:744–750` compares fetched HEAD with `repo.hash` to decide whether to rebuild:

```cpp
update = update || hash != repo.hash;
```

It is an update detector, not an authentication check. Build steps from the fetched manifest are executed as the invoking user (`Manifest.cpp:114–118`; `PluginManager.cpp:834–842`). Privileged copying of an output is separate (`DataState.cpp:106–107`); root ownership of the installed plugin is not evidence that the build ran as root.

**Call chain and preconditions — source-confirmed.** Initially confirmed URL → state URL (`PluginManager.cpp:163–189,372`; `DataState.cpp:98`) → local update invocation and authorized privilege operations (`main.cpp:136–147`) → header check (`PluginManager.cpp:672–676`) → URL clone (`724–730`) → stored-revision handling and update comparison (`732–759`) → manifest validation and matching compatibility pin (`768–815`) → build steps (`834–842`). A network attacker must control the repository delivery channel actually used: plain HTTP/Git transport or compromised trusted source. Merely being on the same network does not establish control of official HTTPS or SSH. Unpinned repositories using an unauthenticated transport remain exposed to substituted HEAD/build content.

**Guards and corrections — source-confirmed.** A nonempty stored revision is reset before the hash comparison (`732–741`), and a matching manifest compatibility pin is hex-checked and reset (`790–811`). The reset error handling is narrow and output-text-based; it is not robust end-to-end revision verification, but the claim that revisions are never enforced is wrong. A hash changing during an update is intended behavior; comparing new HEAD for equality with old HEAD would disable updates, not authenticate them. Git's content-addressed object checks should not be conflated with publisher authentication. Trusting a newly fetched repository's own pin cannot independently authenticate that repository.

The installed Git 2.56.0 manual (`/usr/share/man/man1/git-config.1.gz`, decompressed lines `10039–10081`) specifies `ext` as disallowed by default, with dangerous-policy overrides requiring external configuration. No hyprpm command shown enables that transport. The claimed default external-helper RCE is rejected. Optional hardening is distinct from a missing default Git guard.

**Load endpoint — source-confirmed.** `main.cpp:158` reconciles load state after updates, but only enabled, nonfailed plugins pass `PluginManager.cpp:1025–1039`; ABI checks at `1052–1059` also apply. IPC dispatch (`src/ipc/s1/Commands.cpp:1778–1794`) enters `CPluginSystem::loadPlugin`, whose permission check can deny or await user approval (`src/plugins/PluginSystem.cpp:20–59`). Permission enforcement defaults off (`src/config/values/ConfigValues.cpp:717`); if enabled, a rule or user approval is additionally required. `dlopen` occurs at `PluginSystem.cpp:77` only after that gate. These later checks do not undo build-time user-code execution, but disprove unconditional compositor loading.

**Severity and fix.** Medium, conditional on the user choosing an unauthenticated source or a trusted source being compromised; user-code execution during an update is the exposure. Updating trusted native code is an intended operation, so lack of signatures alone is a supply-chain hardening observation. Prefer authenticated transports, explicit immutable revisions where desired, robust reset-status/identity checks, and an optional publisher-signature/update-review policy. Do not claim that fast-forward ancestry alone establishes publisher trust.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
