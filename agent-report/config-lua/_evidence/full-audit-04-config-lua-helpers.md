# Configuration, Lua bindings, and helpers: upstream verification

Baseline: official Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).  
Dossier: [04-config-lua-helpers](../../pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md).  
Date: 2026-10-15.

Method: read-only examination of the pinned upstream worktree and installed dependency headers, tracing input validation, callers, and exception/lifetime boundaries. Any isolated dependency checks are identified separately as **executed**; upstream code observations are **source-confirmed**. No compositor execution, live IPC, full build, source changes, or GitHub access. Upstream line numbers below refer to this baseline, not the fork's dossier. The example reports named by the brief were absent from this tracker checkout.

## Summary

All four numbered findings and all four unnumbered negative/control records have been reviewed. Numbered findings: **2 confirmed, 1 partially, 1 rejected, 0 inconclusive**. Including controls: **5 confirmed, 2 partially, 1 rejected, 0 inconclusive**. A confirmed control means the narrow negative observation holds, not that another vulnerability was found.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
| --- | --- | --- | --- | --- |
| F-ICC-01 | confirmed | High, user-selected file required | `src/helpers/cm/ICC.cpp:80-131`; `src/output/Monitor.cpp:653` | Singleton VCGT tables pass validation and reach out-of-bounds logging reads; deterministic SIGSEGV and useful disclosure are unproven. |
| F-ICC-02 | confirmed | Low | `src/helpers/cm/ICC.cpp:17-33,277-282` | Failed size queries become huge unsigned lengths; uncaught length_error can terminate a configured load. FIFO open may block first. |
| F-CFG-01 | rejected | None; latent dead-code hardening | `src/config/shared/monitor/Parser.cpp:215-274`; `src/config/lua/bindings/LuaBindingsConfigRules.cpp:103-139` | Throwing conversions remain, but have no upstream callers; Lua uses bounded integer fields and direct assignment. |
| F-RULE-01 | partially | Low | `src/desktop/rule/matchEngine/RegexMatchEngine.cpp:6-16`; `src/desktop/rule/Rule.cpp:98` | Missing regex validation and inverted failure match hold; debug-library abort is unsupported, and installed RE2 reports errors then returns false. |
| Control: ConfigWatcher | confirmed | Informational | `src/config/shared/inotify/ConfigWatcher.cpp:80-110` | Header and name bounds precede field access; accepted event strides are bounded and always progress. |
| Control: Lua objects/timers | partially | Informational | `src/config/lua/objects/LuaWindow.cpp:63-69`; `src/config/lua/ConfigManager.cpp:502-532` | Window getters and ordinary timer teardown are guarded; uniform object-method checking is false, and reentrant reload safety remains unverified. |
| Control: setprop | confirmed | Informational | `src/config/shared/actions/ConfigActions.cpp:786-806,819-952` | Trivial conversions and gradient-angle stoi are inside appropriate catches; this does not certify unrelated property logic. |
| Control: C allocation/string scan | confirmed | Informational | `src/debug/crash/CrashReporter.cpp:179`; `src/Compositor.cpp:81` | No malloc, strcpy, or sprintf calls found in src; comment/include and NOLINT matches do not establish broader memory safety. |

**Shared input/permission boundary — source-confirmed.** Loading an ICC path or installing a rule requires the user's configuration to select it, or a process able to evaluate Lua through the compositor's control socket. Upstream creates the instance directory with owner-only permissions (`src/Compositor.cpp:222-231`) and places that socket inside it (`src/ipc/s1/Unix.cpp:242`). IPC `eval` routes to the Lua manager (`src/ipc/s1/Commands.cpp:1201-1210`), whose `eval` loads code and calls the same bindings (`src/config/lua/ConfigManager.cpp:907-917`). Ordinarily this means the compositor user (or a privileged process), not an unrelated UID or a remote peer. Configuration scripts already run with substantial user authority; these findings primarily concern robust handling of selected data and accidental bad configuration.

## F-ICC-01

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

## F-ICC-02

**Verdict: confirmed, with exception and FIFO corrections.** `readBinary` stores an unchecked failed stream-position query in an unsigned length and resizes a vector with it. Failure can escape the ICC load and terminate the compositor; the dossier's specific `bad_alloc` explanation is inaccurate for the installed standard library.

**Evidence — source-confirmed.** `src/helpers/cm/ICC.cpp:17-33` checks stream state only immediately after open:

```cpp
ifs.seekg(0, std::ios::end);
size_t len = ifs.tellg();
ifs.seekg(0, std::ios::beg);
if (len <= 0)
    return {};
std::vector<uint8_t> buf;
buf.resize(len);
```

No state or signed-position check follows either seek. A failed `tellg` returns `pos_type(-1)`; conversion to `size_t` yields its maximum value, which passes the unsigned zero check. Installed libstdc++ corroborates both steps: `/usr/include/c++/16/bits/istream.tcc:874-899` initializes the return position to `-1` and leaves it there when the stream has failed; `/usr/include/c++/16/bits/stl_vector.h:2192-2195` throws `std::length_error` for a growth request exceeding `max_size()`. Vector default growth uses this check (`/usr/include/c++/16/bits/vector.tcc:855-856`). Thus this case is normally rejected before attempting an actual maximum-sized allocation.

**Trigger chain and guards — source-confirmed.** The configured monitor ICC path follows the same rule/refresh/application chain described in F-ICC-01, through `src/output/Monitor.cpp:653`. `fromICC` only checks filesystem existence before `readBinary` (`src/helpers/cm/ICC.cpp:277-282`). It does not require a regular file. The empty-data and LCMS validity checks at `:284-289` happen **after** the resizing operation, so they cannot protect this failure. In particular, this defect does not require a syntactically valid ICC or enabled VCGT processing.

`fromICC`, `applyMonitorRuleSoft`, and `ensureMonitorStatus` have no catch for this exception; they handle returned errors instead. Reload-time monitor application occurs after the guarded Lua call (`src/config/lua/ConfigManager.cpp:757-760,822-829`), and the render-precheck route is also outside Lua evaluation. `src/managers/eventLoop/EventLoopManager.cpp:124-139` runs the Wayland loop without a surrounding C++ catch. The catch in `src/main.cpp:264-270` covers only compositor construction; server initialization and compositor start at `:289,300` are outside it. Therefore an exception on this runtime path is not converted to a config error.

**Reachability and severity.** Low: this requires configuration/IPC authority or persuading the user to select an unsuitable local path. An existing readable non-seekable input can pass open but fail seeking. A named pipe is conditional: opening it for reading can block until another process opens its writer; without that peer, the dossier's claimed immediate abort is not reached. Plain unreadable/nonexistent paths return errors, and a seekable empty file returns an empty vector. File-type/seek failure, rather than malformed ICC contents, is the relevant precondition. No live filesystem special-file trigger was exercised.

**Corrections.** The core missing-size-check defect is present at the pinned baseline. Replace the precise `bad_alloc` claim with `length_error` for this libstdc++, and avoid claiming that every FIFO/special file produces the same immediate outcome. Do not describe the path as accepting arbitrary client-uploaded ICC data.

**Minimal fix direction.** Validate both seeks and the signed stream position before conversion, bound the accepted file size, and check read completion. Return an ICC read error for invalid file types/positions and handle allocation exceptions at the load boundary.

## F-CFG-01

**Verdict: rejected.** The dossier alleges that oversized monitor transform/VRR inputs reach uncaught `std::stoi` calls through Lua configuration or IPC, terminating the compositor. The unsafe conversion bodies remain, but the asserted input path does not exist at this baseline.

**Evidence — source-confirmed.** `src/config/shared/monitor/Parser.cpp:215-228` still calls `std::stoi(value)` after `isNumber`, then checks the transform range. `Parser.cpp:266-274` similarly converts VRR without a catch. These are latent hazards if called with an out-of-range numeric string. However, a repository-wide symbol search finds only the declarations (`Parser.hpp:16,21`) and definitions, with **no calls** to either method in upstream `src/`.

The actual monitor descriptors in `src/config/lua/bindings/LuaBindingsConfigRules.cpp:103-107,135-140` use:

```cpp
{"transform", []() -> ILuaConfigValue* { return new CLuaConfigInt(0, 0, 7); },
 // apply assigns the already parsed integer directly to m_transform
{"vrr", []() -> ILuaConfigValue* { return new CLuaConfigInt(-1, -1, 3); },
 // apply assigns the already parsed integer directly to m_vrr
```

`src/config/lua/types/LuaConfigInt.cpp:28-30` rejects string input without an enum map; `:49-60` checks integer minimum/maximum before storing it. `src/config/lua/bindings/LuaBindingsConfigRules.cpp:1157-1162` only invokes a field's apply callback after `val->parse(L)` succeeds:

```cpp
auto err = val->parse(L);
if (err.errorCode != PARSE_ERROR_OK)
    self->addError(...);
else if (!desc->apply(val.get(), parser))
    self->addError(...);
```

**Trigger chain and guard — source-confirmed.** A user-loaded Lua file or authorized IPC Lua evaluation can call `hl.monitor`; `hlMonitor` resolves the field descriptor, parses its bounded integer value, and assigns the typed field. Bad strings or out-of-range integers produce configuration errors and never enter the alleged `std::stoi` functions. This is a configuration/IPC input boundary, not an ordinary Wayland-client request. No config default enables the missing call path.

**Severity and corrections.** None for the claimed reachable abort at `e2ad066c9`. The fork-specific assertion that Lua directly calls both parsers is stale. The distinction is reachability, not whether `std::stoi` can throw. The full baseline symbol search, rather than merely the presence of `lua_pcall`, is the basis for rejecting this finding.

**Minimal fix direction.** Remove unused conversion methods, or use checked conversion before any future caller is introduced. No crash fix is required for the current Lua descriptor path. No vulnerability-triggering execution was performed.

## F-RULE-01

**Verdict: partially.** Missing regex validation and erroneous matching of invalid negative patterns are present. The dossier's assertion that use of an invalid RE2 object is undefined or reaches a debug-library fatal check is unsupported; read-only inspection of the installed library shows a nonfatal error path.

**Evidence — source-confirmed.** `src/desktop/rule/matchEngine/RegexMatchEngine.cpp:6-16` constructs a RE2 object without checking `ok()`, including after stripping the optional negative prefix:

```cpp
if (regex.starts_with("negative:")) {
    m_negative = true;
    m_regex = makeUnique<re2::RE2>(regex.substr(9));
    return;
}
// ...
return re2::RE2::FullMatch(other, *m_regex) != m_negative;
```

`src/desktop/rule/Rule.cpp:51-54,90-106` chooses this engine for title/class matches and sets the property mask after construction. The `canMatch()` guard (`:124-126`) only checks that some engine exists and the rule is enabled; it does not check compilation validity. Installed `/usr/include/re2/re2.h:306-324` exposes `ok()`, `error()`, and `error_code()`. Its `Options` documentation explicitly describes syntax/execution errors being logged at **ERROR** (`:620-625`), not fatal severity.

**Evidence — executed, read-only dependency inspection.** `pkg-config --modversion re2` reports `11.0.0`. `objdump` inspection of `/usr/lib/libre2.so.11.0.0` shows `FullMatchN` calling `DoMatch` (virtual addresses `0x50570-0x5059c`). `DoMatch` tests the compiled error code (`0x502f3-0x502fa`), branches to its error path (`0x50420-0x50429`), logs using Abseil's `LogMessage::ErrorTag` (`0x1bac6`), then jumps to a false return (`0x1bb09` to `0x50400`). It does not take a fatal assertion on this path. This is binary inspection, **not** execution of an invalid regex or a Hyprland test. The transcript and library SHA-256 are retained in [dependency-inspection.txt](/home/wilf/data/.claude-scratch/agent-reports/work/04-config-lua-helpers/dependency-inspection.txt). The library is an installed dependency snapshot, not part of the pinned Hyprland commit; hypothetical differently built debug versions are not verified by this observation.

On that failure path, a positive engine yields false and a negative engine yields true (`false != true`). The latter is a real correctness defect: this predicate accepts every tested string even though the configured regex is invalid. Other predicates on the same rule still apply, so it is inaccurate to say an entire multi-condition rule necessarily accepts every window. RE2 may emit its own diagnostics; the problem is the lack of a Hyprland configuration rejection, not necessarily complete silence.

**Trigger chain and preconditions — source-confirmed.** The user/config author supplies a match expression in `hl.window_rule`. `src/config/lua/bindings/LuaBindingsConfigRules.cpp:1190-1205` defaults rules to enabled and registers them; `:1207-1228` converts match-table values to strings and passes recognized properties to `registerMatch`, without compiling/validating first. `src/desktop/rule/Rule.cpp:98` constructs the engine. Rule processing iterates registered window rules and calls `matches` (`src/desktop/rule/windowRule/WindowRuleApplicator.cpp:530-544,585-607`); `CWindowRule::matches` checks `canMatch()` then forwards title or app ID to the engine (`src/desktop/rule/windowRule/WindowRule.cpp:383-408`). Metadata updates also invoke this path: `CWindow::onUpdateMeta` records title/app-ID changes and calls `propertiesChanged` (`src/desktop/view/window/Window.cpp:900-936`). Window clients can supply those metadata strings, but they cannot create this invalid configuration predicate merely by choosing their title/app ID. A bad user-loaded rule or code with configuration/IPC authority is required; disabled rules fail `canMatch()`.

**Severity and corrections.** Low configuration correctness/diagnostics defect. Positive invalid patterns fail to match; invalid negative predicates unexpectedly succeed. No memory-safety defect, compositor abort, privilege escalation, or security-policy bypass is established. The dossier's legacy `windowrulev2` trigger is replaced by the upstream Lua `hl.window_rule` path. The claim about a debug RE2 abort cannot be inferred from Hyprland's lack of `ok()` checks and is not retained as a confirmed impact.

**Minimal fix direction.** Compile and validate at rule registration, surface the library's error as a config error, and make an invalid predicate fail independently of negation (or reject the whole rule). Falling back to literal matching would silently change the user's intended regex semantics.

## Control: ConfigWatcher

**Verdict: confirmed; informational control.** The dossier's narrow observation that inotify header/name parsing is bounded holds. This is not a positive vulnerability finding or a claim about every callback.

**Evidence and input chain — source-confirmed.** `src/config/shared/inotify/ConfigWatcher.cpp:21-25` creates its own nonblocking inotify fd; `:32-34,51-59` watches configured paths unless autoreload is disabled. A file change reaches the Wayland fd watcher (`src/managers/eventLoop/EventLoopManager.cpp:127-128`), then `onInotifyEvent`. `src/config/shared/inotify/ConfigWatcher.cpp:80-98` reads into an aligned, fixed-size array and rejects nonpositive reads, truncated headers, and incomplete name payloads before dereferencing the corresponding fields:

```cpp
if (offset + sizeof(inotify_event) > sc<size_t>(bytesRead))
    break;
if (offset + sizeof(inotify_event) + ev->len > sc<size_t>(bytesRead))
    break;
// ...
offset += sizeof(inotify_event) + ev->len;
```

The final stride at `:109` includes the nonzero header size, so an accepted event advances the loop. The first reinterpretation at `:88` forms a pointer but does not itself read a header. The actual bytes come from the kernel's inotify stream, not a client-controlled socket; on the examined 64-bit host, adding a bounded offset and a 32-bit event length also cannot wrap `size_t`. At `:100-107`, unknown watch descriptors produce a diagnostic rather than indexing an absent watch. The configured callback reloads at `src/config/lua/ConfigManager.cpp:418-420`.

**Scope, severity, and fix direction.** Informational; no bounds fix indicated for this parsing logic. The upstream line range matches the dossier. A complete audit of callback mutation/reentrancy or every supported architecture is beyond this negative result; no malformed event stream was injected.

## Control: Lua objects/timers

**Verdict: partially; informational control.** Sampled object getters retain a strong pointer after locking and ordinary timer teardown precedes `lua_close`. The stronger claim that all Lua object operations uniformly lock/check their references is false; teardown order alone also does not certify reentrant reloads.

**Object evidence — source-confirmed.** `src/config/lua/objects/LuaWindow.cpp:63-69` locks the window reference into `const auto w`, then returns nil on failure before reading fields:

```cpp
const auto w = ref->lock();
if (!w) {
    lua_pushnil(L);
    return 1;
}
```

This strong local protects the ordinary getter's referent; `:104-108` separately locks the monitor relation. Workspace getter code uses the same pattern (`src/config/lua/objects/LuaWorkspace.cpp:50-55`); timer methods lock and test at invocation (`src/config/lua/objects/LuaTimer.cpp:37-43,53-59`). Installed hyprutils `/usr/include/hyprutils/memory/WeakPtr.hpp:175-185` makes `lock()` return an empty strong pointer when storage is absent, cleared, or destroying; `expired()` includes the destroying state (`:154-166`). The types are hyprutils aliases (`src/helpers/memory/Memory.hpp:8-11`), not assumed `std::weak_ptr` semantics.

However, method dispatch is not uniform: `src/config/lua/objects/LuaMonitor.cpp:89-96` guards property lookup, while `:225-228` returns plain C functions. `monitorSetWorkspace` at `:42-58` obtains the userdata reference and later uses `(*ref)->changeWorkspace` without holding/checking a strong monitor local inside that function. The getter's earlier check is not a universal invocation-time guarantee. This observation narrows the negative result; this report does not promote it to a new independently verified crash finding.

**Timer/reload evidence — source-confirmed.** `hl.timer` stores a callback capturing raw `L` and `ref` (`src/config/lua/bindings/LuaBindingsToplevel.cpp:468-484`) and registers the timer at `:503-505`. `cleanTimers` cancels each timer, removes it, unreferences the Lua callback, then clears the vector (`src/config/lua/ConfigManager.cpp:502-513`). State replacement calls cleanup before invalidating its lifetime token and closing the old state (`:515-532`); normal reload invokes that replacement at `:719,734`, and destruction preserves the same ordering (`:424-435`).

Cancellation remains effective even if the event loop already copied its timer vector: `src/managers/eventLoop/EventLoopManager.cpp:142-146` checks `!t->cancelled()` before invoking each copied timer, and `src/managers/eventLoop/EventLoopTimer.cpp:32-35` records cancellation and clears its expiry. Removal is at `EventLoopManager.cpp:159-163`. `SLuaStateLifetime` is set null before close (`ConfigManager.cpp:522-526`); a consumer such as `CLuaBindRef` checks it before unref (`src/config/lua/bindings/LuaBindingsToplevel.cpp:63-79`). The timer callback itself does **not** capture that token, so the token must not be cited as its direct guard. The one-shot callback's `HAS` test prevents duplicate unref after reload (`:491-498`); it is not a general proof that active callback/state replacement is safe.

**Input chain, limits, and fix direction.** A user-loaded Lua script or authorized IPC evaluation retains object handles or creates timers; later object destruction or config reload exercises these lifetime boundaries. The ordinary expired-getter and pending-timer cases have the guards above. All-method safety and reload invoked from within an active Lua callback were not dynamically verified and are not established by this sample. Severity remains informational because this is a correction to a negative control, not a closed new defect report. Retain the ordinary teardown order; any broader safety claim should require invocation-time strong locks and explicit active-callback reload analysis. No live object destruction/reload test was performed.

## Control: setprop

**Verdict: confirmed; informational control.** The specifically cited trivial numeric and gradient-angle conversions have exception handlers; malformed numeric input does not establish an uncaught-conversion defect there.

**Evidence — source-confirmed.** `src/config/shared/actions/ConfigActions.cpp:776-806` encloses trivial integer/float conversion and assignment in `try`/`catch (...)`. Its `std::stoi`, `std::stoull`, and `std::stof` calls at `:794-803` are inside that boundary. The gradient angle conversion at `:847` is inside the outer `Actions::setProp` try block, opened at `:819` and caught at `:952`:

```cpp
try {
    // ... colorData.m_angle = std::stoi(...)
    // ... other property conversions
} catch (std::exception& e) {
    return std::unexpected(std::format("Error parsing prop value: {}", ...));
}
```

`std::invalid_argument` and `std::out_of_range` from those conversions derive from `std::exception`. The Lua dispatcher calls this action with property/value strings and a selected window (`src/config/lua/bindings/LuaBindingsDispatchers.cpp:640-641`). `setProp` first extracts a target window and returns when none is available (`ConfigActions.cpp:809-812`). User/config/dispatcher property text reaches the selected property branch only with a valid target; a trivial property's internal catch logs and returns, while a gradient conversion error returns an action error from the outer catch. Config defaults do not remove these catches.

**Corrections, severity, and fix direction.** The dossier's numeric-conversion control is accurate at the cited upstream lines. Informational; retain the catches, preferably returning consistent user-visible parse errors. This does not certify arithmetic range, all exceptions thrown by other expressions, property side effects, or all code under `Actions::setProp`. No property was sent to the running compositor.

## Control: C allocation/string scan

**Verdict: confirmed as a narrow textual control; informational.** A whole-`src/` search for the exact identifiers `malloc`, `strcpy`, and `sprintf` finds no calls to these three functions at the baseline.

**Evidence — executed, read-only text search; source-confirmed locations.** `rg -n '\b(malloc|strcpy|sprintf)\b' src` finds `malloc` in a crash-reporter comment (`src/debug/crash/CrashReporter.cpp:179`), the include `<malloc.h>` (`src/Compositor.cpp:81`), and `NOLINT(cppcoreguidelines-no-malloc)` annotations on `free` calls, for example `src/helpers/Format.cpp:90,101`. There are no matches for `strcpy` or `sprintf`. The captured results are in [source-search.txt](/home/wilf/data/.claude-scratch/agent-reports/work/04-config-lua-helpers/source-search.txt).

**Input chain and interpretation.** None applies: this is a source-inventory observation, not a triggerable defect. The dossier's “only comments” wording should include the header inclusion. It does not prove absence of other C allocation/string interfaces, library-internal allocation, out-of-bounds STL accesses, or unsafe file parsing; F-ICC-01 itself illustrates that distinction. Severity informational; no fix is implied by the absence of these exact function calls.
