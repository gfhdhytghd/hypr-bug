# F-ICC-01 ICC VCGT parsing can read outside buffer bounds

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Configuration / Lua / ICC helpers. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Singleton VCGT tables pass validation and reach out-of-bounds logging reads; deterministic SIGSEGV and useful disclosure are unproven.

**Verdict: confirmed, with impact corrections.** The VCGT parser accepts a one-element-per-channel table and then indexes elements outside those vectors while forming debug log arguments. This is a heap out-of-bounds read in configuration-selected ICC handling; a reliably crashing or useful information-disclosure exploit was not demonstrated.

**Evidence — source-confirmed.** `src/helpers/cm/ColorManagement.hpp:189-194` defines `entries` as `uint16_t` and each channel as `std::vector<uint16_t>`. `src/helpers/cm/ICC.cpp:72-81` obtains the dimensions from raw tag bytes and rejects zero entries, but does not require at least two. `:90-97` resizes each channel to exactly `entries`. The raw-tag size guards at `:56-62,100-106` protect input-byte reads; they do not establish that the later vector indices are valid. The final ramp-value check at `:124-126` likewise does not constrain vector length.

The offending excerpt is `src/helpers/cm/ICC.cpp:129-131` (red shown):

```cpp
LOG(Log::DEBUG, "readVCGT16: red channel: [{}, {}, ... {}, {}]",
    table.ch[0][0], table.ch[0][1],
    table.ch[0][table.entries - 2], table.ch[0][table.entries - 1]);
```

For a singleton vector, index `1` is past its end; integer promotion makes `entries - 2` negative before its conversion to the vector's unsigned index type. This is undefined behavior. The analogous green/blue expressions repeat it. `src/debug/log/Logger.hpp:10-13` expands `LOG` to an ordinary function call, and the enabled/level checks are inside the function at `:45-50`. Thus the macro supplies no argument-evaluation guard. Disabling debug output is not a source-level bounds fix.

**Trigger chain — source-confirmed.** A user-selected ICC path enters `hl.monitor` through the `icc` descriptor (`src/config/lua/bindings/LuaBindingsConfigRules.cpp:141-142`). `CMonitorRuleParser::parseICC` accepts any nonempty path (`src/config/shared/monitor/Parser.cpp:277-283`). After successful field parsing, `hlMonitor` stores the rule and requests a monitor-state refresh (`LuaBindingsConfigRules.cpp:1157-1169`). The refresh schedules a monitor reload (`src/config/supplementary/propRefresher/PropRefresher.cpp:123-125`); render prechecks invoke `ensureMonitorStatus` (`src/config/shared/monitor/MonitorRuleManager.cpp:23-29`), which selects an applicable rule and applies it softly or fully (`:138-170`). Full application also calls the soft path (`src/output/Monitor.cpp:1037`). A nonempty ICC path reaches `SImageDescription::fromICC` (`Monitor.cpp:627,652-653`). Ordinary config reload can also invoke this directly through `postConfigReload` (`src/config/lua/ConfigManager.cpp:822-829`).

`fromICC` requires an existing readable file, LCMS profile opening, RGB color space, and successful LUT/primaries construction (`src/helpers/cm/ICC.cpp:277-304`) before reading VCGT at `:306-307`. These checks require an otherwise usable RGB display profile; they do not add an entry-count guard to the separate raw VCGT parser. `cmsReadRawTag` supplies the raw tag at `:56-62`; no decoded LCMS VCGT validator is called by this path. The accepted singleton case can therefore reach the unsafe vector access after the parser's remaining value check. This is a source-confirmed boundary failure, not an executed end-to-end ICC test.

**Reachability and severity.** High for memory-unsafe compositor processing of a selected, potentially downloaded profile. The user must configure a matching active monitor to use the file, or code with configuration/IPC authority must do so. Merely offering a file or sending ordinary window metadata is insufficient. `render:icc_vcgt_enabled` defaults to true (`src/config/values/ConfigValues.cpp:666`); disabling it bypasses this parser (`ICC.cpp:306-316`). Existing unchanged monitor rules may be skipped (`MonitorRuleManager.cpp:156-159`), so the profile must actually be loaded. This is not an ordinary client ICC-upload path: the protocol's non-debug ICC creation is rejected (`src/protocols/ColorManagement.cpp:135-140`).

**Corrections.** The invalid accesses are into decoded channel vectors, not an unchecked raw-input offset. A negative vector index does not necessarily address a remotely distant mapping: it can read immediately before the allocation. Undefined behavior does not imply an almost certain SIGSEGV. Adjacent 16-bit values might be formatted into logs if execution continues, but attacker retrieval, sensitive contents, and reliable disclosure are unverified. The upstream API is `SImageDescription::fromICC`, not the dossier's `loadICCFromDescription`; the `entries` declaration is at `ColorManagement.hpp:191`.

**Minimal fix direction.** Reject fewer than two entries before channel access, or make diagnostic sampling safe for every accepted size. Treat all future VCGT consumers consistently. No malformed profile or crash harness was created or run.

[Full dossier-level verification report](_evidence/full-audit-04-config-lua-helpers.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md). These remain agent evidence, not human-confirmed material.
