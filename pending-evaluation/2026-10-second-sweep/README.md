# 2026-10 Second Sweep — raw agent output (pre-verification)

Second full-codebase wave on the fork (`gfhdhytghd/Hyprland`, branch `main`,
baseline **`e38076ac9`** — supersedes the `397cb0efb` baseline of the first
batch). Static review only; no repros authored per current agreement.
Findings pinned to the `e38076ac9` tree (line numbers re-verified there).

Dossiers here are candidates; per-finding promotion to `agent-report/<category>/`
follows the tracker's evaluation workflow.

| Dossier | Area | Findings | Notes |
|---|---|---|---|
| `19-rule-engine.md` | rule engines / applicators (layer rules, tags, overrides) | 9 (F-MATCH-01..09) | F-MATCH-01 = layer-rule fail-open incl. above_lock → lockscreen click-hijack candidate |
| `20-output-power.md` | output/power/mode/buffer lifecycle (impl layer) | 6 (F-OUT-01..06) | DPMS desync lies, retry-timer SEGV, rejected-modeset residue |
| `16-hooks-pluginapi.md` | plugin runtime API / HookSystem / EventBus | 10 (F-HOOK-01..10) | unload has no listener/decoration/promise ledger; unload ungated |
| `18-layer-lifecycle.md` | layer-shell surface lifecycle / lockscreen / popups / exclusive zones | 8 (F-LAY-01..08) | namespace spoof -> above_lock locksurfing; ghost EXCLUSIVE keyboard hijack after monitor disconnect |
| `21-standalone-protocols.md` | small standalone protocols + generated-wrapper destroy semantics | 1 informational + negative results | documents the hyprwayland single-fire teardown invariant |
| `17-animation.md` | animation manager & view controllers & fadeouts | 7 (F-ANIM-01..07) | all upstream-inherited (zero diff vs origin/main); tick core verified safe |
