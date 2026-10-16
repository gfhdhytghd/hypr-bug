# fres-07-cursor-shm-sigbus — F-RES-07 cursor-role SHM read without fault containment

Reproduces the SIGBUS in the cursor path: drive a `wlr_virtual_pointer`, take a
pointer `enter`, `wl_pointer.set_cursor` with a surface backed by a `wl_shm`
buffer, commit, then **`ftruncate` the backing file before the compositor flushes**.
`updateCursorShm` reads the shrunk mapping → SIGBUS, no fault guard.

## Reproduce (ISOLATED NESTED compositor only — this crashes the compositor)
```
bash build.sh
nest/run-nested.sh --host wayland-1 ./repro
```

## Observed (nested Hyprland, fork e5a03b6a5, 2026-10-16)
`COMPOSITOR_EXITED status=135` (SIGBUS / 总线错误, core dumped). Client exits 0.
First boot may flake `NO_SOCKET`; just re-run. Host session unaffected.

**Do not run against a real session with unsaved work.** Agent evidence, not
human-confirmed. Finding: [`../../F-RES-07.md`](../../F-RES-07.md).
