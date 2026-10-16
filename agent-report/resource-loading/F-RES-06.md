# F-RES-06 Async resource loading can lose a wakeup

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Resource loading. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Completion before waiter registration can strand synchronous overlay rendering; completion fields also race.

**Verdict: confirmed. Severity: Medium, main-loop availability.**

The authenticated dependency can complete a resource before `await()` registers a waiter, leaving the compositor's synchronous text renderer waiting indefinitely. The resource's atomic shared-pointer lifetime does not synchronize its separate completion fields.

**Source-confirmed evidence:** `src/render/Renderer.cpp:1702-1705` performs `enqueue(res)` immediately followed by `await(res)`, without a completion check. `HG:src/resource/AsyncResourceGatherer.cpp:32-36` creates a condition variable and waits only for `awaitingEvent`; `HG:src/resource/resources/AsyncResource.hpp:6-10` initializes that event to false. The worker (`AsyncResourceGatherer.cpp:64-72`) records completion for the waiter only inside this conditional:

```cpp
if (r->m_impl->awaitingCv) {
    r->m_impl->awaitingEvent = true;
    r->m_impl->awaitingCv->notify_all();
}
r->m_ready = true;
```

If completion occurs before waiter registration, no durable event is set. A subsequent `await()` cannot observe `m_ready`, because its predicate never checks that atomic. The job is removed from the queue (`58-60`), so no later normal notification is guaranteed. This is a closed source-level missing-completion-state path, not merely a speculative spurious-wakeup concern. The worker also accesses the unique condition-variable pointer and non-atomic event without `awaitingMtx`; atomic ownership of `res` does not make those accesses race-free.

**Call/input chain and preconditions:** configuration-file errors reach the error overlay (`src/errorOverlay/Overlay.cpp:94-95,98-133`), which passes `STextResourceData` to this synchronous renderer. The optional debug-overlay warning does likewise (`src/debug/Overlay.cpp:356-374`). Resource completion scheduling can interleave with waiter registration during these ordinary operations; no malicious file or external client is necessary. The result is a possible main-thread hang, not demonstrated physical lockscreen acceptance or universal reproducibility.

**Corrections:** the lockscreen TTY string calls the **plain-text overload** (`Renderer.cpp:1729`), which renders synchronously without this gatherer. Window titles and notification text also use that overload. A timeout alone is not a complete correction for the data races or condition-variable lifetime.

**Minimal fix direction:** maintain one durable completion state under the same mutex used by the wait; always publish completion, then notify; give the condition variable stable lifetime; synchronize queue wake/exit flags as well. Verify completion-before-wait, completion-during-wait, and failure completion in dependency tests. No timing-amplification harness was run here.

[Full dossier-level verification report](_evidence/full-audit-11-resource-loading.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/11-resource-loading.md). These remain agent evidence, not human-confirmed material.
