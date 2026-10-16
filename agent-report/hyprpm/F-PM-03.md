# F-PM-03 Header update trusts build sources and version metadata

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Plugin management (hyprpm). Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Privileged make consumes user-writable build inputs and version fields reach shell commands; fake running-socket metadata does not drive header checkout.

**Claim.** Header updates consume unchecked version metadata and execute installation rules from a clone as root. The dossier also claims a forged running-compositor version reply drives this operation.

**Evidence — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:492–495` specifically calls `getHyprlandVersion(false)`. At `87`, false selects `execAndGet("Hyprland --version-json")`, not `j/version`; parsed commit/date strings are copied without hash/date validation (`96–113`). Date formatting (`536–537`) and checkout/reset (`563,575`) interpolate these fields into shell commands. `isValidHash` (`59–61`) is not called on them. The source URL defaults to official HTTPS, with an explicit user override (`528`); the override is assigned at `hyprpm/src/main.cpp:71–77,98`.

The privileged boundary is `PluginManager.cpp:632–639`:

```cpp
cmd = std::format("make -C '{}' installheaders && chmod -R 644 '{}' ...", ...);
ret = NSys::root::runAsSuperuserUnsafe(cmd);
```

`hyprpm/src/helpers/Sys.cpp:166–172` runs the selected privilege tool with `/bin/sh -c`. Even the baseline `Makefile:37–51` installation target runs `cmake --build ./build --config Release --target generate-protocol-headers` at `46`, consuming generated build rules; this is not just a fixed privileged file copy. The checkout is created as the invoking user (`PluginManager.cpp:521–525,542`); the preceding CMake configuration is also unprivileged (`590–600`). No ownership handoff freezes those inputs before the root make invocation.

**Call chain and gates — source-confirmed.** Local `update` → privilege-cache request (`main.cpp:136–144`) → installed-binary version data (`PluginManager.cpp:87,495`) → dependency check and missing/stale headers or explicit force (`497–511`) → clone/configure (`528–619`) → root make over the user's checkout/build files (`632–639`). The selected helper must grant elevation. A custom source must survive the `.git` existence check (`550–553`), checkout/reset attempts, and configuration error check (`604–619`). These checks are not authenticity checks or a separation of unprivileged build rules from privileged installation. A process already able to change the invoking user's checkout can influence those root-consumed inputs; an unprivileged different UID normally cannot traverse the 0700 working directory.

**What holds and what does not.** High for the privileged execution of mutable build inputs. Unchecked installed-version fields reaching the user's shell also hold, but require control of the installed binary selected through PATH or equivalent local environment control; this is not a demonstrated remote metadata attack. The fake running-socket trigger is rejected for header checkout: true/false version caches are separate (`70–85,115–118`), and the relevant call uses false. A forged running reply may affect other operations, but cannot substitute for the installed-version source in this chain. Custom URL support and lack of signature enforcement are policy facts, not independent proof that official HTTPS can be silently replaced. The dossier's root-escalation conclusion cannot be attributed to a socket reply alone.

**Minimal fix.** Complete configuration/header generation unprivileged, then install a validated, bounded file set through a narrow privileged operation that cannot execute repository build rules. Validate installed-version fields and use argument-vector process invocation. Authenticate or clearly constrain custom source selection without assuming it fixes mutable local build inputs.

[Full dossier-level verification report](_evidence/full-audit-03-hyprpm.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md). These remain agent evidence, not human-confirmed material.
