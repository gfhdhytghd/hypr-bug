# 2026-10 Full-Codebase Audit — raw agent output (pre-verification)

Static-review snapshot from a sweep of the fork (`gfhdhytghd/Hyprland`, branch `main`,
commit `397cb0efb`, version `0.56.0`). Eleven dossiers were produced by audit subagents
and four by the orchestrating agent; **none of these findings have been
reproduced/verified yet** (unlike the color-management dossier in `agent-report/`). Treat
every claim as machine-generated until a repro client or a nested-session crash confirms it.

- Total: **135 findings across 15 dossiers**.
- Highest-priority candidates to verify first (reachability × impact):
  1. **F-INP-01** — *any* Wayland client binds `zwp_virtual_keyboard_manager_v1`
     (ungated; keyboard permission defaults to ALLOW; `share_states` default) →
     synthesized Super+key enters the real keybind pipeline
     (`InputManager.cpp:1680-1703`) → user-configured **exec** dispatchers fire →
     session-user code execution from a plain GUI app. Companions: **F-INP-02**
     (lockscreen bypass via `invokeReleaseCallbacks` skipping `canInvokeNow`),
     **F-INP-05** (vkbd stuck-key permanent physical-keyout DoS), **F-INP-06**
     (ungated virtual-pointer incl. killmode SIGKILL without lock check).
  2. **F-SEAT-01/02/03** — clipboard `set_selection` ignores focus+serial
     (background clipboard hijack), `start_drag` unvalidated (buttonless DnD),
     and DnD-active swallows all button events until the target voluntarily
     `finish()`es → desktop-wide click-disable DoS. Plus **F-SEAT-06**
     (output-management has zero session-lock gating) and **F-SEAT-05**
     (unbounded custom modes).
  3. **F-XW2-01** — Hyprland chmods the X11 socket to 0666 and starts Xwayland
     **without `-auth`**; the comment claims an xhost ACL, but no authority file is
     passed. If the default Xserver policy does not reject foreign peers, **any
     local user** can connect to the session's DISPLAY → XRecord keylogging, XTEST
     injection, clipboard theft. Minimal test + downgrade condition recorded in
     `08-xwayland-chains.md` (blocked on a host with a GPU; nested run impossible
     here — no `/dev/dri`).
  4. **F-PR2-01 / F-PR2-04 / F-RES-07** — virtual-keyboard keymap `mmap` without
     `fstat`, wl_shm pool `ftruncate`-after-create, unsealed cursor shm → SIGBUS
     crash of the compositor from any client. Trivially reproducible clients expected.
  5. **F-IPC-01 / F-IPC-02** — fork regression: IPC `eval`/`repl` gives arbitrary Lua
     (with `os`/`io` loaded) and `dispatch` string-interpolates user input into Lua
     source → RCE at compositor privilege once the socket is reachable (see also
     F-IPC-04: no `SO_PEERCRED` uid check).
  6. **F-XW-01 / F-XW2-03/04** — X property handling reads `value_len` items as
     4-byte atoms without checking `reply->format` (`format=8` trick) → heap OOB
     read from any X11 client (multiple sites).
  7. **F-DSK-01/02** — group destroy-chain asymmetry: expired WP deref (null-this
     crash) and `CGroup::destroy` infinite-loop hang (IPC-reachable).
  8. **F-BOOT-01/02** — crash reporter predictable path without `O_EXCL`/`O_NOFOLLOW`
     (env-poisoned symlink → arbitrary file write) + signal-handler `sh -c` PATH
     exec (`kill -SEGV` = code execution when PATH poisoned).
  9. **F-PERM-01/02/03/07 + F-RULE-02** — permission-system integrity: PATH-resolved
     dialog whose raw stdout *is* the user verdict; compositor fds leak into every
     exec'd app (no `close_range` in `spawnRawProc`); dialog privileges keyed on bare
     pid (fork-race → uncloseable/priority windows); fail-open when dialog binary
     missing; exec_token rules spoofable via self-`setenv`.
  10. **F-PROTO-01/02** — xdg self-parent popup infinite loop (freeze) and expired
      popup-content `CXDGSurfaceResource` deref in `grab`/`reposition` (segv).
  11. **F-CM-01** — static color-management caches unbounded from any client →
      O(N²) + memory-exhaustion DoS (fork code).
  12. **F-PR2-02** — input-method-v2 first-binder-wins → text-injection +
      surroundingText theft of the focused app.
  13. **F-PM-01/02 (hyprpm)** — command injection via unquoted `--rev` into `sh -c`;
      manifest `repository.name` path traversal into `sudo install` of a root-owned
      state file → lateral RCE.
  14. **F-RES-01/02 (vendor hyprgraphics 0.5.1)** — PNG decompression bomb
      (uncaught bad_alloc) and hand-written BMP parser heap + **stack** overflow;
      needs an upstream Hypr-Ecosystem PR.

| Dossier | Area | Findings | Notes |
|---|---|---|---|
| `01-fork-cm-hdr.md` | fork HDR/CM subsystem (diff vs upstream) | 9 (F-CM-01..09) | old 29-item list partially fixed; these are new/newly-activated |
| `02-ipc-hyprctl.md` | IPC socket, hyprctl, systemd units | 8 (F-IPC-01..08) | fork exposed Lua config API over IPC |
| `03-hyprpm.md` | hyprpm plugin manager | 10 (F-PM-01..10) | sh -c string building + sudo path helpers are the root cause |
| `04-config-lua-helpers.md` | config parsing, Lua bindings, ICC loader | 4 (F-ICC-01/02, F-CFG-01, F-RULE-01) | F-ICC-01 = OOB read in vcgt logging, crafted ICC → crash |
| `05-xwayland.md` | XWayland property parsing | 1 (F-XW-01) | format-confusion heap OOB read |
| `06-protocols-shell.md` | xdg / layer-shell / compositor core | 10 (F-PROTO-01..10) | +1 suspected bug (Subcompositor.cpp:147) |
| `07-protocols-misc.md` | screencopy/shm/IME/dmabuf/lease/gamma/lock | 10 (F-PR2-01..10) | several lock-screen-gating gaps |
| `08-xwayland-chains.md` | XWayland/X11 chains (socket, props, selection, DnD, lock, spoof) | 11 (F-XW2-01..11) | cross-user DISPLAY pending GPU-host test; xwayland/ has zero sessionLock checks |
| `09-seat-clipboard.md` | seat / clipboard / DnD / output-management | 15 (F-SEAT-01..15) | clipboard serial/focus validation absent vs wlroots; DnD click-disable DoS |
| `10-desktop-lifecycle.md` | window/group/workspace lifecycle, layout algos | 14 (F-DSK-01..14) | destroy-chain asymmetry; group-destroy hang via IPC |
| `11-resource-loading.md` | image decoding, cursors, textures | 12 (F-RES-01..12) | most decoder flaws live in vendor hyprgraphics; in-repo SIGBUS + XCursor traversal |
| `12-bootchain-crash.md` | start-hyprland, crash reporter, env/PATH trust | 10 (F-BOOT-01..10) | symlink arbitrary write; signal-handler PATH exec |
| `13-input-keybinds.md` | devices, input manager, keybind execution | 10 (F-INP-01..10) | F-INP-01 is the audit's top Critical (vkbd → exec RCE) |
| `14-permission-dialogs-tokens.md` | DynamicPermissionManager, AsyncDialogBox, tokens | 10 (F-PERM-01..07, F-RULE-02/03, F-TOK-01) | fail-open + PATH trust + pid-based privilege |
| `15-eventloop-timers.md` | event-loop timer hygiene | 1 (F-TMR-01) | screenshare stop-timer never removed |

## Cross-cutting root-cause patterns

1. **Client-trusted numbers/ids** — `value_len`/`format`, HDR metadata, popup parents,
   dmabuf sizes, mode geometries: validation happens late or not at all.
2. **Serial/credential validation skipped** — clipboard, DnD, cursor (the check
   exists — `pointerButtonSerialValid` — but isn't wired to the paths that need it).
3. **Session-lock gating holes** — output-management, gamma/CTM, drm-lease,
   screenshare mirror, whole `src/xwayland/`, vkbd release queue.
4. **PATH as a trust anchor** — dialog/welcome/donate/update/xdg-open/crash-report
   helpers; and PATH-resolved child spawns inherit un-closed privileged fds.
5. **`/bin/sh -c` string building** — hyprpm, crash reporter, XWayland launch.
6. **Destroy-chain asymmetry** — groups, touch device, timers, popup content refs.

## Status tracking

| ID | Verified? | Upstream? | Fix? |
|---|---|---|---|
| all | ☐ | ☐ | ☐ |

Move evaluated items into `agent-report/<category>/<issue-id>.md` with linked agent artifacts once each has a
compilable client and a nested-session crash/leak observation.

⚠️ Reproduction clients for the crash-class findings will kill a live session —
nested/headless only. Keep this repository private until fixes land upstream.
