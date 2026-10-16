# F-PROTO-04 Unbounded per-surface commit / fence queues

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`, plus empirical reproduction in the nested harness; current-version and physical-display acceptance must be established separately.

- Empirical: Empirically tested: survived (no growth). An unsignaled exported timeline is rejected at import on the real XPS session; the unbounded-queue path was not reached by this vector (needs real GPU dma-buf fences).

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: A valid unsignaled fence retains an unbounded state queue; scans amplify work. Raw eventfd is not a dma-buf fence. [EXECUTED real session: unsignaled exported timeline rejected at import, no queue growth; valid-GPU-fence path not exercised]

**Empirical reproduction — executed (negative).** Real XPS session (Hyprland 0.56.2, 2026-10-15): a client issuing 2000 unsignaled acquire-point commits via an exported syncobj timeline did not grow Hyprland RSS (216 MB → 216 MB, +0); the unsignaled exported timeline was rejected at import ("Timeline failed importing"), so the unbounded-queue path was not reached. Triggering it needs a valid but unsignaled GPU dma-buf/sync_file fence, which this client could not construct — growth neither confirmed nor refuted for that harder vector.

**Verdict: confirmed. Severity: Medium.**

The per-surface state queue has no budget or progress timeout. A valid fence-blocked front state prevents later states from draining or merging, retaining client-controlled state and buffer references.

**Evidence — source-confirmed.** Core `wl_surface.commit` reaches `src/protocols/core/Compositor.cpp:193-214`, which copies pending state into the queue and subsequently schedules it. `src/protocols/types/SurfaceStateQueue.cpp:17-18` performs unconditional deque insertion. Processing first checks the front lock:

```cpp
if (front->isLocked())
    break;                       // SurfaceStateQueue.cpp:97-98
// merging following states happens only after that check (:100-105)
```

Thus the available merge optimization does not collapse states behind a blocked front. `SSurfaceState` retains a `CHLBufferReference` and texture; copying buffer references increments buffer locking (`src/protocols/types/Buffer.cpp:57-59,66-73`). Each enqueued state can also retain regions, callbacks, and presentation feedback. This is not merely an ever-growing count of wire requests after they have been processed.

**Fence path — source-confirmed.** Implicit synchronization exports genuine sync files from a valid imported dma-buf at `Compositor.cpp:200-203`, via `src/protocols/types/DMABuffer.cpp:94-120`. `scheduleState`/`drainSyncFds` (`Compositor.cpp:607-656`) registers a readable waiter and leaves the fence lock held while it waits. There is no timeout here. Explicit sync uses validated imported DRM timelines (`src/protocols/DRMSyncobj.cpp:118-132`), applies acquire/release-point preconditions (`:77-110`), and sets `LOCK_REASON_FENCE` at `:106`. Its waiter path is `:30-32` → `src/helpers/sync/SyncTimeline.cpp:67-92` → `Compositor.cpp:615-623`. On supported DRM backends, a registered acquire wait that remains unsignaled can therefore hold the front. Continued accepted surface commits retain later queue entries until the block resolves or the surface/client is torn down.

**Amplification — source-confirmed.** `SurfaceStateQueue.cpp:79-90` linearly searches for a state, while `:51-64` scans the whole queue on `unlockFence`. A succession of such calls over a retained growing queue can require quadratic aggregate scans. This is conditional on the event/commit pattern; a single wakeup is linear and `tryProcess` can coalesce ready adjacent entries once the head unlocks. Kernel/GPU costs and exact runtime scaling were not measured.

**Reachability/preconditions.** A connected client with usable buffer/fence facilities is required. Explicit sync is only advertised when renderer and DRM timeline support are available (`src/managers/ProtocolManager.cpp:239-255`); valid implicit dma-buf fences likewise depend on the buffer/driver path. An arbitrary raw eventfd is not accepted as an imported dma-buf or sync file. Hyprland's internal eventfd in `SyncTimeline.cpp:80-92` is registered with the DRM syncobj API; it does not validate the dossier's proposed eventfd-as-buffer-fence shortcut. A failed waiter registration is explicitly unlocked at `Compositor.cpp:621-622`, so failure to obtain a waiter is not a permanently locked-head trigger.

**Severity/corrections.** Medium for unbounded server-side resource retention and CPU amplification, not an executed compositor crash or universally available GPU-free DoS. FD/GEM growth requires retained distinct resources/waiters; repeatedly referring to one buffer does not create one new GEM object per state. The dossier's implicit/explicit-fence terminology and stale `603-612` locations are corrected above. Ready-state merging combines damage (`SurfaceState.cpp:135-138`), but a blocked queue does not merge on every enqueue. A null-buffer attach explicitly clears the queue (`Compositor.cpp:183-190`), and surface/client teardown reclaims owned state; the issue requires the client to keep the blocked surface alive.

**Fix direction.** Bound outstanding state/resources per surface and client, apply backpressure or a safe protocol error on budget exhaustion, and avoid full-queue scans for individual fence readiness. Preserve required buffer-release and presentation-feedback semantics; blindly dropping arbitrary states is not necessarily protocol-correct. No GPU access or fence reproduction was attempted.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
