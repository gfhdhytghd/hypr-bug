# F-BOOT-07 Data-file writes have path and replacement races

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Symlink-following truncation and directory races remain conditional on writable data storage. Donation startup guards missing data home; deterministic nullopt crash is overstated.

**Claim.** Data-home creation and file writes permit replacement races/symlink redirection; donation state handling allegedly dereferences an unconditionally missing data-home result.

**Evidence — source-confirmed.** `src/helpers/fs/FsUtils.cpp:12–35` uses `XDG_DATA_HOME` or `HOME/.local/share/` and requires the base to exist. It checks, creates, and only then changes permissions on the `hyprland/` child (`37–46`), warning and proceeding if chmod fails. Existing children receive no ownership, directory-type, or mode validation (`37–54`). The write helper is:

```cpp
std::ofstream of(path, std::ios::trunc); // FsUtils.cpp:71
// ...
of << content;                       // :77
```

This opens the named destination with truncation and follows ordinary filesystem links. It supplies no explicit private mode for a newly created file; parent privacy and the process umask matter.

**Trigger chains and caller checks — source-confirmed.** Environment/data-directory state → manager creation (`src/Compositor.cpp:756–760`) → `getDataHome` → fixed data filename → `writeToFile`. `src/managers/VersionKeeperManager.cpp:25–35` returns if the data home is missing, and writes `"0.0.0"` only if reading `lastVersion` fails. Lines 38–43 write the build version only when the existing contents compare older. Thus not every pre-existing readable target is overwritten by this manager. `DonationNagManager.cpp:44–47` also returns on missing data home. Lines 60–66 initialize state when needed; later date/version gates control further updates (`69–115`). `getState` and `writeState` independently cache fresh data-home results and dereference them without a local check (`137–139,155–157`). There is no unchecked dereference in `fire()` at the dossier's cited line 135; its unused cached result (`126–134`) is not dereferenced.

**Reachability and preconditions.** Under a normal private data hierarchy another UID cannot plant the final filename. A shared/wrongly owned data root, replaceable child, or existing permissive hierarchy is required for cross-user redirection; the resulting target must also be writable by the compositor UID. Payloads are fixed version strings or numeric time/version state, not arbitrary attacker bytes. `create_directory` uses default permissions filtered by umask; a create-to-chmod interval is not automatically writable by another UID under a typical restrictive umask. Even then a writable ancestor can permit path replacement, which is why path identity matters in addition to chmod.

The initial donation guard breaks the simple stable-nullopt crash claim. A later independent `getDataHome()` can still fail if access or filesystem state changes after the first successful call, making the unchecked optional a real conditional error-handling concern. No such filesystem transition was executed, and no deterministic startup crash is asserted for a consistently unavailable data root.

**Severity.** Medium for conditional victim-writable file clobber through an insecure data namespace, revised upward from the dossier's Low label for that limited effect; Low for exceptional-state robustness. No default cross-user path or execution is established. The directory prerequisite overlaps F-BOOT-01/04 and must not be counted as independent access already achieved by an attacker.

**Corrections.** `getDataHome` does not blindly create the base directory; it returns nullopt when that base is absent (`30–33`). A base merely owned by another user does not suffice if traversal/write permissions exclude the victim. Short writes are not equivalent to arbitrary content injection. The donation optional claim needs the caller guard and the second-call failure condition above.

**Minimal fix direction.** Validate the data directory and preserve its identity through descriptor-relative operations. Reject symlink targets for state writes and use private, safely replaced files within that directory. Resolve/check the data home once and pass it to donation helpers, or check every optional locally. No symlink overwrite or race harness was run.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
