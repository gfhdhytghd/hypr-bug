# F-BOOT-04 Runtime-directory validation is too weak

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unsafe runtime roots are accepted, but private instance directories block direct cross-user reads and unlinking. Parent-directory replacement remains a conditional namespace-integrity problem.

**Claim.** Insufficient runtime-directory validation places IPC sockets, logs, and the lock file beneath attacker-controlled directories, enabling cross-user reads and socket substitution.

**Evidence — source-confirmed.** `src/main.cpp:202–205` rejects an unset/empty `XDG_RUNTIME_DIR`. `src/Compositor.cpp:196–205` appends `/hypr`, rejects a prefix starting `/hypr`, and merely warns outside `/run/user`:

```cpp
if (!m_hyprTempDataRoot.starts_with("/run/user"))
    std::println("[!!WARNING!!] XDG_RUNTIME_DIR looks non-standard. Proceeding anyways...");
```

The code checks existence/directory type but not owner or permissions (`215–220`). It chooses a random/time/commit signature (`207–213`), rejects an existing instance path (`224–227`), and checks `mkdir(instancePath, S_IRWXU)` for failure (`229–232`). This last checked mkdir prevents simply precreating a chosen instance directory; the dossier misses that guard. Symlinked or wrongly owned ancestors are not rejected.

**Full dataflow — source-confirmed.** Launch environment → compositor runtime root/instance path → logger filename via `src/debug/log/Logger.cpp:37–43` → command socket at `src/ipc/s1/Unix.cpp:241–264` and event socket at `src/ipc/s2/Unix.cpp:92–112` → `hyprland.lock` at `src/Compositor.cpp:775–782`. The lock stores PID and Wayland socket name; startup writes it at line 830. Command-socket path length is checked before bind (`s1/Unix.cpp:244–248`). The Wayland listening socket uses libwayland separately (`Compositor.cpp:383–417`); it is not one of the instance's `.socket*` files.

For command clients, `hyprctl/src/main.cpp:87–94` uses its own runtime environment and `234–245` connects to the derived path and writes the request without verifying the server's UID there. Server-side `SO_PEERCRED` obtains the connecting client's PID (`src/ipc/s1/Unix.cpp:54–63,293–300`); it does not authenticate the server to hyprctl or secure a writable parent namespace.

**Permissions and threat boundary — source-confirmed.** Another UID owning a writable parent of the private instance can affect the instance pathname at the parent level, including directory-name substitution, absent sticky/ACL/other restrictions. The original `0700` child still prevents that UID from traversing it, reading its lock/log, or directly unlinking its sockets. Namespace substitution can redirect future path-based clients to a replacement directory; it neither exposes existing descriptor contents nor automatically grants control over the original compositor. Clients would need the corresponding runtime root/signature and make a later connection. This is the remaining conditional integrity concern, not the dossier's direct inner-socket unlink chain.

**Severity.** Medium conditional hardening/integrity issue when the supplied root or its namespace is controlled by another actor. Correctly owned private runtime roots close the cross-user chain. A normal Wayland client cannot change the compositor's original `XDG_RUNTIME_DIR`. Neither read access to the original lock nor session takeover is established just from owning an ancestor.

**Corrections.** Source line numbers shifted slightly (warning at 204–205, mkdir at 229). A filesystem check followed by mkdir is not sufficient evidence of a practical attacker race: the checked private instance creation is a meaningful guard. Requiring exactly `/run/user/<uid>` is too narrow for valid custom runtimes and non-Linux deployments; the security properties are ownership, permissions, directory identity, and safe traversal.

**Minimal fix direction.** Validate a supplied runtime directory as an absolute, private directory belonging to the effective user, and create/use descendants through validated directory descriptors. Handle existing directories and symlinked components explicitly. Consider peer-UID verification in clients as defense in depth. No runtime-directory substitution, IPC call, or desktop connection was performed.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
