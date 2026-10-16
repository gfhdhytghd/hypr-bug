# F-IPC-04 IPC socket peer identity / permissions rely on directory access

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Missing UID checks and socket chmod hold; mandatory 0700 instance directories block the claimed cross-user access through a shared parent.

**Claim.** Neither IPC socket checks peer UID or sets owner-only socket permissions, so a shared or nonstandard runtime directory permits another local user to execute commands or read events.

**Verdict: partially.** Missing defense-in-depth checks are real; the proposed cross-user trigger omits the private instance directory and does not close.

**Evidence — source-confirmed.** `src/ipc/s1/Unix.cpp:54-64` obtains peer credentials but returns only `credentials.CRED_PID`. `Unix.cpp:252-264` binds and listens without changing socket mode. Socket2 likewise binds and listens at `src/ipc/s2/Unix.cpp:92-112` and accepts peers without a UID comparison at `Unix.cpp:120-147`.

The decisive guard is in `src/Compositor.cpp:222-231`:

```cpp
m_instancePath = std::format("{}/{}", m_hyprTempDataRoot, m_instanceSignature);
// Existing instance path causes startup failure.
if (mkdir(m_instancePath.c_str(), S_IRWXU) < 0) {
    // Startup failure.
}
```

`S_IRWXU` requests 0700. Umask can remove permission bits; it cannot add group/other search permission. Existing instance paths are rejected (`Compositor.cpp:224-227`), so simply precreating a permissive instance directory is not the described path. The parent is also created as 0700 when absent (`Compositor.cpp:215-219`), although preexisting parents are accepted without checking ownership/mode. Missing or invalid runtime roots bail out at `Compositor.cpp:196-202`; a nonstandard root merely warns at `204-205`.

**Reachability / blocked chain — source-confirmed.** A different UID can potentially enumerate a permissive runtime parent and learn the signature, but pathname resolution to either socket must still traverse the newly created 0700 instance directory. The kernel denies that traversal before `accept4` or any command handler. Knowledge of the signature changes no permission. A same-UID peer can traverse the directory; that is the administrative scope discussed in F-IPC-01. Root or a principal able to alter the victim's directory is outside this claimed ordinary cross-user trigger.

**Severity and corrections.** Informational hardening, not a demonstrated High cross-user execution or disclosure defect. On Linux, pathname stream socket connection also requires socket write permission; a typical 0022 umask gives a socket mode of 0755, not world-writable. The dossier alternates between 0777 and 0666 creation masks; pathname sockets use 0777 filtered by umask. Neither a 0755 runtime parent nor a permissive umask defeats the nested 0700 guard. Active replacement of directories under a hostile writable parent is a different threat requiring separate race, ownership, and filesystem analysis; it is not established by this finding.

**Minimal fix direction.** Checking UID before registering either peer, setting socket mode to 0600, and validating runtime-directory ownership would improve defense in depth. Requiring the literal `/run/user/<uid>` pathname is unnecessary: owner-only, correctly owned nonstandard runtime roots can be valid. Any permissions change should target the pathname or appropriate filesystem handle; ordinary socket `fchmod` must not be assumed to chmod the pathname inode on Linux.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
