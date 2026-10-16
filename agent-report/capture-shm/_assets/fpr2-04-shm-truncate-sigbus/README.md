# fpr2-04-shm-truncate-sigbus — F-PR2-04 destructive SHM pool remap

Reproduces the SHM fault: create a `wl_shm_pool` over a backing file, attach +
commit a buffer, then **`ftruncate` the backing file to 0** and re-commit. When
the compositor maps/reads the now-shrunk mapping it takes SIGBUS, with no fault
containment around the client-controlled memory.

## Reproduce (ISOLATED NESTED compositor only — this crashes the compositor)
```
bash build.sh
nest/run-nested.sh --host wayland-1 ./repro
```

## Observed (nested Hyprland, fork e5a03b6a5, 2026-10-16)
`COMPOSITOR_EXITED status=135` (SIGBUS / 总线错误, core dumped). Client exits 0
(display error after re-commit). First boot may flake `NO_SOCKET`; just re-run.
Host session unaffected.

**Do not run against a real session with unsaved work.** Agent evidence, not
human-confirmed. Finding: [`../../F-PR2-04.md`](../../F-PR2-04.md).
