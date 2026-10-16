# F-PERM-06 Permission-dialog text is not escaped

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unescaped variable substitution into markup-bearing text is confirmed; visual phishing depends on the external renderer, and virtual-keyboard names are derived rather than arbitrary.

**Claim.** Client-related names/paths are substituted unescaped into permission text containing markup, enabling rich-text phishing. **Verdict: partially: unescaped text flow is confirmed; the external renderer's exact phishing behavior is not verified.**

**Evidence — source-confirmed.** `src/managers/permissions/DynamicPermissionManager.cpp:240-261` derives `appName` and inserts it, the plugin path, or the keyboard name into localized text without local escaping. `DynamicPermissionManager.cpp:271` appends explicit `<br/>` and `<i>` markup. English translations in `src/i18n/Engine.cpp:219-225` surround the variables with `<b>...</b>`, for example:

```cpp
// DynamicPermissionManager.cpp:258
I18n::i18nEngine()->localize(I18n::TXT_KEY_PERMISSION_REQUEST_PLUGIN,
                           {{"app", appName}, {"plugin", binaryPath}});
// AsyncDialogBox.cpp:103: description is passed as the --text argument
```

`src/i18n/Engine.cpp:1822-1825` delegates variable substitution to hyprutils. `src/helpers/AsyncDialogBox.cpp:103` passes the resulting description as one vector argument to `CProcess`, not a shell command string. This is not evidence of shell injection or code execution.

**Substitution check — executed.** The isolated `dependency_checks.cpp` uses installed hyprutils **0.14.2** to localize a test variable containing ordinary comparison characters. It prints `localized=Label: A < B & C > D`, confirming that this installed localization implementation does not HTML-escape the value. This checks substitution only. No dialog was opened and no renderer or human reaction was tested; hyprutils is an installed dependency, not part of the pinned commit.

**Full text-source chains and preconditions — source-confirmed.** A valid capture request reaches the permission call at `src/managers/screenshare/ScreenshareFrame.cpp:162`; enabled enforcement and an uncached request enter `DynamicPermissionManager.cpp:86,147,232`. `binaryNameForWlClient` obtains peer credentials and canonicalizes `/proc/PID/exe` (`src/helpers/MiscFunctions.cpp:368-410`); the prompt displays the executable basename (`DynamicPermissionManager.cpp:243-244`). A client can control the name of an executable it runs, subject to filesystem naming rules and procfs lookup permissions; this is not an arbitrary protocol app-title string. Plugin request paths come from `src/ipc/s1/Commands.cpp:1770-1784` through `src/plugins/PluginSystem.cpp:21` into the same formatter. The IPC parser's space-separated tokenization constrains path contents. Both paths finish at the helper's `--text` argument.

**Keyboard correction — source-confirmed.** Virtual-keyboard creation does not accept an arbitrary name argument. `src/protocols/VirtualKeyboard.cpp:11-31,110` derives its name from the creating executable when `misc:name_vk_after_proc` is true (the default at `src/config/values/ConfigValues.cpp:571`), otherwise uses a fixed name. `src/devices/VirtualKeyboard.cpp:47` passes that into `m_deviceName`; `src/managers/input/InputManager.cpp:1123,2157-2169` normalizes/deduplicates it before permission lookup at `1226`. `src/helpers/MiscFunctions.cpp:413-420` folds spaces/newlines/commas and lowercases other characters, but does not generally escape markup delimiters. A keyboard dialog additionally requires a matching ask rule because unknown keyboard keys default to allow (`DynamicPermissionManager.cpp:177-206,222-224`).

**Severity and evidence limit.** Low for unsanitized prompt text. Source confirms markup-bearing text construction and delivery, not the supported markup subset, link handling, clipping, truncation, actual appearance, or successful user deception. The genuine `hyprland-dialog` renderer's pinned source is not supplied in the upstream checkout. Its existence in the host's PATH is not renderer validation. Settling the visual claim requires reviewing the packaged renderer version and a controlled rendering test in an isolated environment, outside the running desktop session. No automatic grant follows from this text alone.

**Minimal fix direction.** Escape dynamic values at the renderer's markup boundary, or render variable spans as literal text; bound displayed lengths while preserving sufficient identity information. Handle newlines according to display context rather than assuming HTML escaping alone removes all presentation ambiguity.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
