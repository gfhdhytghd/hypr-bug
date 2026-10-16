# F-RULE-02 Execution-token rules trust client-supplied environment

- Agent confirmation: `rejected`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Permissions / dialogs / tokens. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Launch tokens are random per spawn and rules are consumed on mapping; the alleged plaintext configuration secret and arbitrary self-declaration are absent.

**Claim.** Execution rules use a fixed configuration secret that an unrelated client can declare in its environment. **Verdict: rejected at this upstream baseline.**

**Evidence — source-confirmed.** `src/config/supplementary/executor/Executor.cpp:128-140` generates a token for each ruled launch:

```cpp
const auto TOKEN = g_pTokenManager->registerNewToken(nullptr, std::chrono::seconds(1));
const auto PROC  = spawnRawProc(args, pInitialWorkspace, TOKEN);
```

`src/managers/TokenManager.cpp:13-24,27-31` uses `uuid_generate_random` and checks for collisions in its token map. `Executor.cpp:66-70` registers the generated token and spawned PID on the rule. `Executor.cpp:195-196` passes the token through the child environment. `src/desktop/rule/Rule.cpp:26-45,78-83` does not expose `exec_token` as a public property name; it is an internal match engine (`Rule.cpp:70-71`). The dossier's fixed-secret configuration example does not describe this path.

**Full match chain and guards — source-confirmed.** A user-configured ruled launch reaches `spawnWithRules`, token generation, child environment injection, then window mapping. `src/desktop/rule/windowRule/WindowRule.cpp:466-481` calls `getEnv()` only when `allowEnvLookup` is true; it must match the generated token if present, otherwise it may match the registered launch PID. The PID match is a fallback, not an additional requirement when a token exists. `src/desktop/rule/windowRule/WindowRuleApplicator.cpp:530-559` collects matching execution rules and unregisters them on the non-preread mapping pass. `src/desktop/rule/Rule.cpp:146-163` gives execution rules a one-minute expiry, with cleanup at `src/desktop/rule/Engine.cpp:30-35`. Expiry cleanup is opportunistic; the one-second token-manager expiry is not checked by the environment matcher and must not be advertised as a one-second matching limit.

**Reachability, severity, and corrections.** No fixed known secret exists for an arbitrary unrelated client to self-declare. Descendants can inherit a launch token; that is consistent with matching launched applications and does not establish an authorization bypass. The token is a bearer value, so disclosure would be a different precondition. No token disclosure or security-sensitive rule takeover is established here. On Linux, a `setenv()` immediately before opening a window also does not reliably replace the initial environment exposed by `/proc/PID/environ`; the dossier's proposed `main()` sequence is not sufficient evidence.

**Minimal direction.** No change is needed for the alleged missing randomization: it already exists. Keep window-rule matching distinct from authentication. If execution-rule expiry needs stronger guarantees, enforce expiry at match time as a separate narrowly scoped change.

[Full dossier-level verification report](_evidence/full-audit-14-permission-dialogs-tokens.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md). These remain agent evidence, not human-confirmed material.
