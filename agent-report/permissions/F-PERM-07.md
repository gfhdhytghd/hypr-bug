# F-PERM-07 Missing / invalid dialog results can allow access

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Helper lookup weaknesses hold, but a null dialog is dereferenced before fallback: conditional compositor crash, not allow-all; startup also warns.

**Claim.** Missing or misclassified `hyprland-dialog` silently disables permission control and allows all requests. **Verdict: partially: lookup weaknesses hold, but the asserted fail-open behavior is contradicted by an earlier null dereference.**

**Decisive evidence — source-confirmed.** `src/helpers/AsyncDialogBox.cpp:15-19` returns null when its lookup fails. The permission caller at `src/managers/permissions/DynamicPermissionManager.cpp:276-282` does this in order:

```cpp
rule->m_dialogBox = CAsyncDialogBox::create(...);
rule->m_dialogBox->m_priority = true;
if (!rule->m_dialogBox) {
    // log about disabling permission control
    rule->m_allowMode = PERMISSION_RULE_ALLOW_MODE_ALLOW;
    return;
}
```

The write through the null shared pointer precedes the check. Installed hyprutils pointer semantics do not rescue it: `/usr/include/hyprutils/memory/SharedPtr.hpp:141-157` makes `operator->()` return `get()`, which returns null for an empty shared pointer. Thus this is source-confirmed undefined behavior with an expected null-dereference crash, not a reachable successful permission fallback. No compositor crash was executed. Even if the null check were moved up, that branch would set only the newly created rule, not a session-wide enforcement-disable flag.

**Complete conditional chain — source-confirmed.** A Wayland client with access to the compositor can submit an otherwise valid capture request (`src/protocols/Screencopy.cpp:64-65,135`), leading to a pending frame (`src/managers/screenshare/ScreenshareSession.cpp:187-193`). Frame processing calls `copy` (`src/managers/screenshare/ScreenshareManager.cpp:27-40`) and the permission API (`src/managers/screenshare/ScreenshareFrame.cpp:162`). When enforcement is enabled, no existing decision covers the client/binary, and the helper check fails, `DynamicPermissionManager.cpp:119-149` calls `askForPermission`; creation returns null and the next line writes `m_priority`. No special malicious request argument or invalid Wayland object is needed for this source defect: the essential extra condition is the missing/unusable helper environment. With enforcement disabled the API returns allow earlier, so this null path is not reached. Existing allow/deny/pending rules also avoid prompt creation.

**Lookup correction — source-confirmed.** `src/helpers/fs/FsUtils.cpp:83-107` tests PATH candidates for existence and regular-file status, then returns the first such candidate's **others-execute bit**. A legitimate executable usable by its owner/group without others-execute may be misclassified. A non-executable regular first candidate prevents checking later directories. The path list is static once first initialized (`FsUtils.cpp:87`), while actual subprocess execution is separate; identity/lookup consistency is not enforced. Merely changing a client's PATH does not change the compositor's path list. An ordinary user cannot normally remove a distribution-owned system executable or uninstall its package without additional authority; the dossier's deletion example is not a Wayland-only capability.

**Warning correction — source-confirmed.** `src/Compositor.cpp:897-913` checks for the helper and shows a fifteen-second warning notification unless `misc:disable_hyprland_guiutils_check` is enabled. That setting defaults to false (`src/config/values/ConfigValues.cpp:597`). Therefore “only an ERR log, no visible warning” is incorrect, although the startup warning is not a safe terminal handling policy for each later failed prompt.

**Other invalid-result cases — source-confirmed.** Missing helper, failed open, and invalid stdout are distinct. `AsyncDialogBox.cpp:105-108,117-119,127-130` can return a null promise on setup/spawn failure; `DynamicPermissionManager.cpp:285-286` calls `then` without checking it, and `src/helpers/defer/Promise.hpp:104-107` accesses promise members. Empty/unrecognized stdout on a successfully opened dialog instead follows F-PERM-01's retained-PENDING path (`DynamicPermissionManager.cpp:299-318`), not this null result and not allow-all.

**Severity and minimal fix direction.** High for the corrected conditional local-client compositor-crash path; it requires enforcement enabled and a missing/misclassified helper or failed dialog setup. Reject Medium “silent session-wide allow” as the current mechanism. Check the dialog and returned promise before dereferencing, close/release failed setup resources, and settle the pending permission as DENY/error with a clear notification. Correct executable discovery using effective execution accessibility and a consistent trusted helper path. Moving the null check alone would activate the existing fail-open branch; change its decision too.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
