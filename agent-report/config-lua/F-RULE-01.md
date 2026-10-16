# F-RULE-01 Invalid regular expressions reach window-rule matching

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Configuration / Lua / ICC helpers. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Missing regex validation and inverted failure match hold; debug-library abort is unsupported, and installed RE2 reports errors then returns false.

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

[Full dossier-level verification report](_evidence/full-audit-04-config-lua-helpers.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md). These remain agent evidence, not human-confirmed material.
