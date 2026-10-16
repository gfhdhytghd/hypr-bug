# F-TMR-01 Screenshare stop timer is not unregistered

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Event-loop timers. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Ownerless timers are skipped and garbage-collected; session destruction cannot produce the claimed permanent timer leak or late callback.

The dossier claims that screenshare sessions register a stop timer without unregistering it, leaving timers retained after session destruction. It additionally claims a late inactive event, cumulative event-loop work, and a latent use-after-free pattern from a raw `this` capture.

**Verdict: rejected. Severity: none for the claimed defect.** The missing explicit `removeTimer()` is real, but it is supported by the event-loop manager's ownership contract. Dispatch suppresses callbacks after the session releases its timer, and deferred recalculation reclaims the timer. The dossier overlooked both guards.

### Timer ownership and the decisive guards

**Source-confirmed — registration and teardown.** `src/managers/screenshare/ScreenshareSession.cpp:88–97` constructs the 500 ms timer and registers it. The timer is an `SP<CEventLoopTimer>` member at `src/managers/screenshare/ScreenshareManager.hpp:86`. Its callback captures a raw `this`, not a strong session pointer:

```cpp
m_shareStopTimer = makeShared<CEventLoopTimer>(
    std::chrono::milliseconds(500),
    [this](SP<CEventLoopTimer> self, void* data) {
        screenshareEvents(false);
    },
    nullptr);
```

`src/managers/screenshare/ScreenshareSession.cpp:55–68` shows that the destructor calls `stop()`, and `stop()` changes state and emits the stopped signal without explicitly removing or cancelling the timer. On completion of destruction, the timer member releases its strong reference. The callback does not own the session or its timer, so it creates no ownership cycle. The only accesses to this private timer outside its declaration and initialization are the frame timeout update at `src/managers/screenshare/ScreenshareFrame.cpp:179`; that expression does not retain another `SP`.

**Source-confirmed — automatic cleanup is the documented contract.** `src/managers/eventLoop/EventLoopManager.hpp:54–56` explicitly documents `addTimer()` as: `// Note: will remove the timer if the ptr is lost.` The manager stores `std::vector<SP<CEventLoopTimer>>` at line 97. The implementation enforces two distinct reference-count thresholds:

```cpp
// src/managers/eventLoop/EventLoopManager.cpp:142–149
const auto CPY = m_timers.timers;
for (auto const& t : CPY) {
    if (t.strongRef() > 2 /* if it's 2, it was lost. Don't call it. */ &&
        t->passed() && !t->cancelled())
        t->call(t);
}
scheduleRecalc();

// src/managers/eventLoop/EventLoopManager.cpp:191–195
m_timers.recalcScheduled = false;
std::erase_if(m_timers.timers, [](const auto& t) { return t.strongRef() <= 1; });
```

The reference accounting is:

| Point in lifecycle | Strong timer references | Result |
| --- | --- | --- |
| Live session, outside dispatch | Session member + manager vector = 2 | Timer remains registered. |
| Live session, dispatch snapshot exists | Session member + manager vector + snapshot = 3 | Callback may run if due and uncancelled. |
| Destroyed session, dispatch snapshot exists | Manager vector + snapshot = 2 | `strongRef() > 2` fails; callback is skipped. |
| Destroyed session, deferred recalculation | Manager vector = 1 | `strongRef() <= 1` removes and destroys the timer and closure. |

This also covers a different callback destroying the session earlier in the same dispatch: the snapshot still has a timer reference, but the threshold is tested immediately before that timer's callback. `addTimer()` also rejects duplicate vector entries (`src/managers/eventLoop/EventLoopManager.cpp:152–156`), so duplicate registration does not manufacture extra strong references here.

**Source-confirmed — the dependency implements actual strong-reference counts.** Hyprland's `SP` alias is `Hyprutils::Memory::CSharedPointer` (`src/helpers/memory/Memory.hpp:8–13`). In the installed dependency, `/usr/include/hyprutils/memory/SharedPtr.hpp:40–42` increments the count when an `SP` is copied; lines 70–72 decrement it on destruction; lines 160–162 implement `strongRef()` as `impl_->ref()`. Lines 179–205 release the object when the final strong reference disappears. These are local dependency-header observations, not files pinned by the Hyprland commit.

**Source-confirmed — reclamation is deferred, not permanent retention.** `scheduleRecalc()` queues `nudgeTimers()` through the Wayland idle callback (`src/managers/eventLoop/EventLoopManager.cpp:179–188,215–235`). The dispatch snapshot has gone out of scope before that deferred callback runs. Timer readiness is delivered through `timerWrite()` and `enterLoop()` (`src/managers/eventLoop/EventLoopManager.cpp:51–60,124–125`). Every timer dispatch schedules recalculation, even if all callbacks were skipped. `nudgeTimers()` initializes a 10-second fallback deadline and programs the timerfd (`src/managers/eventLoop/EventLoopManager.cpp:197–212`), so an ownerless disarmed timer also has a future cleanup opportunity while the event loop progresses. This is not an immediate-destruction or hard wall-clock latency guarantee.

Consequently, already-destroyed sessions do not permanently accumulate one timer per creation/destruction cycle. The vector scan is linear in its current contents, including live timers and temporarily pending cleanup; it is not linear in every session ever destroyed. A burst can leave transient entries until recalculation, which does not establish the dossier's monotonic leak or cumulative latency claim.

### Reachability and teardown chain

**Source-confirmed — valid protocol path.** The session creation/destruction path does exist. It does not require malformed object arguments, guessing object IDs, or bypassing libwayland validation:

1. Screenshare protocols are registered at `src/managers/ProtocolManager.cpp:228–232`. A client with access to the compositor's Wayland connection and visibility of the capture globals can bind the output-source and image-copy managers. `src/Compositor.cpp:267–271` allows globals for clients not marked sandboxed; sandboxed clients are restricted by the whitelist at `src/managers/ProtocolManager.cpp:348–403`, which excludes these capture globals. Thus “any client” needs this visibility qualification.
2. A valid output capture source is created by `COutputImageCaptureSourceProtocol::bindManager()`'s request handler (`src/protocols/ImageCaptureSource.cpp:70–87`). The output resource and its monitor are checked before source allocation.
3. `CImageCopyCaptureProtocol::bindManager()`'s session handler resolves the source, checks the options, and allocates `CImageCopyCaptureSession` (`src/protocols/ImageCopyCapture.cpp:451–467`). Its constructor calls `Screenshare::mgr()->newSession()` (`src/protocols/ImageCopyCapture.cpp:36–43`), which validates the monitor and creates the internal session (`src/managers/screenshare/ScreenshareManager.cpp:46–57`). `CScreenshareSession::init()` registers the timer (`src/managers/screenshare/ScreenshareSession.cpp:78–97`). The window variant checks that the window is mapped (`src/managers/screenshare/ScreenshareManager.cpp:74–85`).
4. The protocol session uniquely owns the internal session (`src/protocols/ImageCopyCapture.hpp:49–51`). The screenshare manager's general session index is weak (`src/managers/screenshare/ScreenshareManager.hpp:239`), so that index cannot keep it alive. The protocol session's destroy request and resource-destruction handler both erase the wrapper from its owning vector (`src/protocols/ImageCopyCapture.cpp:20–21,494–496`). Its destructor stops the internal session (`src/protocols/ImageCopyCapture.cpp:52–57`); member destruction then destroys the unique internal session and releases its timer member.
5. A subsequent timer dispatch sees at most the vector plus snapshot references. The callback is skipped at `src/managers/eventLoop/EventLoopManager.cpp:145`; deferred cleanup removes the timer at line 195. These are the guards that break the dossier's proposed failure chain.

**Source-confirmed — permissions do not change the ownership result.** `ecosystem:enforce_permissions` defaults to false (`src/config/values/ConfigValues.cpp:717`); `clientPermissionMode()` returns allow when it is zero (`src/managers/permissions/DynamicPermissionManager.cpp:79–84`). The image-copy session allocation above does not call that permission function. Frame copying later checks screencopy permission (`src/managers/screenshare/ScreenshareFrame.cpp:162–169`). No copied frame or permission grant is needed merely to initialize the timer. This corroborates the narrow default-setting fact in the dossier without importing any verdict on its cross-referenced permission finding. Even an authorized, unrestricted client does not bypass the timer ownership guards.

**Source-confirmed — the dossier conflates capture protocols and ownership.** Legacy `zwlr_screencopy_manager_v1` has output/region capture handlers (`src/protocols/Screencopy.cpp:19–22`), not a `create_session` request. These use `getManagedSession()` (`src/protocols/Screencopy.cpp:35–37`), whose lookup reuses sessions by client, share type, and target and stores them in `m_managedSessions` (`src/managers/screenshare/ScreenshareManager.cpp:110–157`; ownership at `ScreenshareManager.hpp:243–250`). Destroying a legacy frame is therefore not proof that its internal managed session was destroyed. Any separate question about how long those manager-owned sessions survive is distinct from an event-loop timer surviving its destroyed owner; this report does not treat that separate lifecycle as a confirmed leak.

### Late callbacks, member access, and severity

**Source-confirmed — the callback does access the session.** The dossier says the callback currently avoids dereferencing `this`. In fact, it invokes the non-static member `screenshareEvents(false)`, which writes `m_stale`, tests `m_sharing`, and conditionally accesses the type and name (`src/managers/screenshare/ScreenshareSession.cpp:153–169`):

```cpp
m_stale = !startSharing;
// ...
} else if (!startSharing && m_sharing) {
    m_sharing = false;
    // Post inactive events and emit the state signal.
}
```

Safety after destruction comes from the reference-count gate, not from an accidentally harmless callback body. The raw capture can remain in an uncalled closure pending cleanup without dereferencing the destroyed object.

**Source-confirmed — `stop()` without destruction is also different.** Window unmap and monitor disconnect listeners call `stop()` (`src/managers/screenshare/ScreenshareSession.cpp:22,34,103`). Some owners can retain a stopped session. Its timer may then fire once while the session is still alive, but `stop()` has already called `screenshareEvents(false)` (`ScreenshareSession.cpp:61–67`), which clears `m_sharing` before emitting inactive state. A later false call does not issue a duplicate inactive broadcast because the `!startSharing && m_sharing` branch is false. `CEventLoopTimer::call()` resets its expiry before invoking the callback (`src/managers/eventLoop/EventLoopTimer.cpp:41–44`), and this callback does not rearm itself. Successful frame copies separately rearm the timeout (`src/managers/screenshare/ScreenshareFrame.cpp:176–179`).

**Severity justification.** None for F-TMR-01: the proposed persistent timer/closure leak, post-destruction inactive event, monotonically growing scan cost, and use-after-free trigger are not established at the pinned baseline. The lack of an explicit unregister call alone is not a defect under the documented ownership contract. No compositor crash, memory-growth measurement, event ordering observation, or latency measurement was executed or is claimed here.

### Corrections and minimal fix direction

**Source-confirmed corrections.** The dossier's main session line numbers remain accurate upstream. Its decisive omissions are `EventLoopManager.cpp:145` and `:195`, not shifted source locations. Its “callback does not dereference `this`” explanation is incorrect; its “one additional broadcast after destruction” and “permanent retention” conclusions do not follow. Its legacy screencopy request sketch also names the wrong request and assumes frame destruction destroys the managed session.

No correctness fix is required for the claimed post-destruction timer leak. Optional hardening would cancel and unregister the timer when stopping a session, guarded for a missing event-loop manager, to express cancellation directly and avoid the harmless callback on a still-live stopped session. Cancellation and removal should be considered together because removal alone does not destroy a timer already present in a dispatch snapshot. This is a hardening direction, not an upstream patch or a validated runtime fix.

The dossier's weak-pointer suggestion also needs ownership-aware implementation: the internal session is `UP`-owned. In the installed hyprutils, `/usr/include/hyprutils/memory/WeakPtr.hpp:175–185` explicitly disallows locking a `WP` over a `CUniquePointer`; `expired()` accounts for destruction at lines 154–158. A weak observer would need an appropriate liveness check rather than a blind `.lock()`, and initialization after `m_self` is assigned by the factory (`src/managers/screenshare/ScreenshareManager.cpp:52–55,139–145`). No such change was made.

### Unnumbered control sweep in the dossier

These are supporting checks for the dossier's control paragraph, not additional finding IDs or a new audit of every timer callback.

| Dossier control statement | Verification at the pinned baseline |
| --- | --- |
| InputCapture explicitly removes its timer. | **Source-confirmed.** `src/protocols/InputCapture.cpp:77–83` stops repeat, unregisters the timer, and resets its member. This corroborates that implementation, but does not make explicit unregistering mandatory for every owner. |
| Lua `cleanTimers()` runs before `lua_close()`. | **Source-confirmed.** `src/config/lua/ConfigManager.cpp:502–512` cancels and unregisters timers before releasing their Lua references. Destruction (`:424–435`) and state reinitialization (`:515–526`) clean timers before closing Lua. This verifies the stated cleanup ordering, not every possible Lua callback/reentrancy property. |
| WorkBufferPool is safe because it is a process-lifetime singleton. | **Source-confirmed correction.** It is instantiated as two members of `CMonitorResources` (`src/output/MonitorResources.hpp:56–57`), not a single process-lifetime instance. Its destructor explicitly cancels and removes its timer (`src/output/WorkBufferPool.cpp:38–43`). |
| The other timer sites can all be treated as process-lifetime `[this]` captures. | **Source-confirmed correction.** The Unix request timeout belongs to `CUnixPeer` and captures a weak peer (`src/ipc/s1/Unix.hpp:19–24,54–55`; `src/ipc/s1/Unix.cpp:79–91`), whereas the follow timer captures the implementation (`Unix.cpp:374–388`). The blanket lifetime/capture characterization cannot substitute for per-owner analysis. |
| F-TMR-01 is the only missing cleanup and hence the only defect. | **Source-confirmed rejection of the inference.** The shared event-loop ownership contract permits implicit cleanup. The main finding is a false positive; the examples above do not establish an exhaustive absence of other event-loop defects. |

### Verification boundary

All behavioral evidence in this report is **source-confirmed**. The pinned worktree identity was checked and the reviewed timer/session/protocol source was checked against `HEAD` without modification. No standalone harness was necessary to settle the reference-count guards; no runtime behavior is labeled **executed**. Only this report was written in the tracker; no README, dossier, upstream source, or git state was changed.

Verdict counts: **0 confirmed, 0 partially, 1 rejected, 0 inconclusive**.

[Full dossier-level verification report](_evidence/full-audit-15-eventloop-timers.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/15-eventloop-timers.md). These remain agent evidence, not human-confirmed material.
