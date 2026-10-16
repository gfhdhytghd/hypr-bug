# Boot chain and crash handling: upstream verification

- Baseline: upstream Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`), verified from the supplied detached worktree.
- Dossier: [12-bootchain-crash](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md).
- Date: 2026-10-15.
- Method: adversarial source review of the pinned upstream snapshot and available dependency headers; isolated checks, if performed, are individually labeled. No desktop connection, compositor execution, full build, network access, or upstream modification. All upstream references below refer to this baseline.
- Status: complete. All ten dossier IDs are evaluated; counts: 2 confirmed, 6 partially, 1 rejected, 1 inconclusive.
- Evidence boundary: source-confirmed conditional defects do not establish a default cross-user exploit. The isolated checks exercised helper functions only; no compositor/watchdog, hostile payload, symlink overwrite, or directory-substitution test was run. The notification socket harness compiled but could not bind its own receivers under the sandbox.
- Scratch evidence: `/home/wilf/data/.claude-scratch/agent-reports/work/12-bootchain-crash/` contains `checks.cpp`, `checks-results.txt`, `checks-home.txt`, `notify_check.cpp`, `notify-results.txt`, and the dependency disassembly. Helper checks used the installed hyprutils 0.14.2 and libstdc++; these dependencies are not claimed to be pinned by the Hyprland commit.
- The two example reports named in the brief were absent from the supplied tracker; verification follows the brief's required structure directly.

## Summary

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
| --- | --- | --- | --- | --- |
| F-BOOT-01 | confirmed | Medium (conditional file corruption/disclosure) | `src/debug/crash/CrashReporter.cpp:62–98` | Unsafe report creation is real; exploitation requires an attacker-writable report namespace and a crash. Default private cache directories block other users. |
| F-BOOT-02 | confirmed | Medium (conditional shell interpretation; crash robustness) | `src/debug/crash/CrashReporter.cpp:167–232`; `src/debug/crash/SignalSafe.hpp:123–179` | Crash reporting invokes shell helpers and interpolates the executable path; environment/path control is required. Independent client-to-code-execution escalation is unproved. |
| F-BOOT-03 | partially | Low (executable detection); conditional PATH hardening | `src/helpers/fs/FsUtils.cpp:83–107`; `start/src/helpers/Nix.cpp:48–69,84–121` | Executable detection is wrong; PATH execution is intentional. Both splitters remove empty components, contradicting the claimed lookup-to-current-directory mechanism. |
| F-BOOT-04 | partially | Medium (conditional runtime namespace integrity) | `src/Compositor.cpp:196–234`; `hyprctl/src/main.cpp:87–94,234–245` | Unsafe runtime roots are accepted, but private instance directories block direct cross-user reads and unlinking. Parent-directory replacement remains a conditional namespace-integrity problem. |
| F-BOOT-05 | rejected | None (intentional environment switch semantics) | `src/helpers/env/Env.cpp:5–7`; `src/Compositor.cpp:295–298,799–825` | Nonempty values except exactly zero intentionally enable these switches. Setting another process’s launch environment is an unmet prerequisite, not a demonstrated security bypass. |
| F-BOOT-06 | partially | Low (notification format/error handling) | `src/helpers/SdDaemon.cpp:23–58`; `src/Compositor.cpp:582–583,821–825` | Length and abstract-address handling are defective; environment-selected notification routing is intentional, and the alleged fd-prefix compatibility requirement is unsupported. |
| F-BOOT-07 | partially | Medium (conditional data-file clobber); Low robustness | `src/helpers/fs/FsUtils.cpp:12–54,70–80`; `src/managers/DonationNagManager.cpp:44–47,137–157` | Symlink-following truncation and directory races remain conditional on writable data storage. Donation startup guards missing data home; deterministic nullopt crash is overstated. |
| F-BOOT-08 | partially | Low (restart resource/robustness defects) | `start/src/core/Instance.cpp:47–94,157–219`; `start/src/main.cpp:104–120` | Parent allocations leak and initialized crashes restart without backoff. Early failures stop; the claimed multithreaded-fork deadlock and untrusted child-protocol boundary are unproved. |
| F-BOOT-09 | inconclusive | Informational (deployment-dependent confidentiality) | `src/Compositor.cpp:105–121`; no core suppression in `src/`, `start/`, `systemd/` | Abort can request a core, but production limits, collection policy, contents, and other-user readability are unverified. Directory permissions alone do not establish disclosure. |
| F-BOOT-10 | partially | Low (missing-HOME path handling); Informational controls | `src/helpers/MiscFunctions.cpp:54–75`; `src/debug/Overlay.hpp:59,79`; `src/config/supplementary/executor/Executor.cpp:23–49` | Missing HOME breaks tilde expansion; empty-string UB is false. Overlay retention is source-supported with weak keys; environment propagation and packaging notes are informational. |

## F-BOOT-01

**Claim.** Predictable crash-report creation follows pre-existing links, accepts existing directories without checking ownership, and overwrites existing files without truncating them.

**Evidence — source-confirmed.** `src/debug/crash/CrashReporter.cpp:62–78` selects the nonempty `XDG_CACHE_HOME`, otherwise `HOME/.cache`, appends `/hyprland`, and accepts `mkdir(..., S_IRWXU)` returning `EEXIST`. Lines 80–82 derive the basename from the PID. The decisive operation at line 96 is:

```cpp
reportFd = open(reportPath.getStr(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
```

There is no exclusive-create, no final-component symlink protection, no truncation, and no validation of an already existing file's owner or mode. Buffered report writing starts at `CrashReporter.cpp:100–115`; the destructor flushes at `src/debug/crash/SignalSafe.hpp:73–75,183–192`. Existing file contents are overwritten from offset zero; any longer suffix remains. The `0600` argument applies to newly created files only.

**Trigger and reachability — source-confirmed.** Startup environment/cache-directory state → `src/Compositor.cpp:295–298` installs the crash handler unless disabled → an actual SIGSEGV/SIGABRT reaching that handler → `Compositor.cpp:105–121` calls `createAndSaveCrash` → unchecked directory/name selection → the open and writes above. A pre-existing link or file must be installable in the chosen report namespace, its target must be writable by the compositor UID, and report creation must reach the open. No Wayland request in this dossier is proved to cause the initial crash. Another UID ordinarily cannot inject environment into an already running process or send these signals to it.

**Severity.** Medium under the dossier's explicitly compromised/shared cache-directory prerequisite. This is conditional file corruption and possible report disclosure; there is no demonstrated default cross-user path or arbitrary-content write. With a correctly private cache hierarchy, another UID cannot prepare the report basename. Newly created `0600` files do not become readable merely because another user owns a parent directory; disclosure additionally requires a writable pre-existing destination with permissions permitting that other user's reads. Kernel link protections, directory permissions, and target permissions can independently prevent a proposed redirection.

**Corrections.** Report text begins with a fixed header (`CrashReporter.cpp:102`), rather than attacker-selected interpreter syntax. Plugin metadata (`125–140`) comes from already loaded native code and supplies no new code-execution privilege. Neither turning a report into a valid startup script nor session takeover is established. Lack of `O_TRUNC` preserves a suffix; it does not append. PID predictability alone grants no directory access; redirection still requires preparing the relevant filename before the reporter opens it.

**Minimal fix direction.** Use a previously validated private directory and descriptor-relative exclusive creation with symlink rejection, retrying collisions with fresh names; fail closed on invalid directories and path overflow. Keep directory validation and allocation outside the signal handler where possible. Adding `O_TRUNC` alone worsens destruction of an existing target and does not repair the trust issue. No live crash or redirection test was performed.

## F-BOOT-02

**Claim.** Crash reporting invokes shell commands through inherited PATH, and inserts the compositor executable pathname into another shell command without quoting; signal-context allocation can also compromise report generation.

**Evidence — source-confirmed.** `src/debug/crash/SignalSafe.hpp:144–157` forks and invokes the fixed shell binary with a command string:

```cpp
char const* const argv[] = {"/bin/sh", "-c", cmd, nullptr};
execv("/bin/sh", cc<char* const*>(argv));
```

`src/debug/crash/CrashReporter.cpp:167,171` supplies the GPU and OS pipelines, containing bare helper names. On Linux `CrashReporter.cpp:209` canonicalizes `/proc/self/exe`. Lines 226–232 interpolate its result and run it through `execAndGet`:

```cpp
const auto CMD = std::format("addr2line -e {} -Cf {}", FPATH.c_str(), addrs);
const auto ADDR2LINE = execAndGet(CMD.c_str());
```

The Clang branch uses `llvm-addr2line` with the same pathname handling. `src/helpers/MiscFunctions.cpp:158–164` constructs `CProcess("/bin/sh", {"-c", cmd})`. Canonicalization resolves filesystem names; it does not make their characters safe shell syntax. Ordinary spaces can already break symbolization, independently of malicious input.

**Trigger and preconditions — source-confirmed.** Inherited PATH or the installation/launch pathname → default signal registration (`src/Compositor.cpp:295–298`) → a subsequent fatal signal → `handleUnrecoverableSignal` (`105–121`) → report creation/open (`CrashReporter.cpp:62–98`) → shell pipelines → backtrace/path formatting → shell symbolizer invocation. The latter also requires the reporter to survive earlier crash-context operations. PATH helper replacement requires an executable in a searched location; pathname interpretation requires a suitably named executable installation. These are launch/session trust prerequisites, not data delivered in an ordinary Wayland request. The session is normally unprivileged (`src/main.cpp:207–214` rejects superuser launch without an explicit override).

**Robustness — source-confirmed, failure not executed.** The reporter explicitly acknowledges allocations and possible deadlock at `CrashReporter.cpp:177–181`, and already allocates a plugin vector at lines 125–130. `Compositor.cpp:108–117` resets the fatal handlers and arms a 15-second alarm that aborts. This limits many hangs but cannot guarantee a complete report or safe recovery. Signal registration uses `signal`, with no alternate-stack setup found in `src/` or `start/`; stack exhaustion remains a report-reliability concern. Merely saying that every `fork` or `execv` call is itself inherently unsafe would be inaccurate; allocation, library state, and the post-fork operations require separate analysis.

**Severity.** Medium: real shell-interpretation and diagnostic-reliability defects with strong setup prerequisites. A controlled PATH is already widely honored during session startup (F-BOOT-03), so the dossier does not establish a new cross-user execution boundary merely by delaying helper execution until a crash. Arbitrary executable-path characters are mishandled, but no attack payload, forced compositor crash, or session compromise was executed.

**Corrections.** `/bin/sh` itself is absolute; its helper commands are searched. The initial fatal-signal trigger is assumed, not demonstrated by this dossier. Another user's ability to write a session unit or profile is not an ambient permission. Plugin loading already grants native execution and cannot be counted as an independent exploit prerequisite with lesser privilege.

**Minimal fix direction.** Collect system metadata during normal operation. Invoke any remaining symbolizer with a fixed executable and an argument vector, passing the pathname as one argument. Keep the fatal handler minimal and move complex report work to a separate process or normal-context mechanism; consider an alternate signal stack. No executable payload or desktop interaction was used.

## F-BOOT-03

**Claim.** PATH helper lookup trusts arbitrary directories and empty entries, and executable checking examines only the other-execute mode bit. The dossier also alleges automatic execution of every listed helper.

**Evidence — source-confirmed.** The compositor implementation uses the older splitter, not `CVarList2`:

```cpp
static CVarList paths(getenv("PATH"), 0, ':', true); // FsUtils.cpp:87
// after existence/regular-file/status checks:
return std::filesystem::perms::none != (perms & std::filesystem::perms::others_exec); // :104
```

That immediate return rejects an otherwise executable owner/group-only candidate, and stops before later candidates. The watchdog uses `CVarList2(PATHENV, 0, ':', true)` (`start/src/helpers/Nix.cpp:53`), checks `others_exec`, but **continues** if that bit is absent (`65–67`). Neither helper determines effective executability, including ACLs, mount policy, or the caller's owner/group bits.

**Dependency check — source-confirmed.** `/usr/include/hyprutils/string/VarList.hpp:9–14` and `VarList2.hpp:13–18,25–35` document the fourth parameter as `removeEmpty`. Both call sites pass `true`. Moreover, the compositor constructs `p + "/" + exe` (`FsUtils.cpp:90`); even a retained empty string there would produce a root-relative pathname, not the claimed cwd lookup. Relative nonempty entries remain relative. Direct `execvp` calls at `start/src/core/Instance.cpp:66–68` use normal PATH semantics, including empty entries, independently of these availability checks.

**Checks — executed, isolated.** `checks.cpp` in the designated scratch directory links the installed hyprutils 0.14.2 and contains the exact extracted baseline `NFsUtils::executableExistsInPath` body. Results in `checks-results.txt`: both splitters return two entries for `:a::b:` and zero for `::`; an owner-executable first fixture plus an other-executable second fixture returns false; removing the first fixture makes the same cached PATH return true. Fixtures contain inert text; no fixture was executed. This confirms parsing and detection behavior, not a live launch attack. Inspection of the installed `/usr/lib/libhyprutils.so` shows calls to `execvp` in `CProcess::runSync` (address `0x5097b`) and `runAsync` (`0x50ed6`); this is dependency-version evidence, not a claim that Hyprland pins that library binary.

**Actual call chains and gates — source-confirmed.**

- Startup constructs the three managers at `src/Compositor.cpp:756–763`. `src/managers/WelcomeManager.cpp:12–32` requires the top-level `autogenerated` option, excludes safe mode, checks availability, and launches the welcome helper. The option defaults to zero at `src/config/lua/ConfigManager.cpp:416`; generated config explicitly enables it at `DefaultConfig.hpp:11`.
- `VersionKeeperManager.cpp:25–59` requires a usable data home, readable prior version that compares older, notifications enabled, and helper availability. Missing version state is initialized and returns without launching. `ecosystem:no_update_news` defaults false (`src/config/values/ConfigValues.cpp:715`).
- `DonationNagManager.cpp:41–79,87–115,126–133` checks update suppression, data availability, prior nag state, a month interval, availability, and a date/version condition. This is not an unconditional boot launch; its disable option defaults false (`ConfigValues.cpp:716`).
- Safe mode can call `CAsyncDialogBox::open` → `CProcess("hyprland-dialog", ...)` at `src/helpers/AsyncDialogBox.cpp:95–127`. `xdg-open` at `src/Compositor.cpp:948–971` requires the dialog result selecting the open-report option; it is not automatic. The report path is passed as an argument vector, not interpolated into shell syntax there.
- Watchdog startup calls `Nix::nixEnvironmentOk`, then `shouldUseNixGL` (`start/src/main.cpp:95–102`), which queries bare `Hyprland --version-json` (`Nix.cpp:84–105`) unless forced/disabled. Selecting nixGL further depends on OS and `/run/opengl-driver` checks (`107–121`); returning a Nix flag alone does not unconditionally force it. `--path` does not change this probe's binary name.

An actor must already influence the launcher's PATH or write a searched directory to substitute code. Standard private user directories are not writable by arbitrary other UIDs, and session environment inheritance is not a separate authentication mechanism. F-BOOT-02 covers crash-specific shell use; do not count these as independent privilege escalations.

**Severity and corrections.** Low for the confirmed helper-availability bug. Conditional execution from an unsafe PATH is ordinary launch trust, not a new default session takeover. The dossier's empty-token explanation, missing caller gates, `misc:autogenerated` spelling, and unconditional nixGL claim are incorrect.

**Minimal fix direction.** Check effective executable access and continue searching after an unsuitable candidate; use the resolved candidate consistently when launching. Probe the requested compositor when `--path` is supplied. Stronger PATH policy, if desired, should be explicit and preserve legitimate user executables rather than treating all non-root paths as inherently hostile.

## F-BOOT-04

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

## F-BOOT-05

**Claim.** Accepting any nonempty value except exactly `"0"` lets an injected environment variable disable crash reporting or systemd integration and therefore constitutes a security defect.

**Evidence — source-confirmed.** `src/helpers/env/Env.cpp:5–7` explicitly implements the rule:

```cpp
const auto ret = getenv(env);
return ret && ret[0] != '\0' && !(ret[0] == '0' && ret[1] == '\0');
```

`src/Compositor.cpp:295–298` conditionally installs the reporter; lines 582–583 and 824–825 gate notifications. Lines 795–815 gate session-target/environment management on a backend session, `HYPRLAND_NO_SD_VARS`, `MANAGERPID`, build-time systemd support, and `HYPRLAND_NO_SD_TARGET`.

**Parser check — executed, isolated.** The baseline `Env.cpp` was included directly in `checks.cpp`. Empty and `"0"` returned false; `"1"`, `"false"`, and `"00"` returned true (`checks-results.txt`). This confirms the implementation without establishing an injection route.

**Input chain and guard — source-confirmed.** A session launcher supplies the environment → `getenv` returns that process's own value → the helper deliberately evaluates it → the caller skips an optional integration. Empty/unset and exactly `"0"` leave the switches disabled. There is no client-controlled argument that changes the compositor's environment on this path and no authorization check being bypassed. Even strict boolean parsing would still permit an environment controller to use `"1"`; it would not close the dossier's proposed trust boundary.

**Severity.** None as a security finding. Unexpected values such as `"false"` can surprise an operator, but the intended feature permits disabling these facilities. Notification failure is relevant only for a supervisor/unit that actually depends on a readiness notification, not every systemd session. Disabling the handler means normal fatal-signal behavior; it does not by itself prove a particular system core was generated or that all external evidence disappeared.

**Corrections.** The proposed replacement `truthy()` is not a strict parser either: `src/helpers/MiscFunctions.cpp:465–478` accepts case-insensitive prefixes of `true`, `yes`, and `on`. Changing this established toggle convention would be a compatibility decision rather than a fix for environment injection.

**Minimal fix direction.** No security fix required for this claim. Document the values and, if desired, log explicitly disabled diagnostic/integration facilities. A change in accepted values needs an intentional compatibility policy.

## F-BOOT-06

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

## F-BOOT-07

**Claim.** Data-home creation and file writes permit replacement races/symlink redirection; donation state handling allegedly dereferences an unconditionally missing data-home result.

**Evidence — source-confirmed.** `src/helpers/fs/FsUtils.cpp:12–35` uses `XDG_DATA_HOME` or `HOME/.local/share/` and requires the base to exist. It checks, creates, and only then changes permissions on the `hyprland/` child (`37–46`), warning and proceeding if chmod fails. Existing children receive no ownership, directory-type, or mode validation (`37–54`). The write helper is:

```cpp
std::ofstream of(path, std::ios::trunc); // FsUtils.cpp:71
// ...
of << content;                       // :77
```

This opens the named destination with truncation and follows ordinary filesystem links. It supplies no explicit private mode for a newly created file; parent privacy and the process umask matter.

**Trigger chains and caller checks — source-confirmed.** Environment/data-directory state → manager creation (`src/Compositor.cpp:756–760`) → `getDataHome` → fixed data filename → `writeToFile`. `src/managers/VersionKeeperManager.cpp:25–35` returns if the data home is missing, and writes `"0.0.0"` only if reading `lastVersion` fails. Lines 38–43 write the build version only when the existing contents compare older. Thus not every pre-existing readable target is overwritten by this manager. `DonationNagManager.cpp:44–47` also returns on missing data home. Lines 60–66 initialize state when needed; later date/version gates control further updates (`69–115`). `getState` and `writeState` independently cache fresh data-home results and dereference them without a local check (`137–139,155–157`). There is no unchecked dereference in `fire()` at the dossier's cited line 135; its unused cached result (`126–134`) is not dereferenced.

**Reachability and preconditions.** Under a normal private data hierarchy another UID cannot plant the final filename. A shared/wrongly owned data root, replaceable child, or existing permissive hierarchy is required for cross-user redirection; the resulting target must also be writable by the compositor UID. Payloads are fixed version strings or numeric time/version state, not arbitrary attacker bytes. `create_directory` uses default permissions filtered by umask; a create-to-chmod interval is not automatically writable by another UID under a typical restrictive umask. Even then a writable ancestor can permit path replacement, which is why path identity matters in addition to chmod.

The initial donation guard breaks the simple stable-nullopt crash claim. A later independent `getDataHome()` can still fail if access or filesystem state changes after the first successful call, making the unchecked optional a real conditional error-handling concern. No such filesystem transition was executed, and no deterministic startup crash is asserted for a consistently unavailable data root.

**Severity.** Medium for conditional victim-writable file clobber through an insecure data namespace, revised upward from the dossier's Low label for that limited effect; Low for exceptional-state robustness. No default cross-user path or execution is established. The directory prerequisite overlaps F-BOOT-01/04 and must not be counted as independent access already achieved by an attacker.

**Corrections.** `getDataHome` does not blindly create the base directory; it returns nullopt when that base is absent (`30–33`). A base merely owned by another user does not suffice if traversal/write permissions exclude the victim. Short writes are not equivalent to arbitrary content injection. The donation optional claim needs the caller guard and the second-call failure condition above.

**Minimal fix direction.** Validate the data directory and preserve its identity through descriptor-relative operations. Reject symlink targets for state writes and use private, safely replaced files within that directory. Resolve/check the data home once and pass it to donation helpers, or check every optional locally. No symlink overwrite or race harness was run.

## F-BOOT-08

**Claim.** The watchdog performs unsafe nested process creation, leaks argument strings, restarts indefinitely without backoff, and trusts child lock/unlock messages.

**Evidence — source-confirmed.** `start/src/core/Instance.cpp:47–53` allocates every argument using `strdup` **before** forking. `std::vector<char*>` does not free its pointed-to allocations, and no frees appear after the fork or before return. Thus the long-lived watchdog parent leaks a small argument-sized amount per launch; this is not confined to a disposable child, as the dossier suggests. The child calls `Nix::shouldUseNixGL()` at line 63 and then `execvp` at 66–68; the Nix probe invokes `CProcess::runSync` (`start/src/helpers/Nix.cpp:84–105`) unless flags short-circuit it. PATH/`--path` behavior is covered in F-BOOT-03.

**Restart chain and stopping guards — source-confirmed.** Launcher invocation → `start/src/main.cpp:104–109` creates an instance → pipes and launch (`Instance.cpp:157–181`) → poll and child-event processing (`196–217`) → result at `Instance.cpp:219` → restart decision (`main.cpp:113–120`):

```cpp
return !m_hyprlandInitialized || m_hyprlandExiting; // Instance.cpp:219
// ... main.cpp:113–117 ...
if (!RET) {
    safeMode = true;
    continue;
}
```

`m_hyprlandInitialized` defaults false (`Instance.hpp:32`) and is set by `vax` (`Instance.cpp:129–134`). The compositor sends this marker after its ready path (`src/Compositor.cpp:830–836`). `end` sets the exiting flag (`Instance.cpp:137–140`), and cleanup sends it (`Compositor.cpp:568–572`). Therefore a pre-initialization failure or clean exit stops the launcher. Only a child that has initialized and exits without `end` selects a restart. The restarted child receives `--safe-mode` (`Instance.cpp:34–35`). The same condition must recur in safe mode for an unbounded loop. There is no restart count or delay in that loop.

**Threading and protocol corrections — source-confirmed.** The watchdog's visible worker thread is created **after** fork (`Instance.cpp:76`) and joined before `run` returns (`217`); the prior instance is destroyed before the next launch (`main.cpp:110–111`). The dossier does not establish a concurrently running watchdog thread at fork. Additional dependency-created threads were not proved. Complex child-side library work remains avoidable, but a concrete inherited-lock deadlock does not follow merely from the second process launch. No deadlock was executed.

The event pipe is created by the parent (`Instance.cpp:157–177`) and its write fd is deliberately passed to the compositor (`30–33`). `lock`/`unlock` modify state (`143–152`) and `lockedCrash` becomes `--locked` (`37–38`; `main.cpp:110`). A malicious `--path` executable is the selected trusted child, not an arbitrary external client; it already executes as the same user. No distinct unauthenticated client-to-watchdog path is established by these text messages.

**Severity.** Low for a real parent leak and lack of backoff after repeatedly initialized crashes. Triggering rapid restarts requires an already recurring post-initialization crash that survives safe mode; the dossier supplies no client reproducer for it. CPU saturation, measured log growth, and multithreaded deadlock were not demonstrated. These facts do not justify a new cross-user execution finding.

**Minimal fix direction.** Own argument storage with strings and point argv into stable buffers, precompute launch decisions before fork, and add bounded/backoff restart policy without weakening locked-session recovery. Treat child protocol framing as reliability work rather than assuming a separate authentication boundary. No watchdog or compositor binary was run.

## F-BOOT-09

**Claim.** Because Hyprland does not suppress core dumps, aborting can disclose session memory to unrelated local users under permissive system coredump policy.

**Evidence — source-confirmed.** `src/Compositor.cpp:105–121` resets the SIGABRT/SIGSEGV handlers, runs reporting, and calls `abort`; the alarm fallback also aborts (`112–117`). `src/debug/crash/CrashReporter.cpp:45–50` aborts on a reporter error. Searches of `src/`, `start/`, and `systemd/` found no `RLIMIT_CORE`, `PR_SET_DUMPABLE`, `LimitCORE`, or explicit coredump-policy implementation. The watchdog's `prctl` at `start/src/core/Instance.cpp:57` is `PR_SET_PDEATHSIG`, not dumpability control. Existing resource-limit code manages `RLIMIT_NOFILE` (`src/Compositor.cpp:145–173`). The shipped `systemd/hyprland-session.target:1–6` is a target, not a service setting dump policy.

**Potential input/trigger chain — source-confirmed until the system boundary.** An actual fatal signal → reporter/default abort path → kernel and any configured external core collector → possibly stored process memory. Hyprland constructs/exports its instance identifier at `Compositor.cpp:211–213`; the executor handles workspace and exec-rule tokens at `src/config/supplementary/executor/Executor.cpp:131–132,165–168,194–196`. This supports the possibility of session state in memory. It does not prove any particular credential was present in a particular core. Exec-rule tokens are registered with a one-second lifetime at line 131, and neither an instance identifier nor a Wayland display name by itself substitutes for filesystem access authorization.

**Unclosed boundary.** Whether a dump is collected, what is included/retained, and who can read it depend on the launcher, kernel, collector, file mode/ACL, and service policy. Merely placing a dump in a publicly readable directory does not make a private file readable. An unrelated UID's access to the dump is not established by any upstream source path. Same-UID or administrative debugging access must not be presented as a cross-user confidentiality break. No production limits, kernel core settings, storage permissions, or process memory were inspected in this review.

**Verdict and severity.** Inconclusive for the alleged disclosure; informational as an upstream record. The absence of a program-level restriction and the abort path are verified. Treating normal crash diagnostics as a confirmed Low vulnerability would require additional policy/access evidence. No current system default is assumed.

**What would settle it.** In an authorized disposable environment, document effective launcher/kernel/collector policy, resulting file/ACL access for a different unprivileged UID, and the actual retained sensitive data. This can be assessed separately from the running desktop. A private collector or absent dump closes the proposed disclosure path for that deployment.

**Minimal fix direction and corrections.** Document dump privacy expectations and offer an explicit privacy/debugging policy if needed. A hard zero `RLIMIT_CORE` is not interchangeable with clearing dumpability, especially with an external pipe collector; lowering the core-size limit does not itself disable ptrace, whereas clearing dumpability has broader debugging effects. Blanket suppression is a product/diagnostics tradeoff, not a demonstrated mandatory security fix from this dossier alone.

## F-BOOT-10

**Claim.** This mixed record alleges undefined behavior for empty paths and missing HOME, notes lock-file writing and environment propagation, and records a clean overlay/systemd review with possible monitor-state retention.

**Path expansion — source-confirmed.** `src/helpers/MiscFunctions.cpp:54–75` copies a `std::string`, reads `value[0]`, and expands an initial tilde using a cached environment pointer:

```cpp
if (value[0] == '~') {
    static const char* const ENVHOME = getenv("HOME");
    value.replace(0, 1, std::string(ENVHOME));
}
```

For a modern `std::string`, reading index `size()` is permitted and yields the null character; an empty string's index zero is therefore not the alleged UB. The installed `/usr/include/c++/16/bits/basic_string.h:1387–1396` explicitly permits `pos <= size()`. `CMAKE_CXX_STANDARD` is 26 (`CMakeLists.txt:101`). Constructing a string from a null C-string pointer remains invalid; this libstdc++ throws at `basic_string.h:733–736` rather than necessarily segfaulting.

**Path checks — executed, isolated.** `checks.cpp` contains the exact baseline function body. With assertions enabled, `absolutePath("", "/config/main.lua")` returned `/config/`, with exit status zero. A separate invocation unsets HOME only in that test process and calls the tilde branch; it catches `basic_string: construction from null is not valid` (`checks-home.txt`). This validates helper behavior, not a full compositor crash.

**Reachable configuration chain — source-confirmed.** HOME need not be set for the program to reach a supplied config: `src/main.cpp:118–142` accepts a canonical explicit config, and `src/config/supplementary/jeremy/Jeremy.cpp:26–35` selects a safe/explicit/environment-selected path before default home-directory discovery. The only unconditional runtime-directory environment check is at `main.cpp:202–205`. A configured nonempty `input:kb_file` value (default empty, `src/config/values/ConfigValues.cpp:358`) is read at `src/managers/input/InputManager.cpp:1208`, assigned and used at `1267–1268`, and expanded by `src/devices/IKeyboard.cpp:88–89`. First-frame startup calls `setKeyboardLayout` through `src/render/Renderer.cpp:2248–2252` → `src/config/supplementary/executor/Executor.cpp:23–49` → `InputManager.cpp:1188–1190`. A tilde path with HOME absent at the first expansion reaches the invalid construction; the nearby comparison-only catch at `InputManager.cpp:1252–1262` does not wrap `setKeymap` at 1268. This is a trusted configuration/launch robustness issue, not an ordinary client-input exploit. A keyboard, initialized config/backend, and a first tilde expansion without HOME are prerequisites.

`absolutePath` is also reached from explicit Lua require resolution (`src/config/lua/ConfigManager.cpp:79–97,588–594`); the renderer rejects empty shader paths before calling it (`src/render/OpenGL.cpp:1017–1020`), and keyboard and bell paths reject empty input (`IKeyboard.cpp:88`; `src/bell/impl/Canberra.cpp:32–38`). These gates further undermine an empty-input compositor-crash claim. No device or config was changed for this review.

**Lock-file control — source-confirmed.** `src/Compositor.cpp:775–782` uses truncating `ofstream` for the lock, but the checked random private instance creation (`211,224–232`) prevents another UID from planting a child file under a normal runtime hierarchy. F-BOOT-04 covers the insecure-ancestor condition. This record does not establish a separate default lock-file vulnerability.

**Overlay control and retention — source-confirmed, limited negative evidence.** `src/debug/Overlay.hpp:59,79` uses `PHLMONITORREF` both for the stored monitor and map keys, **not** the dossier's strong `PHLMONITOR` keys. `src/desktop/DesktopTypes.hpp:43–45` defines these as shared and weak pointers respectively. The dependency weak-pointer `lock()` returns an empty shared pointer for a dead/destroying referent (`/usr/include/hyprutils/memory/WeakPtr.hpp:175–185`), and map ordering uses control-block addresses (`217–218`). `src/debug/Overlay.cpp:216–221` locks/checks before cache rebuilding; drawing also handles an absent monitor (`278–289`). No monitor-use-after-free is inferred from a stale weak key.

The map is populated at `Overlay.cpp:329–353,400–401,433–438` and independently by renderer timing at `src/render/Renderer.cpp:3205–3216`; no erase/clear of `m_monitorOverlays` was found anywhere in `src/`. Entries, cached textures, samples, and weak control blocks can therefore outlive a removed monitor, without keeping its monitor object alive. Sample histories are bounded per entry (`Overlay.cpp:120–125,174–177`), but total retained entries across distinct monitors are not pruned. Debug overlay defaults false (`ConfigValues.cpp:723`), so a texture-retention claim needs prior overlay use; renderer timing can still create empty entries. This supports Low-priority lifetime cleanup, not a measured memory leak magnitude or proof that all overlay code is secure. No hotplug/render test was performed.

**Systemd and environment controls — source-confirmed.** `systemd/hyprland-session.target:1–6` has static target dependencies; `systemd/hyprland-uwsm.desktop:1–7` has static `Exec=uwsm start -e -D Hyprland hyprland.desktop` and `TryExec=uwsm`. No dynamic shell interpolation exists in these file contents. A maliciously writable installed desktop file would be a packaging/permissions issue not demonstrated here.

The executor's first-start listener deliberately invokes environment import for session backends (`src/config/supplementary/executor/Executor.cpp:23–37`) and ultimately uses `/bin/sh -c` (`173–205`). It exports selected **variable names**, including PATH/XDG_DATA_DIRS; their values are inherited, not inserted as command text at this call site. This can propagate an already untrusted environment into future activations, but does not prove every already-running service changes environment. The compositor's separate import path is guarded by `HYPRLAND_NO_SD_VARS` and `MANAGERPID` (`src/Compositor.cpp:795–808`); the executor listener shown here lacks those guards. Thus the dossier's environment-management narrative must distinguish the two paths. No systemd or D-Bus environment was changed.

**Severity and minimal fix direction.** Low for missing-HOME handling and stale overlay storage; informational for the static file/propagation controls. Reject the empty-string UB claim. Handle a missing home explicitly or use an error-returning path resolver; prune expired overlay entries, while preserving weak lifetime semantics. Document the intended activation environment and align its opt-out behavior if appropriate. Generic filtering of all user-owned PATH directories is not justified by these controls.

