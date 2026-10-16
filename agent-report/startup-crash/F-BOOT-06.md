# F-BOOT-06 Systemd notification socket validation / format support

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Length and abstract-address handling are defective; environment-selected notification routing is intentional, and the alleged fd-prefix compatibility requirement is unsupported.

**Claim.** An unvalidated `NOTIFY_SOCKET` redirects readiness messages; address/state truncation and missing `fd:` support diverge from systemd notification behavior.

**Evidence — source-confirmed.** `src/helpers/SdDaemon.cpp:23–39` opens an AF_UNIX datagram socket and reads the process's `NOTIFY_SOCKET`; absent environment yields zero. Lines 41–55 copy at most `sizeof(sun_path)-1` bytes into a zero-initialized structure, translate an initial `@`, connect using the whole structure, and cap the message at 128 bytes:

```cpp
size_t addrLen = strnlen(addr, sizeof(unixAddr.sun_path) - 1);
// ... copy and optional @ -> NUL conversion ...
if (connect(fd, rc<const sockaddr*>(&unixAddr), sizeof(struct sockaddr_un)) < 0)
    return -errno;
ssize_t stateLen = strnlen(state, 128);
```

Overlong addresses are silently changed rather than rejected. On Linux an abstract address's bytes and length identify its name: passing the full structure includes trailing zeros and does not match the normal short abstract name. The prefix conversion alone is insufficient. This corrects the dossier's assumption that `@x` necessarily reaches the ordinary socket named `x`.

**Contract check — source-confirmed from installed primary documentation.** The supplied host's `/usr/share/man/man3/sd_notify.3.gz`, uncompressed lines 488–515 and 858–862, explicitly makes `NOTIFY_SOCKET` the service manager's selected destination, supports `/` and `@`, and documents vsock formats, not `fd:`. Its standalone C implementation validates family/length (589–596) and passes `offsetof(sockaddr_un, sun_path) + path_length` to `connect` (608). This establishes the relevant UNIX-address mismatch; it is not evidence for a missing `fd:` requirement at the pinned Hyprland baseline. FD passing/FDSTORE capabilities are different from a `fd:` destination string.

**Call chain and guards — source-confirmed.** Supervisor/launcher environment → systemd-enabled build → `NSystemd::sdBooted() > 0` plus `!Env::envEnabled("HYPRLAND_NO_SD_NOTIFY")` → startup `READY=1` (`src/Compositor.cpp:821–825`) or cleanup `STOPPING=1` (`582–583`) → sdNotify above. Both call sites ignore its return. These fixed strings are shorter than 128 bytes, so state truncation has no demonstrated effect at these callers. The pathname and abstract-address handling does affect actual call inputs. A readiness-dependent supervisor could remain waiting when notifications fail; this requires such a supervisor setup.

**Execution limit — executed, blocked before notification.** An isolated harness `notify_check.cpp` compiled the actual baseline `SdDaemon.cpp`. Socket binding to its own scratch receiver and private abstract test names was denied with `Operation not permitted`; `notify-results.txt` records that setup failure. No `sdNotify` exchange occurred, and no desktop/service socket was contacted. The address-length conclusion is source-confirmed, not runtime reproduced.

**Severity and corrections.** Low for the format/validation defect; no meaningful secret is contained in these fixed status messages. Choosing the destination from the service environment is the protocol's purpose. A different UID cannot ordinarily change that environment. Requiring a root-owned endpoint or an `@/run/systemd` prefix would break legitimate user managers/custom supervisors, and `SO_PEERCRED` is not a general authentication solution for this unconnected-datagram protocol. The dossier's redirection-security and `fd:` compatibility claims are not established.

**Minimal fix direction.** Prefer libsystemd's supported helper when available, or validate accepted UNIX formats and lengths, compute the correct address length, avoid arbitrary state truncation, and expose notification errors where useful. Do not impose a root-only destination requirement on user-session notifications.

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
