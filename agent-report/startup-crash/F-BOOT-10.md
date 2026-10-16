# F-BOOT-10 Informational: startup / crash handling notes

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Startup / crash reporting. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Missing HOME breaks tilde expansion; empty-string UB is false. Overlay retention is source-supported with weak keys; environment propagation and packaging notes are informational.

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

[Full dossier-level verification report](_evidence/full-audit-12-bootchain-crash.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/12-bootchain-crash.md). These remain agent evidence, not human-confirmed material.
