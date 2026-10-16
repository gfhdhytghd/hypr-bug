# fproto02-popup-grab-uaf — F-PROTO-02 popup grab on an expired content surface

Reproduces the xdg-shell use-after-free: create an `xdg_surface`, give it a
`xdg_popup` role (parent has a role + positioner), **destroy the popup's content
`xdg_surface` while the popup role object survives**, then issue `xdg_popup.grab`
with a real `wl_seat` + serial. The grab path locks the expired content weak-ptr
and dereferences null → compositor crash.

## Reproduce (ISOLATED NESTED compositor only — this crashes the compositor)
```
bash build.sh
# via the nested harness (boots a throwaway nested Hyprland, never the host session):
nest/run-nested.sh --host wayland-1 ./repro
```

## Observed (nested Hyprland, fork e5a03b6a5, 2026-10-16)
`COMPOSITOR_EXITED status=134`; crash report signal 11 (SIGSEGV), top frames:
```
#4 CWeakPointer<CWLSurfaceResource>::lock()
#5 CXDGShellProtocol::addOrStartGrab(CSharedPointer<CXDGPopupResource>)
```
Client exits 0 (display error after the grab). Host session unaffected.

**Do not run against a real session with unsaved work.** Agent evidence, not
human-confirmed. Finding: [`../../F-PROTO-02.md`](../../F-PROTO-02.md).
