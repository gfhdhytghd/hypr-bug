# hyprpm dossier verification

- Baseline: upstream Hyprland `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb` (`e2ad066c9`).
- Dossier: [03-hyprpm](../../pending-evaluation/2026-10-full-audit/03-hyprpm.md).
- Date: 2026-10-15.
- Source references below are relative to `/home/wilf/data/.claude-scratch/agent-reports/upstream/`; shortened basenames refer to the same previously named files. Dependency-manual references use decompressed line numbers and describe the installed dependency, not code pinned by the Hyprland commit.
- Method: read-only source tracing of every dossier ID, including caller gates, privilege boundaries, and dependency behavior. Any isolated checks are identified explicitly. No running compositor, IPC, Hyprland executable, privileged operation, network, or full build is used. This is defensive verification; exploit payloads and operational reproduction instructions are omitted.
- Completion: all 10 dossier IDs reviewed. Verdicts: 3 confirmed, 5 partially, 2 rejected, 0 inconclusive. No end-to-end exploit or live compositor result is claimed.
- The two example report paths named in BRIEF.md are absent in this checkout; the explicit brief structure is used.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-PM-01 | confirmed | Medium | `hyprpm/src/main.cpp:107–125`; `hyprpm/src/core/PluginManager.cpp:139–148,217–230` | Positional revision reaches an unquoted shell command after confirmation; no --rev option exists. User-context execution, not unconditional privileged execution. |
| F-PM-02 | confirmed | High | `hyprpm/src/core/Manifest.cpp:64,91`; `hyprpm/src/core/DataState.cpp:85–122`; `hyprpm/src/helpers/Sys.cpp:124–163` | Repository name escapes the cache during privileged directory creation and installation; filenames and serialized state constrain writes. Root authorization remains required. |
| F-PM-03 | partially | High | `hyprpm/src/core/PluginManager.cpp:87,495,528–639`; `Makefile:37–51`; `hyprpm/src/helpers/Sys.cpp:166–172` | Privileged make consumes user-writable build inputs and version fields reach shell commands; fake running-socket metadata does not drive header checkout. |
| F-PM-04 | partially | Medium (conditional transport risk) | `hyprpm/src/core/PluginManager.cpp:217,724–750,790–842`; `hyprpm/src/main.cpp:136–158` | Unauthenticated transports and unsigned updates are accepted; fixed revisions receive resets, Git checks objects, and ext execution is disabled by default. |
| F-PM-05 | rejected | None under default patched Git policy | `hyprpm/src/core/PluginManager.cpp:178–189,217,230,293,724,866`; installed Git protocol policy | Recursive fetch exists, but Git blocks ext by default and confirmation precedes clone; historical Git vulnerabilities require separate version-specific evidence. |
| F-PM-06 | confirmed | High | `hyprpm/src/core/Manifest.cpp:40,102`; `hyprpm/src/core/PluginManager.cpp:344,376,848,877`; `hyprpm/src/core/DataState.cpp:106–107` | Unchecked output paths reach privileged copying; existence checks do not require read permission. Accessible metadata and root authorization are necessary. |
| F-PM-07 | partially | Informational (dependent on state compromise) | `hyprpm/src/core/DataState.cpp:217–245`; `hyprpm/src/core/PluginManager.cpp:997–1065`; `src/plugins/PluginSystem.cpp:20–77` | Stored load paths lack confinement; normal state is root-protected, permission and ABI gates apply, and compositor-supplied names reach unload only. |
| F-PM-08 | rejected | Informational hardening only | `hyprpm/src/core/PluginManager.cpp:419–489,632–639,1156–1158`; `hyprpm/src/core/DataState.cpp:65–78` | Compatibility checks are not authenticity checks, but headers remain root-owned and appended package paths do not normally override the managed package. |
| F-PM-09 | partially | Low | `hyprpm/src/core/PluginManager.cpp:123–134,208,521,697`; `hyprpm/src/core/DataState.cpp:12–37`; `hyprpm/src/core/HyprlandSocket.cpp:38–69` | Fixed work/state paths permit same-user concurrency races; normal private runtime directories block cross-user placement. Socket environment trust is not a race. |
| F-PM-10 | partially | Low (undefined call); otherwise Informational | `hyprpm/src/main.cpp:61–64,94`; `hyprpm/src/core/PluginManager.cpp:59–61,1089–1091`; `hyprpm/src/helpers/Sys.cpp:41–84` | Build trust and permissive validators exist; deprecated notification calls a null manager, but stateless notify does not establish the claimed deterministic crash. |

## F-PM-01

**Claim.** The revision supplied when adding a repository is interpolated into a shell command without quoting or a revision-specific validator.

**Evidence — source-confirmed.** `hyprpm/src/main.cpp:107–109,125` reads `command[2]` and passes it to `addNewPluginRepo(command[1], rev)`. `hyprpm/src/core/PluginManager.cpp:139–148` checks only:

```cpp
return !s.contains("'") && !s.ends_with("\\") && !s.starts_with("\\");
```

The add path at `PluginManager.cpp:224–230` formats a Git reset command with a bare final `{}` revision. `PluginManager.cpp:36–44` executes formatted commands using `CProcess("/bin/sh", {"-c", cmd})`. Shell syntax is therefore not excluded by the validator. This is a source-confirmed data-to-shell boundary defect; no injected command was executed.

**Call chain and gates — source-confirmed.** Local CLI positional revision → main's ABI checks (`main.cpp:111–118`) and privilege-cache request (`120–123`) → `addNewPluginRepo` argument/dependency/duplicate checks (`PluginManager.cpp:146–161`) → nonempty recorded header ABI (`173–176`) → explicit repository-trust confirmation (`178–189`) → successful clone with a `.git` entry (`217–222`) → revision reset (`224–230`) → shell. No manifest validation precedes that reset. An attacker must influence the actual revision argument used by a user or script; a remote manifest alone cannot set it.

**Severity and corrections.** Medium: user-context command execution requires a user-initiated install of a deliberately supplied argument and the preceding gates. The dossier's Critical rating and `--rev` example are incorrect: unknown flags are rejected at `main.cpp:81–83`, while revision is positional (`main.cpp:16–17`). Privileged helper calls occur elsewhere; this reset itself is not run as root. The update path single-quotes the stored revision (`PluginManager.cpp:732–741`); manifest commit pins are separately hex-checked and quoted (`275–293`).

**Minimal fix.** Use an argument-vector Git invocation; validate the supported revision syntax and distinguish revisions from options. If only full object IDs are intended, require an exact nonempty supported hash length. Do not treat the existing permissive `validArg` as shell escaping.

## F-PM-02

**Claim.** An unchecked manifest repository name can direct privileged cache installation outside the user's cache tree.

**Evidence — source-confirmed.** `hyprpm/src/core/Manifest.cpp:64` assigns `m_repository.name` directly, whereas plugin keys pass `validManifestName` at `91–94`. `hyprpm/src/core/PluginManager.cpp:370,378` retains the supplied repository name and sends the repository to DataState. `hyprpm/src/core/DataState.cpp:85–90,103–107,122` uses:

```cpp
const auto PATH = getDataStatePath() / repo.name;
// ...
const auto filename = std::format("{}.so", p.name);
// privileged install to PATH / filename, then writeState to PATH / "state.toml"
```

There is no component/containment check. Absolute right-hand paths replace the base under `std::filesystem::path` composition; relative parent components are also accepted. `hyprpm/src/helpers/Sys.cpp:87–89,124–133,154–163` rejects certain punctuation but not path separators or parent components. Directory creation and copies use argument vectors, including `install -o 0 -g 0`; they do not neutralize path traversal.

**Call chain and preconditions — source-confirmed.** User authorizes add and privileged operations (`main.cpp:111–125`; `PluginManager.cpp:178–189`) → repository clone and good manifest (`217–258`) → manifest repository name (`Manifest.cpp:64`) → header validation (`PluginManager.cpp:304–310`) → plugin build/output bookkeeping (`317–376`) → `DataState::addNewPluginRepo` (`378`) → privileged directory creation, plugin copies, and state-file copy (`DataState.cpp:85–122`). A same-machine user without the necessary privilege authorization cannot use these helpers to bypass authentication. The vulnerability is the privileged destination being delegated to repository data after authorization for plugin management.

**Impact limits and corrections.** High: an authorized install can create directories and place/replace root-owned files outside its intended cache, exposing a real privilege boundary. The dossier's claim of fully arbitrary file writing is too broad: ordinary destinations are `<validated-plugin-name>.so` and `state.toml`; state content is TOML serialization (`DataState.cpp:93–122`), not arbitrary bytes. Manifest author text is not the persisted repository author: `PluginManager.cpp:371` derives that field from the input URL. TOML serialization also does not imply textual configuration injection. A cross-user state-corruption scenario additionally requires a victim-discovered state location and a later update, its header checks, dependency availability, and privileged authorization (`DataState.cpp:48–61,217–245`; `main.cpp:136–158`). Unconditional root execution or a concrete system-configuration overwrite is not established here.

**Minimal fix.** Require a nonempty repository name containing only approved single-component characters, validate URL-derived fallback names too, and enforce destination containment in the privileged implementation using trusted directory descriptors and symlink-safe operations. Lexical string-prefix checks alone are insufficient.

## F-PM-03

**Claim.** Header updates consume unchecked version metadata and execute installation rules from a clone as root. The dossier also claims a forged running-compositor version reply drives this operation.

**Evidence — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:492–495` specifically calls `getHyprlandVersion(false)`. At `87`, false selects `execAndGet("Hyprland --version-json")`, not `j/version`; parsed commit/date strings are copied without hash/date validation (`96–113`). Date formatting (`536–537`) and checkout/reset (`563,575`) interpolate these fields into shell commands. `isValidHash` (`59–61`) is not called on them. The source URL defaults to official HTTPS, with an explicit user override (`528`); the override is assigned at `hyprpm/src/main.cpp:71–77,98`.

The privileged boundary is `PluginManager.cpp:632–639`:

```cpp
cmd = std::format("make -C '{}' installheaders && chmod -R 644 '{}' ...", ...);
ret = NSys::root::runAsSuperuserUnsafe(cmd);
```

`hyprpm/src/helpers/Sys.cpp:166–172` runs the selected privilege tool with `/bin/sh -c`. Even the baseline `Makefile:37–51` installation target runs `cmake --build ./build --config Release --target generate-protocol-headers` at `46`, consuming generated build rules; this is not just a fixed privileged file copy. The checkout is created as the invoking user (`PluginManager.cpp:521–525,542`); the preceding CMake configuration is also unprivileged (`590–600`). No ownership handoff freezes those inputs before the root make invocation.

**Call chain and gates — source-confirmed.** Local `update` → privilege-cache request (`main.cpp:136–144`) → installed-binary version data (`PluginManager.cpp:87,495`) → dependency check and missing/stale headers or explicit force (`497–511`) → clone/configure (`528–619`) → root make over the user's checkout/build files (`632–639`). The selected helper must grant elevation. A custom source must survive the `.git` existence check (`550–553`), checkout/reset attempts, and configuration error check (`604–619`). These checks are not authenticity checks or a separation of unprivileged build rules from privileged installation. A process already able to change the invoking user's checkout can influence those root-consumed inputs; an unprivileged different UID normally cannot traverse the 0700 working directory.

**What holds and what does not.** High for the privileged execution of mutable build inputs. Unchecked installed-version fields reaching the user's shell also hold, but require control of the installed binary selected through PATH or equivalent local environment control; this is not a demonstrated remote metadata attack. The fake running-socket trigger is rejected for header checkout: true/false version caches are separate (`70–85,115–118`), and the relevant call uses false. A forged running reply may affect other operations, but cannot substitute for the installed-version source in this chain. Custom URL support and lack of signature enforcement are policy facts, not independent proof that official HTTPS can be silently replaced. The dossier's root-escalation conclusion cannot be attributed to a socket reply alone.

**Minimal fix.** Complete configuration/header generation unprivileged, then install a validated, bounded file set through a narrow privileged operation that cannot execute repository build rules. Validate installed-version fields and use argument-vector process invocation. Authenticate or clearly constrain custom source selection without assuming it fixes mutable local build inputs.

## F-PM-04

**Claim.** Fetches do not authenticate publisher signatures or enforce the previously stored hash as a lock; the dossier adds automatic external-helper execution and unconditional malicious plugin loading.

**Evidence — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:217,724` passes user/stored URLs to `git clone --recursive` without a transport allowlist or signature-verification operation. `PluginManager.cpp:744–750` compares fetched HEAD with `repo.hash` to decide whether to rebuild:

```cpp
update = update || hash != repo.hash;
```

It is an update detector, not an authentication check. Build steps from the fetched manifest are executed as the invoking user (`Manifest.cpp:114–118`; `PluginManager.cpp:834–842`). Privileged copying of an output is separate (`DataState.cpp:106–107`); root ownership of the installed plugin is not evidence that the build ran as root.

**Call chain and preconditions — source-confirmed.** Initially confirmed URL → state URL (`PluginManager.cpp:163–189,372`; `DataState.cpp:98`) → local update invocation and authorized privilege operations (`main.cpp:136–147`) → header check (`PluginManager.cpp:672–676`) → URL clone (`724–730`) → stored-revision handling and update comparison (`732–759`) → manifest validation and matching compatibility pin (`768–815`) → build steps (`834–842`). A network attacker must control the repository delivery channel actually used: plain HTTP/Git transport or compromised trusted source. Merely being on the same network does not establish control of official HTTPS or SSH. Unpinned repositories using an unauthenticated transport remain exposed to substituted HEAD/build content.

**Guards and corrections — source-confirmed.** A nonempty stored revision is reset before the hash comparison (`732–741`), and a matching manifest compatibility pin is hex-checked and reset (`790–811`). The reset error handling is narrow and output-text-based; it is not robust end-to-end revision verification, but the claim that revisions are never enforced is wrong. A hash changing during an update is intended behavior; comparing new HEAD for equality with old HEAD would disable updates, not authenticate them. Git's content-addressed object checks should not be conflated with publisher authentication. Trusting a newly fetched repository's own pin cannot independently authenticate that repository.

The installed Git 2.56.0 manual (`/usr/share/man/man1/git-config.1.gz`, decompressed lines `10039–10081`) specifies `ext` as disallowed by default, with dangerous-policy overrides requiring external configuration. No hyprpm command shown enables that transport. The claimed default external-helper RCE is rejected. Optional hardening is distinct from a missing default Git guard.

**Load endpoint — source-confirmed.** `main.cpp:158` reconciles load state after updates, but only enabled, nonfailed plugins pass `PluginManager.cpp:1025–1039`; ABI checks at `1052–1059` also apply. IPC dispatch (`src/ipc/s1/Commands.cpp:1778–1794`) enters `CPluginSystem::loadPlugin`, whose permission check can deny or await user approval (`src/plugins/PluginSystem.cpp:20–59`). Permission enforcement defaults off (`src/config/values/ConfigValues.cpp:717`); if enabled, a rule or user approval is additionally required. `dlopen` occurs at `PluginSystem.cpp:77` only after that gate. These later checks do not undo build-time user-code execution, but disprove unconditional compositor loading.

**Severity and fix.** Medium, conditional on the user choosing an unauthenticated source or a trusted source being compromised; user-code execution during an update is the exposure. Updating trusted native code is an intended operation, so lack of signatures alone is a supply-chain hardening observation. Prefer authenticated transports, explicit immutable revisions where desired, robust reset-status/identity checks, and an optional publisher-signature/update-review policy. Do not claim that fast-forward ancestry alone establishes publisher trust.

## F-PM-05

**Claim.** Merely putting an external-helper submodule URL or a historical Git exploit pattern in a repository yields code execution during recursive cloning, outside confirmation.

**Evidence and breaking guard — source-confirmed.** Recursive operations do exist at `hyprpm/src/core/PluginManager.cpp:217,230,293,724,866`, and there is no hyprpm-specific submodule-URL audit. However, no command sets a Git protocol policy that enables the dangerous external transport. The installed dependency is Git 2.56.0 (version query executed; no repository clone was executed). Its primary documentation, `/usr/share/man/man1/git-config.1.gz`, decompressed lines `10039–10081`, specifies safe transport defaults, `ext = never`, and a `user` policy preventing noninteractive recursive use of other transports such as `file`. `/usr/share/man/man1/git-submodule.1.gz:154–163` additionally states custom submodule update commands are not copied from `.gitmodules` for security.

At `PluginManager.cpp:186–189`, the confirmation guard is:

```cpp
if (input.size() > 0 && input[0] != 'Y' && input[0] != 'y') {
    std::println(stderr, "Aborting.");
    return false;
}
```

The subsequent Git invocation delegates transport policy to Git; lack of a redundant hyprpm denial does not remove Git's default denial.

**Call chain and reachability — source-confirmed.** Local add request → ABI/dependency/state gates described in F-PM-01 → repository-author trust prompt (`178–189`) → recursive clone (`217`) → Git's protocol decision → refusal of the dossier's external transport under default policy. Update lacks another repository confirmation but uses the same dependency policy (`724`). A repository's `.gitmodules` alone does not change the caller's global protocol allowance. A user or administrator who already opted into permissive Git policies changes this precondition; hyprpm is not shown to do so.

**Severity and corrections.** No independently confirmed hyprpm vulnerability for the asserted trigger. Clone precedes manifest review, but it follows explicit repository trust confirmation, contrary to the title's claim. Reliance on CVE-2024-32002 would require an affected Git/platform/configuration and an exact reproduction against that dependency, not merely a recursive-clone call. This report neither assumes every installed Git is patched nor attributes an untested dependency CVE to e2ad066c9. A custom unsafe policy would warrant a separate conditional assessment, not this unconditional High finding.

**Minimal fix direction.** Defense in depth may pin conservative protocol policies and require a supported patched Git. Such hardening should not be presented as proof that Git's existing defaults allow the proposed attack.

## F-PM-06

**Claim.** A manifest output may escape the checkout, after which a privileged copy publishes its contents with mode 0755.

**Evidence — source-confirmed.** Both manifest formats copy output strings without confinement (`hyprpm/src/core/Manifest.cpp:40,102`), requiring only nonempty output/build-step collections (`58–61,121–124`). Add/update check `std::filesystem::exists` on a string concatenation (`PluginManager.cpp:344,848`), then retain that same path (`376,877`). This is not `path / absolute-output`: a leading slash alone does not replace the checkout string prefix. Parent traversal and filesystem links remain unconstrained.

The final guard and privileged copy are `hyprpm/src/core/DataState.cpp:106–107`:

```cpp
if (std::filesystem::exists(p.filename)) {
    if (!NSys::root::install(p.filename, (PATH / filename).string(), "0755"))
        // fail
}
```

`hyprpm/src/helpers/Sys.cpp:154–163` passes the source to elevated `install` with owner/group 0. Its punctuation filter does not check containment, source ownership, regular-file type, or link resolution. `exists()` establishes accessible metadata; it does not establish that the invoking user can read the file's contents. A file with protected contents beneath traversable directories can therefore pass both existence checks and subsequently be read by the privileged copy.

**Call chain and preconditions — source-confirmed.** User authorizes plugin add/update and elevation (`main.cpp:120–125,136–147`) → valid fetched manifest (`PluginManager.cpp:240–258` or `768–788`) → successful header checks → nonempty build step(s) and output (`Manifest.cpp:58,121`) → unprivileged build (`PluginManager.cpp:330–338` or `834–842`) → output existence check → source path in repository state object → `DataState::addNewPluginRepo` → second existence check → elevated install → readable/executable cached copy. This occurs before plugin enable/load, so ELF validity and compositor permission checks are irrelevant to the disclosure. The build need not create the referenced existing file.

**Limits and corrections.** High: a repository can influence a root-readable copy source during an otherwise authorized install, crossing the intended read-permission boundary. This is not an unauthenticated remote read, nor does it bypass a denied elevation request. Source ancestors must be searchable by the ordinary user for `exists()` to succeed; some other users' private home directories therefore block the dossier's generalized private-key example. The target must also be copyable by `install`; a directory or an inaccessible metadata path does not automatically qualify. The raw dossier's exact traversal depth is environment-dependent. No sensitive file was read, copied, or tested in this verification.

**Minimal fix.** Resolve outputs under the checkout through symlink-safe directory-relative APIs, require regular artifacts owned/readable by the invoking user, and stage/copy unprivileged before elevating only destination installation. Enforce the source boundary at the privileged operation too, avoiding a check/use race.

## F-PM-07

**Claim.** Repository-name/filename fields loaded from state can point outside the cache and ultimately reach `dlopen`; the dossier also treats names reported by the compositor as a loading input.

**Evidence — source-confirmed.** `hyprpm/src/core/DataState.cpp:217–245` reads repository names and filenames directly from parsed state. `hyprpm/src/core/PluginManager.cpp:997–1005` maps plugin names back to a stored repository name. The enabled-plugin loop at `1025–1039` forms:

```cpp
loadUnloadPlugin(HYPRPMPATH / repoForName(p.name) / p.filename, true)
```

There is no confinement check on this composed path; absolute state components can discard its base. `loadUnloadPlugin` first checks global-vs-running ABI (`1052–1059`) and then sends `/plugin load` (`1062–1063`). `src/ipc/s1/Commands.cpp:1778–1794` extracts the path and invokes `CPluginSystem::loadPlugin` with the IPC requester's PID. `src/plugins/PluginSystem.cpp:20–59` applies dynamic plugin permissions; only an allowed request reaches the internal loader and `dlopen` (`67–77`).

**Reachability and guards — source-confirmed.** A prepared state entry must be discovered by `DataState::getPluginStates` (`48–61`), be enabled and nonfailed (`PluginManager.cpp:1028–1034`), pass header validation (`953–956`), and satisfy the running ABI (`1056`). Unless forced, an already-listed plugin is skipped (`1036–1037`). Permission enforcement defaults false (`src/config/values/ConfigValues.cpp:717`); when enabled, denial or pending confirmation blocks immediate loading (`PluginSystem.cpp:21–56`; `DynamicPermissionManager.cpp:152–156,194–206,226–229`). There is no claim here of bypassing that permission gate.

The missing initial attacker capability matters. Normal cache directories are created with mode 755 (`DataState.cpp:65–78`) and state is installed root-owned mode 644 (`28–37`; `Sys.cpp:154–163`). Manifest plugin names are checked (`Manifest.cpp:28,91`), and DataState generates their stored filenames as `<name>.so` (`DataState.cpp:103,112`). A repository does not directly choose an arbitrary state filename during an ordinary valid installation. A different unprivileged UID cannot simply rewrite this state under normal ownership. Existing writable/symlinked cache trees would require separately established preconditions; no source shows header installation making cache state writable to ordinary users.

**What holds and what does not.** Unconfined stored load paths are present. A preexisting privileged-state compromise or F-PM-02 destination violation can make them relevant, but this is not a separate demonstrated Medium privilege bypass. Names from `j/plugins list` feed the **unload** loop (`PluginManager.cpp:1013–1017`), not the enabled-plugin load path. `src/ipc/s1/Commands.cpp:1795–1805` looks up an already-loaded module for unload; it does not `dlopen` that name. The dossier conflates the two operations. A usable native module and permitted load are also required; an arbitrary copied file is not automatically executable plugin code.

**Severity and fix.** Informational as a dependent sink; count the actual destination/state corruption at F-PM-02 rather than multiplying its impact. Validate stored components, ownership, and final descriptor-relative containment before load. ABI/version matching is a compatibility guard, not a content-authentication mechanism. No module was loaded or IPC sent during verification.

## F-PM-08

**Claim.** Ordinary peers can replace installed headers during a chmod window, or appended package metadata can redirect validation and silently inject a plugin backdoor.

**Evidence and guards — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:419–420` requires the managed `headersRoot/share/pkgconfig/hyprland.pc`. At `423–445` it asks pkgconf for include flags and reads a selected `hyprland/src/version.h`. It validates the header commit (`459–481`) and recorded installed ABI (`483–487`), returning success only at `489`. This is compatibility validation, not a full header-content signature check.

The search path at `PluginManager.cpp:1156–1158` is:

```cpp
static const auto str = std::format("{}/share/pkgconfig:$PKG_CONFIG_PATH", DataState::getHeadersPath());
```

The managed package directory is prepended, not appended. `hyprland.pc.in:1–8` supplies include paths rooted in its configured prefix. Headers originate from privileged installation (`Makefile:37–54`; `PluginManager.cpp:632–639`). That command changes files to 644 and directories to add traversal permission. Those modes do **not** grant the ordinary owner of the invoking process write access to root-owned files. There is no same-user write window created by reducing mode 755 to 644; neither mode grants non-root write permission on root-owned files.

**Dependency control — executed.** The isolated [dependency_controls.py](/home/wilf/data/.claude-scratch/agent-reports/work/03-hyprpm/dependency_controls.py) in the assigned scratch directory created two harmless package directories with different include paths, passed only a small explicit environment to `/usr/bin/pkgconf`, and checked that the first package directory wins. [dependency-controls.json](/home/wilf/data/.claude-scratch/agent-reports/work/03-hyprpm/dependency-controls.json) records success. No installed hyprpm cache, compiler build, socket, or privileged operation was used. This confirms the ordinary same-package lookup priority on this host, not every possible pkgconf behavior. The local pkgconf manual (`/usr/share/man/man1/pkgconf.1.gz`, decompressed lines `738–756`) documents that environment mechanisms such as preloaded files can override ordinary package discovery; that still requires control of the caller's environment and is not evidence that an appended duplicate package automatically wins.

**Call chain and missing attacker step — source-confirmed.** Environment/package metadata → `headersValid` and `getPluginBuildEnv` (`PluginManager.cpp:423,1140–1141`) → unprivileged plugin build (`330–338,834–842`) → output installation. With normal managed-cache ownership and ordinary appended `PKG_CONFIG_PATH`, the dossier does not establish how another UID can replace the main package or headers. If an attacker already controls the invoking user's environment/toolchain, that user-code execution capability is already outside the proposed header-only trust boundary. Writable build inputs consumed by root are a different, confirmed concern at F-PM-03; do not confuse that checkout with the root-owned installed headers.

**Severity and corrections.** Reject the claimed independent Medium header-replacement vulnerability; retain only informational build-reproducibility/supply-chain hardening. A full file manifest could detect drift but its absence is not proof of an attacker write capability. To reopen the claim, demonstrate an untrusted actor's actual ability to write a normal installed header or supply metadata ahead of the managed package without already controlling the invoking process environment/privileged installation. No such permission-changing source path is identified here.

**Minimal fix direction.** Use explicit package/tool paths and a controlled build environment where reproducibility requires them; verify header-tree ownership and consider a content manifest. Preserve the distinction between compatibility and authenticity checks.

## F-PM-09

**Claim.** Work-directory recreation, a shared temporary state file, privileged removal, and environment-derived IPC paths collectively permit file-operation races and socket impersonation.

**Evidence — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:123–134` removes an existing path, tests existence again, then calls `mkdir(..., S_IRWXU)`. The plugin work path is predictable and shared by add/update for the same username (`208,697`); the header checkout is also predictable (`521`). There is no interprocess lock. `updatePlugins` even discards `createSafeDirectory`'s boolean result (`720`). Concurrent invocations can erase or reuse each other's trees.

`hyprpm/src/core/DataState.cpp:28–37` uses one file for all state updates:

```cpp
std::ofstream of(std::format("{}.temp-state", getTempRoot()), std::ios::trunc);
// write, close
return NSys::root::install(std::format("{}.temp-state", getTempRoot()), to, "644");
```

The pathname is reopened by the privileged copy after closing the ordinary user's stream. A source-level interleaving is sufficient to establish the correctness race: writer A closes its state; writer B truncates/replaces the same file; A's install then consumes B's bytes or partial state. These are not unique temporary files or an atomic transaction. No such race was executed against hyprpm.

**Reachability and privilege limits — source-confirmed.** Two same-user add/update/enable/disable operations using the same runtime directory can enter `writeState` (`DataState.cpp:122,195,287`); their shared staging file and work paths produce a closed concurrency trigger. The cache copy still needs the selected privilege tool to authorize it (`Sys.cpp:154–163`). New runtime subdirectories use 0700 (`DataState.cpp:21–22`; `PluginManager.cpp:198–200,133`); newly created cache directories use 755 and root-owned state uses 644 (`DataState.cpp:65–78`; `Sys.cpp:161`). With a properly private XDG runtime directory, an unrelated UID cannot place entries there. `DataState::getTempRoot` does not verify an existing directory's ownership, symlink status or mode (`12–24`); this is hardening debt, not proof that the normal runtime parent is attacker-writable. Same-UID malicious code is a materially stronger precondition than a different local user.

**Other subclaims — source-confirmed, with limits.** `hyprpm/src/helpers/Sys.cpp:136–151` canonicalizes before invoking elevated removal and checks a textual cache prefix. This is not an atomic directory-relative removal, but the dossier supplies no cross-user attacker capability to replace the normally root-owned ancestors. A general exploitable root-removal race is not confirmed. `HyprlandSocket.cpp:38–44,55–69` accepts runtime/signature environment strings and composes the socket pathname without a signature-character or peer-owner check. That is source-confirmed environment trust, not a timing race. It requires control of the caller's environment or an accessible selected socket; the default private runtime directory is again relevant. Furthermore, as F-PM-03 explains, a forged running-version reply does not supply the installed-version fields used for header checkout.

**Severity and fix.** Low for demonstrated same-user concurrency/state-corruption risk; no cross-user escalation, root deletion, or compositor compromise is demonstrated. Use a per-user operation lock, unique private workspaces, exclusive no-follow temporary files, and atomic bounded state installation. Validate existing runtime directories and selected socket identity separately from the race fixes.

## F-PM-10

**Claim.** This informational bundle alleges arbitrary trusted build commands, an overly broad name validator, PATH-based privilege-tool selection, a deprecated-flag null-pointer crash, and permissive pin hash lengths.

**Build trust — source-confirmed.** `hyprpm/src/core/Manifest.cpp:51–55,114–118` retains build commands, and `PluginManager.cpp:330–338,834–842` executes them as shell commands in the user's context. This is the intended native-plugin build model, not a sandbox escape. The once-only general disclaimer is at `163–170`, but each add still has an explicit repository/author trust prompt (`178–189`). Update does not repeat it (`main.cpp:136–158`). An update-review option is reasonable hardening; the trust model itself is informational. Privileged build execution is separately assessed in F-PM-03.

**Names — source-confirmed.** `hyprpm/src/core/Manifest.cpp:7–8` allows `=` and accepts the empty string through `all_of`; the comment's narrower description is inaccurate. Plugin destinations are built as `<name>.so` (`DataState.cpp:103`) and passed in an argument vector (`Sys.cpp:161`), so an equals sign alone is not evidence of shell assignment or path traversal. No concrete unexpected placement or security impact from `=` is established. Require nonempty names and remove unintended characters for consistency, without promoting this observation to an exploit.

**Privilege-tool lookup — source-confirmed.** `hyprpm/src/helpers/Sys.cpp:41–59` searches PATH and accepts a regular file with others-execute permission, without an ownership/integrity check. `subin` (`65–84`) stores the bare tool name, not the resolved full path. `cacheSudo` (`107–111`) and the other helpers then invoke that selected name. Control of an earlier PATH directory or executable therefore affects program selection, but requires compromising a trusted caller environment/filesystem component; it is not a new way for an arbitrary different UID to gain privileges under a safe PATH. More ordinary build/dependency executables are also PATH-selected. Resolve and retain approved absolute helper paths where the threat model requires it; do not claim this prevents all environment/toolchain compromise.

**Deprecated flag — source-confirmed, crash not established.** `hyprpm/src/core/PluginManager.hpp:92` declares a default-null global `unique_ptr`. Parsing `--notify-fail`/`-nn` reaches `main.cpp:61–64` before construction at `94`:

```cpp
g_pPluginManager->notify(ICON_INFO, 0, 10000, "...");
```

The local CLI flag is a complete source trigger for an invalid non-static member call on a null object. However, `CPluginManager::notify` is nonvirtual (`PluginManager.hpp:67`) and its body (`PluginManager.cpp:1089–1091`) uses no instance fields: it directly calls `NHyprlandSocket::send`. Thus C++ undefined behavior is confirmed, while a guaranteed machine-code null-memory dereference or deterministic crash is **not** established by this source. Typical unsanitized compilation can reach the socket routine without reading `this`; an instrumentation/build-specific result would be needed for a definite crash claim. No actual hyprpm execution was used, since that path attempts IPC. Even if a particular build crashes, the affected process is the user's invoked CLI, not the compositor.

**Hash validation — source-confirmed.** `hyprpm/src/core/PluginManager.cpp:59–61` checks hexadecimal characters without a nonempty or exact-length constraint. Manifest pin callers (`284,801`) quote the resulting value (`291,810`), so this is not a demonstrated shell-injection path. Empty, abbreviated or excessively long IDs can produce Git errors or ambiguous revision behavior, and command success/identity checks should be improved. It does not justify a separate remote-execution severity.

**Verdict and minimal fixes.** Partially: the design/validation observations and undefined deprecated-flag call hold; the asserted deterministic DoS and unsupported equals-sign impact do not. Low for the local correctness defect, otherwise informational. Remove the deprecated branch or defer notifications until manager construction, narrow names/hash syntax to the supported contract, retain approved helper paths, and make update trust behavior explicit. No crash or live-session behavior is claimed as executed evidence.

