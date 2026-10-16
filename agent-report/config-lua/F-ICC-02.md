# F-ICC-02 ICC binary reads do not handle failed size queries

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Configuration / Lua / ICC helpers. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Failed size queries become huge unsigned lengths; uncaught length_error can terminate a configured load. FIFO open may block first.

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

[Full dossier-level verification report](_evidence/full-audit-04-config-lua-helpers.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md). These remain agent evidence, not human-confirmed material.
