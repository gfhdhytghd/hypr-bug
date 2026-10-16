# F-TOK-01 Control record: token-manager checks and low-risk notes

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: UUID generation and lazy expiry hold; activation consumes its authorization entry, while retained live token resources can outgrow the delivery-list cap.

**Claim.** Token generation and lazy expiry look sound; non-consuming lookup might permit replay, and registration has no total cap. **Verdict: partially: control checks and a boundedness concern hold; replay does not follow from lookup alone.**

**Evidence — source-confirmed.** `src/managers/TokenManager.cpp:13-24` calls `uuid_generate_random` and retries on a map collision. `TokenManager.cpp:27-31` inserts a shared token without a quota. `TokenManager.cpp:34-43` removes expired entries on lookup and returns an existing entry without consuming it:

```cpp
std::erase_if(m_tokens, [&NOW](const auto& el) { return el.second->m_expiresAt < NOW; });
// then return the entry, if present
return m_tokens.at(uuid);
```

No periodic cleanup is shown, so the map can retain expired entries until a later lookup. This review confirms use of libuuid's random-generation API, not the host's exact entropy source, entropy quality, or randomness at runtime.

**Activation chain and replay guard — source-confirmed.** A bound activation manager creates a token resource at `src/protocols/XDGActivation.cpp:77-82,114-122`. Commit is allowed once per resource (`19-39`) and requires a nonzero serial valid for the requesting client's seat. `src/managers/SeatManager.cpp:134-153` rejects absent seat resources; its normal event serials are consumed when validated (default `erase=true` at `src/managers/SeatManager.hpp:109`), while the current enter serial has a distinct successful check. An accepted commit registers a twelve-month token (`XDGActivation.cpp:42`) and adds it to `m_sentTokens`. Activation looks up **that separate list**, not `CTokenManager::getToken`, and erases its entry before attempting the window activation (`XDGActivation.cpp:83-102`). Repeating an already spent token does not pass this list check. Token-resource destruction calls manager removal (`64-66,110-111`).

**Quota distinction — source-confirmed.** `XDGActivation.cpp:50-59` caps each client's sent-token delivery list at ten by erasing the oldest list entry. That does not erase its token-manager entry or destroy its live token resource. Thus issuance can retain additional resource-backed manager entries if those resources remain alive, subject to the seat-serial admission guard. This is a Low resource-retention concern, not an unconditional “any client can allocate infinite accepted tokens” claim. No load test or resource exhaustion was attempted, and a delivery-list cap must not be reported as a global manager-memory cap.

**Other consumers — source-confirmed.** The dossier's “activation only” impression is incomplete: ruled launches register tokens (`src/config/supplementary/executor/Executor.cpp:131`), and initial-workspace tracking does so at `165-168`. The one-shot workspace mode explicitly removes a token at `src/desktop/view/window/Window.cpp:1182-1201`; persistent mode deliberately keeps it (`1202-1208`) and has later owner cleanup at `496-504`. Shared lookup is useful for these different semantics and is not intrinsically a replay flaw.

**Severity, corrections, and minimal direction.** Retain the random-generation/expiry check as an informational control result. Treat manager quotas and lifecycle cleanup as Low defense against retained resources; close the resource and token-map lifetime gap when evicting issuance entries, or introduce an appropriate quota. A separate `consumeToken` API can clarify genuinely one-shot uses but must not break intentional persistent-workspace behavior. No confirmed activation replay is reported.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
