# F-PM-08 Header validation trusts writable package metadata

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Compatibility checks are not authenticity checks, but headers remain root-owned and appended package paths do not normally override the managed package.

**Claim.** Ordinary peers can replace installed headers during a chmod window, or appended package metadata can redirect validation and silently inject a plugin backdoor.

**Evidence and guards — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:419–420` requires the managed `headersRoot/share/pkgconfig/hyprland.pc`. At `423–445` it asks pkgconf for include flags and reads a selected `hyprland/src/version.h`. It validates the header commit (`459–481`) and recorded installed ABI (`483–487`), returning success only at `489`. This is compatibility validation, not a full header-content signature check.

The search path at `PluginManager.cpp:1156–1158` is:

```cpp
static const auto str = std::format("{}/share/pkgconfig:$PKG_CONFIG_PATH", DataState::getHeadersPath());
```

The managed package directory is prepended, not appended. `hyprland.pc.in:1–8` supplies include paths rooted in its configured prefix. Headers originate from privileged installation (`Makefile:37–54`; `PluginManager.cpp:632–639`). That command changes files to 644 and directories to add traversal permission. Those modes do **not** grant the ordinary owner of the invoking process write access to root-owned files. There is no same-user write window created by reducing mode 755 to 644; neither mode grants non-root write permission on root-owned files.

**Dependency control — executed.** The isolated [dependency_controls.py](/home/wilf/data/.claude-scratch/agent-reports/work/03-hyprpm/dependency_controls.py) in the assigned scratch directory created two harmless package directories with different include paths, passed only a small explicit environment to `/usr/bin/pkgconf`, and checked that the first package directory wins. [dependency-controls.json](/home/wilf/data/.claude-scratch/agent-reports/work/03-hyprpm/dependency-controls.json) records success. No installed hyprpm cache, compiler build, socket, or privileged operation was used. This confirms the ordinary same-package lookup priority on this host, not every possible pkgconf behavior. The local pkgconf manual (`/usr/share/man/man1/pkgconf.1.gz`, decompressed lines `738–756`) documents that environment mechanisms such as preloaded files can override ordinary package discovery; that still requires control of the caller's environment and is not evidence that an appended duplicate package automatically wins.

**Call chain and missing attacker step — source-confirmed.** Environment/package metadata → `headersValid` and `getPluginBuildEnv` (`PluginManager.cpp:423,1140–1141`) → unprivileged plugin build (`330–338,834–842`) → output installation. With normal managed-cache ownership and ordinary appended `PKG_CONFIG_PATH`, the dossier does not establish how another UID can replace the main package or headers. If an attacker already controls the invoking user's environment/toolchain, that user-code execution capability is already outside the proposed header-only trust boundary. Writable build inputs consumed by root are a different, confirmed concern at F-PM-03; do not confuse that checkout with the root-owned installed headers.

**Severity and corrections.** Reject the claimed independent Medium header-replacement vulnerability; retain only informational build-reproducibility/supply-chain hardening. A full file manifest could detect drift but its absence is not proof of an attacker write capability. To reopen the claim, demonstrate an untrusted actor's actual ability to write a normal installed header or supply metadata ahead of the managed package without already controlling the invoking process environment/privileged installation. No such permission-changing source path is identified here.

**Minimal fix direction.** Use explicit package/tool paths and a controlled build environment where reproducibility requires them; verify header-tree ownership and consider a content manifest. Preserve the distinction between compatibility and authenticity checks.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
