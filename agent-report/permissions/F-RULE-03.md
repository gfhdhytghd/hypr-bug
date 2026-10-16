# F-RULE-03 Execution-token matching performs synchronous process-environment reads

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Eligible execution-token rules reread process environments synchronously during mapping; no cache exists, but latency and denial-of-service magnitude remain unmeasured.

**Claim.** Execution-token matching repeatedly reads process environments synchronously on the mapping path. **Verdict: confirmed for the performance mechanism; no measured compositor stall is claimed.**

**Evidence — source-confirmed.** `src/desktop/view/window/Window.cpp:753-778` gets the backend PID, rejects PID ≤ 1, opens `/proc/PID/environ` synchronously on Linux, and expands a local buffer in 512-byte increments:

```cpp
std::ifstream ifs(environFile, std::ios::binary);
if (!ifs.good())
    return {};
buffer.resize(512, '\0');
while (ifs.read(buffer.data() + needle, 512)) {
    buffer.resize(buffer.size() + 512, '\0');
    needle += 512;
}
```

The function parses the entire buffer into a fresh map at `Window.cpp:794-810`; there is no cache. `src/desktop/rule/windowRule/WindowRule.cpp:466-470` invokes it for an execution-token matcher when `allowEnvLookup` is true.

**Call chain, guards, and preconditions — source-confirmed.** A client's first window map calls `src/desktop/view/window/Window.cpp:1342-1346` into `readStaticRules` and possibly `recheckStaticRules`. Their rule loops call `matches(window, true)` at `src/desktop/rule/windowRule/WindowRuleApplicator.cpp:530-536,564-574`. Only a matcher reached before another property rejects the window performs the read; ordinary rules without an execution-token match do not. Execution rules must already exist from compositor-authorized ruled launches (`src/config/supplementary/executor/Executor.cpp:118-140`); a Wayland client does not create arbitrary token rules just by declaring an environment variable. Rules are removed after a matching normal mapping (`WindowRuleApplicator.cpp:555-559`). An independent environment read also occurs when initial-workspace tracking is enabled (`Window.cpp:1182-1187`), whose default is 1 (`src/config/values/ConfigValues.cpp:591`). Access failure yields an empty map rather than a blocking retry.

**Severity and corrections.** Low: redundant main-thread work under a configured population of outstanding execution rules. N eligible token rules can mean N reads per mapping pass, but total rule count, process environment size, and map rate determine impact; no timings or denial-of-service threshold were executed. The dossier's universal two-MiB environment limit is not established: Linux exec limits depend on platform and stack/resource settings. The read is procfs data, not an arbitrary client-supplied file path. Normal dynamic rule updates call `matches` with its default false environment flag and skip execution rules (`WindowRuleApplicator.cpp:598-604`).

**Minimal direction.** Read the necessary environment keys once per mapping pass and share the result across rule matching and initial-workspace handling. A persistent PID cache would require process-start identity and invalidation; a per-pass cache avoids that additional lifecycle problem.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
