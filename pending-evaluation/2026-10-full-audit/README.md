# 2026-10 Full-Codebase Audit — raw agent output (pre-verification)

Static-review snapshot from a sweep of the fork (`gfhdhytghd/Hyprland`, branch `main`,
commit `397cb0efb`, version `0.56.0`). Five audit areas were produced by subagents and
two by the orchestrating agent; **none of these findings have been reproduced/verified
yet** (unlike the color-management dossier in `reports/`). Treat every claim as
machine-generated until a repro client or a nested-session crash confirms it.

- Total: 63 findings across 8 dossiers.
- Highest-priority candidates to verify first (reachability × impact):
  0. **F-XW2-01** — Hyprland chmods the X11 socket to 0666 and starts Xwayland **without
     `-auth`**; the comment claims an xhost ACL, but no authority file is passed. If the
     default Xserver policy does not reject foreign peers, **any local user** can connect
     to the session's DISPLAY → XRecord keylogging, XTEST injection, clipboard theft.
     Minimal test + downgrade condition recorded in `08-xwayland-chains.md`
     (blocked on a host with a GPU; nested run impossible here — no `/dev/dri`).
  1. **F-PR2-01 / F-PR2-04** — virtual-keyboard keymap `mmap` without `fstat`, and
     wl_shm pool `ftruncate`-after-create → SIGBUS crash of the compositor from any
     client. Trivially reproducible clients expected.
  2. **F-IPC-01 / F-IPC-02** — fork regression: IPC `eval`/`repl` gives arbitrary Lua
     (with `os`/`io` loaded) and `dispatch` string-interpolates user input into Lua
     source → RCE at compositor privilege once the socket is reachable.
  3. **F-XW-01** — `_NET_WM_STATE`/`_NET_WM_WINDOW_TYPE`/`WM_PROTOCOLS` parsed with
     `value_len` as `xcb_atom_t` count without checking `reply->format` → heap OOB
     read from any X11 client (`format=8` trick).
  4. **F-PROTO-01 / F-PROTO-02** — xdg self-parent popup infinite loop (freeze) and
     use of expired popup-content `CXDGSurfaceResource` in `grab`/`reposition` (segv).
  5. **F-CM-01** — static color-management caches unbounded from any client → O(N²)
     + memory-exhaustion DoS (fork code).
  6. **F-PR2-02** — input-method-v2 first-binder-wins → text-injection +
     surroundingText theft of the focused app.
  7. **F-PM-01 / F-PM-02 (hyprpm)** — command injection via unquoted `--rev` into
     `sh -c`; manifest `repository.name` path traversal into `sudo install` of a
     root-owned state file → lateral RCE.

| Dossier | Area | Findings | Notes |
|---|---|---|---|
| `01-fork-cm-hdr.md` | fork HDR/CM subsystem (diff vs upstream) | 9 (F-CM-01..09) | old 29-item list partially fixed; these are new/newly-activated |
| `02-ipc-hyprctl.md` | IPC socket, hyprctl, systemd units | 8 (F-IPC-01..08) | fork exposed Lua config API over IPC |
| `03-hyprpm.md` | hyprpm plugin manager | 10 (F-PM-01..10) | sh -c string building + sudo path helpers are the root cause |
| `04-config-lua-helpers.md` | config parsing, Lua bindings, ICC loader | 4 (F-ICC-01/02, F-CFG-01, F-RULE-01) | F-ICC-01 = OOB read in vcgt logging, crafted ICC → crash |
| `05-xwayland.md` | XWayland property parsing | 1 (F-XW-01) | format-confusion heap OOB read |
| `06-protocols-shell.md` | xdg / layer-shell / compositor core | 10 (F-PROTO-01..10) | +1 suspected bug (Subcompositor.cpp:147) |
| `07-protocols-misc.md` | screencopy/shm/IME/dmabuf/lease/gamma/lock | 10 (F-PR2-01..10) | several lock-screen-gating gaps |
| `08-xwayland-chains.md` | XWayland/X11 chains (launch socket, props, selection, DnD, lock, spoof) | 11 (F-XW2-01..11) | F-XW2-01 cross-user DISPLAY pending GPU-host test; lockscreen: xwayland/ has zero sessionLock checks |

## Status tracking

| ID | Verified? | Upstream? | Fix? |
|---|---|---|---|
| all | ☐ | ☐ | ☐ |

Move confirmed items into `reports/` + `repro/<bug>/` dossiers once each has a
compilable client and a nested-session crash/leak observation.

⚠️ Reproduction clients for the crash-class findings will kill a live session —
nested/headless only. Keep this repository private until fixes land upstream.
