# F-RES-08 Cursor theme names / index files are not confined

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unconfined inherited names and rescans hold; the equals-sign guard rejects the alleged prefixed-key spoofing.

**Verdict: partially. Severity: Low, theme-file parsing/confinement; no demonstrated privilege boundary.**

Inherited theme names are interpolated into paths without confinement and an expanding inheritance graph is repeatedly rescanned. The alleged malformed-key spoofing does **not** pass the parser's later equals-sign guard.

**Source-confirmed evidence:** `src/managers/XCursorManager.cpp:241-257` contains both the suspicious search and the decisive guard:

```cpp
if (line.rfind("Inherits", 8) != std::string::npos) {
    std::string inheritThemes = line.substr(8);
    // trim leading whitespace ...
    if (inheritThemes.at(0) == '=')
        inheritThemes.erase(0, 1);
    else
        continue;
}
```

If `Inherits` starts at any nonzero position up to eight, character eight lies **inside that word**. The extracted suffix therefore starts with a letter, not optional whitespace followed by `=`. For example, the dossier's prefixed-key form leaves `s=...` and is rejected. `rfind` is an unclear way to express the intended prefix test, but the rest of this parser breaks the claimed injection chain. Empty suffixes are checked before `.at(0)` (`243-257`).

For an ordinary accepted `Inherits` key, values are split on commas and trimmed without rejecting separators or parent components (`263-278`). `themePaths()` uses string formatting to append these names under each library path (`290-308`), and `loadTheme()` passes resulting cursor directories to `loadAllFromDir()` (`113-133`). Parent components can therefore leave a nominal theme root if the resulting directory exists. An absolute-looking component does **not** replace the prefix automatically: these are formatted strings, not a filesystem path-join operation with absolute-path replacement semantics.

`XCursorManager.cpp:315-323` repeatedly rescans all discovered names until the set stops growing. Cycles alone converge because of the set; a long finite inheritance chain adds redundant scans, while no unbounded unique-name or processing budget is imposed. This is a potential local theme-loading performance problem, not a measured stat storm or established infinite loop for ordinary finite themes.

**Input chain/preconditions:** startup theme selection comes from `XCURSOR_THEME` (`src/pointer/cursor/CursorManager.cpp:103-118`), and selected-theme reload uses `m_theme`/the environment (`379-394`). `XcursorLibraryPath()` supplies the search roots (`XCursorManager.cpp:208-217`); an accepted theme's local `index.theme` supplies inheritance (`220-284`). A writable selected theme/search root or startup environment control is required. There is no title/app-ID route, filesystem permission bypass, or return of discovered file contents to a remote client. Theme writers can already supply cursor files and links, so an independent security escalation from nonconfinement is unproven.

**Corrections:** retain the path-confinement and redundant-scan observations, withdraw the malformed-key examples and automatic absolute-path reset. The dossier's directory paths/line numbers are stale; current path construction is at `297,303`. No traversal payload was installed or evaluated against the desktop.

**Minimal fix direction:** express the key test directly or use a desktop-entry parser with a validated group/key; maintain a visited set with a bounded traversal. Define whether inherited themes may contain path separators before enforcing confinement, since theme loading is already a user-controlled resource domain.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
