# F-IPC-07 Synchronous IPC work and connection load block the event loop

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Synchronous work and absent aggregate peer quotas hold; a 250 ms watchdog limits ordinary Lua loops, and input is not retained as claimed.

**Claim.** Synchronous eval or heavy queries can freeze the compositor indefinitely, while unlimited connections each retain 1 MiB of input plus 64 KiB of follow output.

**Verdict: partially.** Main-loop execution and missing application-level aggregate quotas are real. The plain unbounded-bytecode-loop example and the per-connection retained-memory calculation omit existing guards and state transitions.

**Evidence / execution chain — source-confirmed.** `src/ipc/s1/Unix.cpp:264` registers the listener with `m_wlEventLoop`; `66-67` registers each peer with that loop. The client callback reads a request and directly invokes `m_requestHandler` at `Unix.cpp:316-325`; `src/ipc/s1/S1.cpp:39-41,85-109,205-210` dispatches it synchronously. Response promises support deferred results, but they do not automatically move handler CPU work to another thread (`src/helpers/defer/Promise.hpp:98-107`; `Unix.cpp:145-157`). A slow synchronous handler therefore delays compositor event processing.

**Existing CPU guard — source-confirmed.** `src/config/lua/ConfigManager.cpp:917` calls:

```cpp
guardedPCall(0, LUA_MULTRET, 0, LUA_TIMEOUT_EVAL_MS, "hyprctl eval")
```

`src/config/lua/ConfigManager.hpp:118-125` sets an instruction interval of 10,000, eval timeout of 250 ms, and dispatch timeout of 100 ms. `ConfigManager.cpp:452-491` installs a count hook, checks the steady-clock deadline, raises a Lua error when it expires, and restores the previous hook/state. `533-542` also removes the ordinary debug hook setter/getter. This breaks the dossier's simple uninterrupted bytecode-loop trigger: an ordinary loop is expected to return an error around the deadline at the next hook opportunity. This is a source conclusion, not measured desktop timing or a proof that hostile Lua is safely confined. An instruction hook is not a preemptive timer for blocking native library operations, Lua compilation, or every other C++ handler; the opened native libraries therefore cannot be assigned a universal wall-clock bound from this guard. No bypass or blocking payload was tested.

**Existing memory/lifetime guards — source-confirmed.**

- `src/ipc/s1/Unix.cpp:43-44` defines a 1 MiB request cap and 64 KiB incremental follow-output cap. `111-142` appends while bytes arrive, rejects input above the cap, then moves nonempty input to the request as soon as the current read drains. It does not retain a partially supplied 1 MiB request on an idle peer waiting for a length terminator. Nonempty input is dispatched; holding a connection open does not reserve a full megabyte.
- `Unix.cpp:79-91` starts a five-second request timer and `138-141` resets that timer when a nonempty request is dispatched. This is a request-acquisition timeout, not an execution, reply-drain, deferred-response, or follow-session deadline. `src/managers/eventLoop/EventLoopManager.cpp:142-146,191-195` skips timers with no owning reference and later prunes them, so resetting the peer's timer does stop its later callback from enforcing an execution deadline.
- `Unix.cpp:283-300` drains accepts and stores peers without an application peer-count limit. `listen(..., 10)` at line 258 is a pending-connection backlog, not an active-peer quota. Kernel/process FD and memory limits still exist; “no cap” here means no IPC-specific quota, not literally infinite allocatable resources.
- `Unix.cpp:165-170` enforces 64 KiB when appending follow data. Initial replies at `145-162` have no equivalent size check. Furthermore the upstream log producer first accumulates per-follower text in `src/debug/log/RollingLogFollow.hpp:23-29`; that staging string is drained later at `Unix.cpp:391-402`. Thus 64 KiB is not a bound on all log-related storage in every phase.
- `Unix.cpp:151-155,350-353` locks weak peers and checks membership before completing deferred work. A disconnected peer is not kept alive merely by that weak continuation. No dangling-pointer or use-after-free claim follows from the connection-limit observation.

**Reachability / severity / corrections.** An ordinary same-UID process with socket access can submit work or hold multiple peers. Other UIDs remain blocked by the private instance directory (F-IPC-04). Per-request CPU work runs on the event loop and batch entries execute sequentially (`S1.cpp:156-158`) without an aggregate batch time budget. This supports Medium availability-hardening priority for aggregate work and connection quotas. It does not prove a compositor crash, a persistent hang from the supplied simple loop, or the asserted hundreds-of-megabytes retention scenario. No RSS increase, exhaustion threshold, responsiveness loss, or crash was measured; query-specific pathological costs are not established here.

**Minimal fix direction.** Add conservative active/deferred/follow peer quotas, admission and batch-work budgets, and bounded producer-side log queues. Bound reply lifetime/output where practical. Keep the existing Lua watchdog; do not describe adding it as a missing fix. If arbitrary native operations remain available, their blocking behavior requires separate scheduling or capability policy rather than assuming instruction hooks provide hard preemption.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
