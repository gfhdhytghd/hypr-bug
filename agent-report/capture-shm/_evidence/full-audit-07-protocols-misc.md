# Miscellaneous Wayland protocols: upstream verification

Baseline: **e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb** (`e2ad066c9`), verified from the read-only upstream worktree.  
Dossier: [07-protocols-misc](../../pending-evaluation/2026-10-full-audit/07-protocols-misc.md).  
Date: **2026-10-15**.  
Method: pinned-source review of request handlers, callers, lifetime and validation guards, protocol contracts, and available dependency headers. Evidence is labeled **source-confirmed** or **executed**. No connection to a compositor, desktop IPC, full build, network access, or device test is performed. Isolated checks, if any, remain in the assigned scratch directory. Runtime, GPU, and visual consequences are not inferred from missing checks alone. This is a defensive verification report; it contains no attack client or operational reproduction recipe.

## Summary

Final disposition: **3 confirmed, 6 partially, 1 rejected, 0 inconclusive**. A partial verdict preserves only the specifically established subclaims; it does not validate the original severity or reproduction sketch.

| ID | Verdict | Severity (revised) | Upstream location (e2ad066c9) | Tracker note (≤ 25 words) |
|---|---|---|---|---|
| F-PR2-01 | confirmed | High | `src/protocols/VirtualKeyboard.cpp:74-108` | Unbounded string parsing of client mappings persists; the one-byte example is invalid and sandboxed clients cannot bind. |
| F-PR2-02 | partially | High (IME trust); Medium (serial correctness) | `src/managers/input/InputMethodRelay.cpp:17-87`; `src/protocols/InputMethodV2.cpp:181-185` | Unsandboxed first-IME trust and ignored serial hold; injection requires a focused text-input, and password disclosure depends on application behavior. |
| F-PR2-03 | partially | Low (validation gap); GPU impact unproven | `src/protocols/LinuxDMABUF.cpp:289-328`; `src/protocols/types/DMABuffer.cpp:26-43` | Backing-size validation is absent; plane continuity and import-failure guards exist; GPU hangs and cross-client reads are unproven. |
| F-PR2-04 | confirmed | High | `src/protocols/core/Shm.cpp:65-100`; `src/render/gl/GLFramebuffer.cpp:143,231-236` | Mutable SHM backing and destructive remap remain unsafe; creation bounds checks do not protect later access. |
| F-PR2-05 | partially | Medium | `src/protocols/Screencopy.cpp:21-37,92-93`; `src/managers/screenshare/ScreenshareSession.cpp:99-140` | Region clipping is missing and allocation uses requested dimensions; claimed INT32_MAX SHM/OOM recipe and signed-overflow mechanism are unproven. |
| F-PR2-06 | partially | Medium (capture policy); blanket frozen-desktop claim rejected | `src/render/Renderer.cpp:1176-1180,1738-1785`; `src/managers/screenshare/ScreenshareFrame.cpp:150-190,366-397` | No capture lock gate; transition and retained snapshots need policy. Mirror includes lock rendering, contradicting unconditional frozen-desktop disclosure. |
| F-PR2-07 | partially | Medium (purpose/correctness); victim memory corruption unproven | `src/protocols/TextInputV1.cpp:39-48,92-94`; `src/managers/input/TextInput.cpp:289-294` | Purpose uses hint incorrectly; deletion conversion has real semantic/bounds issues, but signed negative indices and client-provided v1 serials are legitimate. |
| F-PR2-08 | rejected | Informational (optional lease policy) | `src/output/Monitor.cpp:291-316`; `src/protocols/DRMLease.cpp:113-137,241-252` | Only offered non-desktop outputs are leaseable; DPMS-off desktop connectors are not offered. Missing lock gate alone does not prove desktop takeover. |
| F-PR2-09 | partially | Low | `src/protocols/IdleInhibit.cpp:46-51`; `src/managers/screenshare/ScreenshareManager.cpp:110-154` | Object quotas are absent and non-desktop inhibitors affect idle policy; completed pending frames are pruned, and event delivery is guarded. |
| F-PR2-10 | confirmed | Medium | `src/protocols/GammaControl.cpp:49-120,141-159`; `src/protocols/CTMControl.cpp:20-63` | Unsandboxed owners can change output color state during lock; gamma support, VCGT and single-controller guards limit reachability; physical blackout untested. |

## Shared reachability and evidence limits

**Source-confirmed:** `src/Compositor.cpp:267-271,288` installs a global filter. Ordinary connections pass it; security-context sandbox connections only see nonprivileged globals. `src/protocols/SecurityContext.cpp:142-166,221-222` identifies those clients. `src/managers/ProtocolManager.cpp:348-403` whitelists core SHM, DMA-BUF, idle inhibition and text-input, but not virtual-keyboard, IME, screencopy, session-lock, DRM lease, gamma or CTM. Thus the dossier's blanket “all clients, no security-context gate” statement is false. Unless stated otherwise, privileged-extension findings require an **unsandboxed local Wayland connection**. A socket connection is not remote network access by itself.

**Source-confirmed:** capture also consults dynamic permissions: `src/managers/permissions/DynamicPermissionManager.cpp:79-84,122-149`; `ecosystem:enforce_permissions` defaults to false (`src/config/values/ConfigValues.cpp:717`). With enforcement enabled, pending capture retains a private snapshot, and denied capture renders a denial image (`src/managers/screenshare/ScreenshareFrame.cpp:161-169,366-397`). This limits disclosure but does not itself validate geometry or SHM backing-file lifetime.

**Source-confirmed:** Hyprland implements its own SHM pools (`src/protocols/core/Shm.cpp:81-101,186-204`), not `wl_shm_buffer_begin_access()` / `end_access()`. Libwayland wire/object validation does not establish fd length, seal a file against truncation, validate application serial semantics, or impose compositor-specific geometry bounds. Correctly typed, client-owned objects suffice for the reviewed paths; none depends on forging another client's object ID. Installed dependency headers are host evidence, not a version-pinned dependency build.

The two example reports named in the brief are absent in this checkout. The requested report structure and evidence requirements are followed directly. All ten numbered dossier records are covered; unnumbered controls are addressed at the end.

## F-PR2-01

**Claim.** A virtual-keyboard keymap request maps a client-supplied fd/length and parses it without establishing safe file and string bounds.

**Evidence — source-confirmed.** `src/protocols/VirtualKeyboard.cpp:149-176` creates the resource and connects it to the input manager. Its keymap handler at **74-108** has an `mmap` failure check but no backing-size, seal, format, or string-termination validation:

```cpp
auto keymapData = mmap(nullptr, len, PROT_READ, MAP_PRIVATE, keymapFd.get(), 0);
// MAP_FAILED is handled
auto xkbKeymap = xkb_keymap_new_from_string(xkbContext, sc<const char*>(keymapData),
                                          XKB_KEYMAP_FORMAT_TEXT_V2, XKB_KEYMAP_COMPILE_NO_FLAGS);
```

The parser receives a pointer, not `len`. The host declaration documents the length-taking alternative as `xkb_keymap_new_from_buffer` (`/usr/include/xkbcommon/xkbcommon.h:1329-1346`); it does not support the dossier's assumed `new_from_bytes` name. The file descriptor is owned by `CFileDescriptor` (line 76), so this is not an fd leak.

**Path and preconditions — source-confirmed.** An unsandboxed client's valid virtual-keyboard resource reaches this callback directly. Request decoding checks neither the mapping's backing extent nor C-string termination. A file mapping can succeed while some requested pages have no backing; the string parser may access such pages, or continue outside a mapping without a terminator. No per-access SIGBUS recovery surrounds this call. These are client-controlled memory-access bounds in the compositor process, independent of whether generated key events are later permitted. No crash was executed.

**Correction.** A one-byte file containing a single character does **not** establish the proposed crash: the remainder of its partial page is zero-filled, supplying a terminator. Crossing the file's exact byte EOF is not automatically a fault. Missing termination and access to pages wholly beyond backing are the relevant conditions. `SIGSEGV` has a crash reporter (`src/Compositor.cpp:295-298`), contrary to the dossier's “no SIGSEGV handler” statement; it is not safe memory-access recovery. No `SIGBUS` recovery was found. Huge-length parser memory amplification is not separately demonstrated.

**Severity and fix.** High for a local client causing compositor-process memory faults; no remote-access or code-execution claim. Parse with a bounded API, validate a practical length and file type/extent, and protect against concurrent backing-file changes using a safe snapshot or equivalent fault-safe ingestion. A one-time `fstat` alone does not close truncation races.

## F-PR2-02

**Claim.** The first input-method client can monopolize the IME role, receive application context and keyboard events, and commit text without serial validation.

**Evidence — source-confirmed.** `src/protocols/InputMethodV2.cpp:335-372` creates an input method without application identity authorization. `src/managers/input/InputMethodRelay.cpp:17-36` accepts the first live IME; later objects receive `unavailable`. Destroying that accepted IME resets the weak reference at **39-48**. The protocol explicitly requires at most one IME per seat (`protocols/input-method-unstable-v2.xml:60-70`); uniqueness itself is intentional. Trusting whichever unsandboxed client arrives first is the policy concern.

`InputMethodV2.cpp:163-184` stores pending text/deletion state and applies it without using the supplied serial:

```cpp
m_resource->setCommit([this](CZwpInputMethodV2* r, uint32_t serial) {
    m_current = m_pending;
    m_pending.reset();
    m_events.onCommit.emit();
});
```

This contradicts the mismatched-serial state rule at `protocols/input-method-unstable-v2.xml:302-307`. `done()` at `InputMethodV2.cpp:254-255` sends the event without maintaining a corresponding validation counter.

**Path and guards — source-confirmed.** Accepted IME commits reach `InputMethodRelay.cpp:28-36`, which **requires `getFocusedTextInput()`**. That function at **73-87** matches the current focused surface, prefers enabled inputs, then falls back to a matching input without the enabled test. `TextInput.cpp:259-295` translates the state to that application's v1/v3 events. Conversely, application context passes from `TextInput.cpp:231-256` through `InputMethodV2.cpp:242-251` to the accepted IME. The v1 enable path checks actual focus (`TextInput.cpp:58-64`), and v3 enter is client-matched (`InputMethodRelay.cpp:152-159`). This is not arbitrary text insertion into every window. An application must use the supported text-input path, and surrounding-text disclosure is limited to what it sends. Password-purpose corruption is covered by F-PR2-07; it cannot force an application to supply text it withheld.

**Keyboard path — source-confirmed.** The accepted IME's grabs are created at `InputMethodV2.cpp:204-216`; `hasGrab()` at **262-263** does not require activation or a focused text-input. `src/managers/input/InputManager.cpp:1680-1729` chooses the IME for eligible hardware keys and forwards them to `InputMethodV2.cpp:294-302` → `InputMethodV2.cpp:59-62`. There are keyboard-enabled/permission, keybinding, hook, input-capture and handler-consumption gates before forwarding. There is no session-lock check on this branch. The generic handler stack (`src/managers/SeatManager.cpp:52-79`) returns false when empty; no production registration of that stack was found in this baseline. A generic seat grab is different: `SeatManager.cpp:755-758` rejects it while locked, but IME grabs do not use that function. This supports a confidentiality concern for keys that reach the IME, including during a lock; it does not prove every key is delivered or that a particular lock client accepts IME text.

**Preconditions and corrections.** The attacker must have an unsandboxed connection and occupy the IME role while it is vacant. A later client cannot displace an existing legitimate IME by merely creating another object. “Forever unavailable” lasts while that accepted role remains occupied, not after its owner disconnects. A blanket polkit/terminal/lock-screen injection claim is unsupported without verifying their text-input use. C strings are decoded by Wayland, so “arbitrary strings” does not imply an unterminated-wire-string vulnerability.

**Severity and fix.** High for a malicious accepted IME observing/altering other applications' input under these preconditions; Medium for stale-state serial handling considered independently. Use an explicit IME authorization policy and count/validate `done` state, reset pending state across context changes, and define the intended keyboard-grab policy during lock. Environment variables such as `XDG_CURRENT_DESKTOP` are not an authentication mechanism. No keyboard capture or injection was executed.

## F-PR2-03

**Claim.** DMA-BUF metadata is not checked against each plane fd's extent, allegedly allowing GPU faults and cross-client memory disclosure.

**Evidence — source-confirmed.** `src/protocols/LinuxDMABUF.cpp:148-179` bounds plane indices, rejects duplicates and (v5) inconsistent modifiers. `create` / `create_immed` count supplied fds at **200-223**. `verify()` at **289-328** rejects missing plane 0, holes, no planes, nonpositive dimensions, and arithmetic beyond `UINT32_MAX`:

```cpp
const auto computedSize = sc<uint64_t>(m_attrs.offsets.at(i))
                        + sc<uint64_t>(m_attrs.strides.at(i)) * m_attrs.size.y;
if (computedSize > UINT32_MAX) { /* protocol error */ }
```

It does not query the plane's backing size or derive a format-specific required plane count. This confirms the missing compositor-side check, not acceptance by the GPU. `commence()` at **268-286** probes PRIME import when a main-device fd exists. Texture import at `src/protocols/types/DMABuffer.cpp:26-43` retries an implicit modifier and rejects failure. `src/render/GLRenderer.cpp:249-254` returns null on EGL-image failure; `src/render/OpenGL.cpp:627-677` passes dimensions, plane fds, offsets and pitches to `eglCreateImageKHR`, and handles `EGL_NO_IMAGE_KHR`. `LinuxDMABUF.cpp:256-259` then discards an unsuccessful buffer.

**Path and guard — source-confirmed.** Client plane metadata passes Hyprland's structural checks to PRIME/EGL import; only a successfully imported texture can become a usable buffer. This protocol is exposed to security-context clients when DMA-BUF is available (`ProtocolManager.cpp:258-262,396-398`). Import requires actual acceptable DMA-BUF fds; an arbitrary fd is not equivalent. The checked path does not bypass kernel memory ownership or demonstrate an out-of-bounds GPU mapping.

**Correction.** The tracker title mentions plane count: contiguous supplied-plane counting is implemented, but exact per-format count validation is delegated to import. Do not report an unchecked array plane index or a hole bypass. The dossier's leap from missing `fstat` to reliable GPU reset, and especially other clients' BO disclosure, assumes additional driver defects. No driver, render-node, EGL, kernel fault, or disclosure test was performed. `drmGetFdBuffer` is not established as an applicable generic validation API, and simple `stride * full height` is not a universal multi-plane/modifier size rule.

**Severity and fix.** Low for defense-in-depth validation inconsistency; the claimed GPU DoS / disclosure severity remains unestablished. Add supported format/modifier-aware extent checks where reliable, preserve downstream import rejection, and classify outcomes using isolated importer/driver evidence before assigning GPU-security impact.

## F-PR2-04

**Empirical reproduction — executed.** Nested harness (2026-10-15): client created a wl_shm pool + buffer, committed it, then ftruncated the pool backing file to 0 and re-committed — the nested compositor took SIGBUS (`COMPOSITOR_EXITED status=135`), client exit 0.

**Claim.** Client-controlled SHM backing can become invalid after pool creation; a failed resize also leaves existing buffers using an invalid mapping.

**Evidence — source-confirmed.** `src/protocols/core/Shm.cpp:103-114,186-204` checks file length on pool creation. `create_buffer` validates positive geometry, format and layout at **138-165**, using checked size arithmetic in `src/helpers/Format.cpp:53-80`. These checks are real. They do not pin file contents or backing pages. The pool is mapped `MAP_SHARED` at **81**, and the buffer holds a strong pool reference (`Shm.cpp:23`; `Shm.hpp:49-52`), so destroying the pool resource does not itself invalidate a buffer.

Resize validates non-shrinking requested size and current file extent at `Shm.cpp:125-135`, then unmaps the old region before obtaining a new mapping (**90-100**):

```cpp
munmap(m_data, m_size);
m_size = size_;
m_data = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd.get(), 0);
if UNLIKELY (m_data == MAP_FAILED)
    LOG(Log::ERR, "Couldn't mmap {} bytes from fd {} of shm client", m_size, m_fd.get());
```

`beginDataPtr()` at **65-66** returns `m_data + m_offset` without a validity check. `CWLSHMBuffer::good()` at **73-74** detects `MAP_FAILED`, but existing-buffer capture validates `buffer->m_resource->good()`, not that method (`src/managers/screenshare/ScreenshareFrame.cpp:63-64,88-90`). `src/render/gl/GLFramebuffer.cpp:143-147` checks only a null data pointer and later writes through it at **231-236**. An invalid nonnull mapping is not caught by those checks.

**Path and preconditions — source-confirmed.** A legitimately created SHM pool/buffer reaches capture's `share` → output-commit `copy` → `copyShm` → framebuffer readback (`Screencopy.cpp:122-135`; `ScreenshareManager.cpp:27-43`; `ScreenshareFrame.cpp:171-174,440-484`). The copied buffer remains strongly referenced. Concurrent file truncation can invalidate pages despite earlier `fstat`; failed pool remapping can leave the stored pointer invalid. The same pool data is also read by the cursor path (`src/protocols/core/Compositor.cpp:673,788-824`). SHM itself is available to security-context clients; screencopy additionally requires the privileged global and is subject to the permission behavior above. No compositor fault was executed.

**Correction.** Merely shrinking a file does not ordinarily make `mmap` fail; it makes later page access unsafe. Remap failure instead requires an actual mapping failure, such as address-space/resource exhaustion. Constructor/new-buffer guards exist, contrary to a completely unchecked-pool interpretation. The dossier's `MAP_FIXED_NOREPLACE` suggestion is unrelated, and mandatory `F_SEAL_WRITE` would conflict with ordinary writable SHM operation. SIGSEGV crash reporting exists; it provides no recovery.

**Severity and fix.** High for compositor-process faults caused by a local client. Preserve the old mapping until a replacement succeeds, surface resize failure, and fail every access to invalid pools. For mutable backing, use fault-safe SHM access or an appropriate compatibility-preserving backing-lifetime strategy; an initial or repeated `fstat` alone cannot close the race.

## F-PR2-05

**Claim.** Screencopy accepts unclipped region geometry, allowing oversized capture resources and alleged stride overflow / guaranteed compositor OOM.

**Evidence — source-confirmed.** `protocols/wlr-screencopy-unstable-v1.xml:57-63` promises clipping to output extents. `src/protocols/Screencopy.cpp:21-37` passes client coordinates and dimensions directly to a managed region session; it validates the output object, not the region. `src/managers/screenshare/ScreenshareManager.cpp:110-145` selects or creates a session without clipping or size limits. `ScreenshareSession.cpp:99-101,139-141` scales/rounds the region and uses its size unchanged:

```cpp
m_captureBox.scale(PMONITOR->m_scale).round();
// SHARE_REGION:
m_bufferSize = m_captureBox.size();
```

`Screencopy.cpp:82-93` converts those dimensions into the stride and buffer announcement. The host `minStride` declaration is `int minStride(..., int32_t width)` (`/usr/include/hyprgraphics/egl/Egl.hpp:46`); its implementation is outside the pinned tree. Signed `width * bpp` UB is therefore **not proven by this call site**. Out-of-range floating/integer conversions also require separate care after scaling.

**Path and constraints — source-confirmed.** The geometry flows from region request to session constraints and frame announcement. For actual copying, `ScreenshareFrame.cpp:88-115` requires a real valid resource, matching dimensions, and supported format. SHM construction rejects nonpositive dimensions and checks stride, offset, products and pool extent (`src/protocols/core/Shm.cpp:138-165`; `src/helpers/Format.cpp:53-80`). A pool size is positive `int32_t` (`Shm.cpp:103-114,186-195`); a sparse file does not bypass these arithmetic/layout checks. Thus the dossier's near-maximum two-dimensional SHM buffer is not shown to be constructible.

For accepted buffer geometry, `ScreenshareFrame.cpp:440-455` creates a framebuffer with the requested size. Even a **pending** dynamic-permission decision can allocate `m_tempFB` via **161-168,501-507**. The allocation is a GL texture/framebuffer, not the claimed CPU framebuffer (`src/render/gl/GLFramebuffer.cpp:16-40`). Allocation requires positive dimensions (`src/render/Framebuffer.cpp:9-10`), and GL framebuffer completeness is a fatal assertion (`GLFramebuffer.cpp:66-67`) rather than a recoverable failure. Hyprland's region dimensions have no output-size or renderer-limit guard before this resource work. Actual GL allocation/failure consequences depend on the accepted dimensions and backend.

**Preconditions and corrections.** Requires an unsandboxed capture client and a live output. Disclosure has a separate permission gate; geometry/resource handling precedes or survives that gate. Negative region sizes can be announced but do not pass the positive SHM-buffer creation guard, so they are not alone a proven negative-size framebuffer trigger. Unbounded distinct session creation is reviewed under F-PR2-09. No allocation stress, GL failure, integer-overflow harness, or OOM test was executed.

**Severity and fix.** Medium for confirmed protocol-contract failure and avoidable client-controlled resource demand beyond output bounds; reliable OOM/crash is not established here. Intersect logical regions with output extents, reject empty/invalid results, check scaling/conversions/stride arithmetic and renderer limits, and handle framebuffer allocation failure without aborting the compositor. Validate geometry before permission-pending snapshot allocation.

## F-PR2-06

**Claim.** Capture continues through locking and allegedly returns an unprotected desktop mirror for five seconds and indefinitely after `locked`.

**Evidence — source-confirmed.** The capture entry and shared copy path do not check session lock: `src/protocols/Screencopy.cpp:27-50,103-165`; `src/managers/screenshare/ScreenshareFrame.cpp:72-147,150-190,366-397`. Image-copy monitor sessions use the same manager (`src/protocols/ImageCopyCapture.cpp:15-49`). Capture permissions and `noScreenShare` behavior are real guards, as described above; they are not lock-state guards.

`src/render/Renderer.cpp:1176-1180` continues rendering workspaces while a lock is pending until client-locked/client-denied or `shallConsiderLockMissing()` becomes true (unless explicitly using xray). `src/managers/SessionLockManager.cpp:244-250` uses **`misc:lockdead_screen_delay`**, whose default is **1000 ms**, range 0–5000 (`src/config/values/ConfigValues.cpp:599`). The five-second timer at `SessionLockManager.cpp:113-132` is a fallback for sending `locked`, not the default duration of ordinary-workspace rendering. `misc:session_lock_xray` defaults false (`ConfigValues.cpp:583`).

**Decisive correction to the alleged frozen-mirror mechanism — source-confirmed.** The full monitor scene draws workspace **then lock screen** (`src/render/scene/MonitorScene.cpp:26-31`):

```cpp
g_pHyprRenderer->renderWorkspace(ctx, MONITOR, MONITOR->m_activeWorkspace, now, GEOMETRY);
g_pHyprRenderer->renderLockscreen(ctx, MONITOR, now, GEOMETRY);
```

`Renderer.cpp:1747-1785` supplies an opaque black primer once lock-missing/client-locked/client-denied applies, and draws a mapped lock surface. `src/render/OpenGL.cpp:825-831,2375-2399` copies the composed scene to the mirror; `ScreenshareFrame.cpp:184-224` samples that mirror. Successful composed output submissions service capture after the copy FB is prepared (`src/output/OutputCommitCoordinator.cpp:175-205`). Normal locked output frames therefore **replace** desktop pixels with the lock composition. The absence of a lock reference in the screenshare directory does not prove that lock surfaces are absent from captured textures. If an opaque lock screen already covers a region, this path does not reveal the workspace behind it.

**What remains — source-confirmed.** Before the lock surface/primer hides a region, capture can sample the same still-visible workspace composition; this is a pre-`locked` transition, not proof of a secure-lock bypass. The installed ext-session-lock contract permits waiting for a lock frame and defines the secure boundary at `locked` (`.../ext-session-lock-v1.xml:77-99`). It does not state the dossier's blanket “all sharing must fail whenever locking begins” rule.

There are narrower post-transition concerns that the dossier did not distinguish. A private permission-pending snapshot is retained at `ScreenshareFrame.cpp:161-168,501-532`, then used on approval at **384-387**, with no lock-event invalidation. It can contain an earlier desktop frame. Separately, a permitted **window-source** image-copy session uses `ScreenshareFrame.cpp:323-339` → `Renderer.cpp:585-631` to render a mapped window directly, without the monitor workspace lock gate. `ScreenshareManager.cpp:34-40` requires that window to intersect its monitor before copying; `ScreenshareFrame.cpp:376-381` still honors deny/no-screen-share. These are source-supported reasons to define lock-time capture policy, not support for the claimed permanently frozen **output** mirror. No capture pixels, timing, or physical presentation were tested.

**Severity and fix.** Medium for potential retained/window content disclosure under the explicit permission/source preconditions. A blanket five-second live-output leak after completed locking, or unconditional indefinite desktop-mirror disclosure, is rejected. To enforce strict lock confidentiality, gate all capture sources and pending completions, invalidate retained snapshots when lock begins, and test lock-transition/presentation ordering in an isolated compositor before claiming exact durations or visible contents.

## F-PR2-07

**Claim.** The v1 bridge allegedly sends an enormous deletion index due to unsigned underflow, corrupts content purpose, and wrongly trusts a client-provided serial.

**Evidence — source-confirmed.** `src/protocols/TextInputV1.cpp:39-41` really substitutes the **hint** for purpose:

```cpp
m_pendingContentType = {true, /* hint translation */,
    purpose > sc<uint32_t>(ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD) ? hint + 1 : hint};
```

`src/managers/input/TextInput.cpp:245-251` forwards that stored value to the accepted IME. A normal password purpose with ordinary hints can therefore become a normal or unrelated purpose; this is a real compatibility/sensitive-context signaling defect. Both purpose and hint must be translated independently. It does not manufacture surrounding text that the application did not send.

The deletion bridge at `TextInput.cpp:289-294` uses:

```cpp
INPUT->deleteSurroundingText(std::string(ime->m_current.preeditString.string).length()
                               - ime->m_current.deleteSurrounding.before,
                           ime->m_current.deleteSurrounding.after
                               + ime->m_current.deleteSurrounding.before);
```

Its target is `deleteSurroundingText(int32_t index, uint32_t length)` (`TextInputV1.cpp:92-94`). The v1 XML specifies a **signed cursor-relative index** and an unsigned length (`/usr/share/wayland-protocols/unstable/text-input/text-input-unstable-v1.xml:313-325`). Thus a negative index is required for deletion before the cursor; a size_t underflow expression is not by itself a gigantic unsigned wire index. The addition can wrap in `uint32_t`, and using the **new preedit string's length** in the cursor-relative offset is a separate semantic problem. IME-v2 commit ordering removes old preedit, deletes surrounding text, then inserts new preedit (`protocols/input-method-unstable-v2.xml:292-300`); it does not define this offset in terms of new preedit length.

**Executed — isolated ordinary-value conversion check.** Scratch [`text_input_conversion.cpp`](/home/wilf/data/.claude-scratch/agent-reports/work/07-protocols-misc/text_input_conversion.cpp) was compiled with the host C++ compiler and run without Wayland or graphics. Its output is saved in [`text_input_conversion.out`](/home/wilf/data/.claude-scratch/agent-reports/work/07-protocols-misc/text_input_conversion.out):

```text
preedit_bytes=0 before=3 index=-3 length=5
preedit_bytes=4 before=3 index=1 length=5
password_purpose=8 hint=0 forwarded_purpose=0
```

This checks language-level conversion and the expression with ordinary inputs, not application behavior or compositor execution. It disproves the dossier's generic “empty preedit produces a huge unsigned index” mechanism and shows how a nonempty new preedit shifts the index.

**Serial negative control — source-confirmed.** `TextInputV1.cpp:46-48` records `commit_state(serial_)`. That is expected: v1's request explicitly supplies the application's known-state serial (`.../text-input-unstable-v1.xml:193-195`); preedit and commit events identify that known state (**234-248**, and `TextInput.cpp:279,287`). Replacing it with an independently generated server counter, as proposed by the dossier, would violate that design. This differs from IME-v2's ignored `done` counter in F-PR2-02.

**Path, severity and fix.** A legitimate focused v1 application sends content type/state; the bridge forwards it to the selected IME. A selected IME's deletion state follows the focused relay in F-PR2-02 to this conversion and the application's v1 events. V1 access alone grants no other-client object access; the malicious-IME variant still requires the accepted unsandboxed IME role. Medium for incorrect text operations and sensitive-purpose signaling; no vulnerable victim implementation or memory-corruption outcome was demonstrated. Translate purpose correctly, derive deletion coordinates from v1/v2 semantics with checked representability and sum, and retain client-state serial pairing. Clamping deletion to preedit length is wrong: surrounding-text deletion may legitimately exceed preedit.

## F-PR2-08

**Claim.** An arbitrary client can obtain a normal inactive/DPMS-off desktop connector during lock and take over its KMS scanout; repeated grants allegedly exhaust kernel leases.

**Evidence and decisive guard — source-confirmed.** DRM globals are created only for usable DRM backends (`src/managers/ProtocolManager.cpp:239-247`) and are filtered from security-context clients. More decisively, the **only source call to `lease->offer()`** is `src/output/Monitor.cpp:306-316`, inside the non-desktop branch:

```cpp
if (m_output->nonDesktop) {
    // ... choose matching DRM backend
    lease->offer(m_self.lock());
    return;
}
```

The disabled-rule branch returns before that at **291-303**; changing DPMS does not mark a desktop output non-desktop. `src/protocols/DRMLease.cpp:241-252` sends connector objects only for `m_offeredOutputs`. `request_connector` at **113-137** rejects duplicates and resources absent from that lease device's connector list. It takes a typed connector object, not an arbitrary integer KMS connector ID. `offer()` at **334-347** also checks DRM backend identity.

**Reachable path — source-confirmed.** An unsandboxed client's request can name an offered non-desktop connector, proceed through `submit` (**140-161**), then `CDRMLeaseResource` (**38-89**) checks monitor liveness/already-leased state and delegates to `Aquamarine::CDRMLease::create(outputs)`. Only backend success sets `m_isBeingLeased` and sends a lease fd; failure sends `finished`. There is no session-lock or DPMS test in this constructor. For an intentionally offered non-desktop output, independent scanout is the purpose of DRM leasing; this alone does not establish a desktop-lock isolation failure. The host Aquamarine declaration is not proof of a successful kernel grant.

**Correction.** The claimed normal-desktop takeover is blocked **before** the grant handler because that connector is never advertised in the ordinary desktop path. No need to assume that an active CRTC will produce `EBUSY`; that kernel assertion was not verified. Ordinary idle power management is not an offer path. Existing successful leases are bounded by offered outputs and `m_isBeingLeased`; repeated failed/request objects can consume userspace metadata, but that is not evidence of unbounded successful kernel leases. Cleanup erases request/lease resources at **160-161,326-331**, and lease destruction releases the backend object at **98-101**.

**Severity and fix direction.** Informational for an explicit choice about non-desktop devices during lock. If product policy requires suppressing VR/non-desktop scanout, implement a deliberate authorization/revocation policy, but do not track this dossier's normal-monitor exploit as confirmed. Generic object quotas belong to F-PR2-09. No DRM/KMS operation was executed.

## F-PR2-09

**Claim.** Several protocols retain unlimited objects and side effects, supposedly including monotonically growing completed pending frames, unrestricted event flooding, and unconditional permanent DPMS prevention.

**Evidence — source-confirmed.** The listed object-creation handlers have no per-client quotas. Important distinctions follow:

| Object / side effect | Pinned evidence and actual limits |
|---|---|
| Idle inhibitors | `src/protocols/IdleInhibit.cpp:46-51` retains an inhibitor and emits it to `src/managers/input/IdleInhibitor.cpp:10-33`. The latter assumes an unknown/non-desktop surface is visible (**23-27**); `recheckIdleInhibitorStatus()` immediately inhibits on that flag (**38-41**). Ordinary desktop views instead require mapped/input-accepting/nonzero-alpha state (**44-53**). A roleless surface can take the former branch; this is a real idle-policy weakness independent of object count. |
| Virtual keyboards | `src/protocols/VirtualKeyboard.cpp:164-176` creates resources without a count limit; `VirtualKeyboard.hpp:37,57` stores pressed keys and keyboards. `VirtualKeyboard.cpp:53-57` deduplicates pressed keycodes. This is a `uint32_t` keycode vector, not an enforced 512-key bound. `InputManager.cpp:1215-1250,1680-1706` applies device enablement/permission and forwarding gates. |
| Screencopy frame resources | `src/protocols/Screencopy.cpp:39-50,191-197` retains frames until their destruction or manager cleanup. One-shot completion does not erase the Wayland frame object; the protocol tells the client to destroy it after ready/failed (`protocols/wlr-screencopy-unstable-v1.xml:96-100`). An uncooperative client can retain many objects. |
| Pending-frame bookkeeping | `src/managers/screenshare/ScreenshareSession.cpp:187-193` adds a **weak** frame reference. `ScreenshareManager.hpp:239-250` distinguishes weak `m_pendingFrames` / `m_sessions` from owned managed sessions. `ScreenshareManager.cpp:27-43` skips expired/unshared/done frames and **erases expired or done entries after each output-commit pass**. Completed-frame entries do not grow monotonically while output commits continue. Unshared live frames can remain pending, and stalled outputs delay pruning. |
| Managed sessions | `ScreenshareManager.cpp:110-145` retains owned sessions keyed by client, source and region, without a count limit. Erasure is on session stop (**148-154**); the idle share timer merely sets sharing false (`ScreenshareSession.cpp:88-93,153-169`). No client-disconnect removal of these managed sessions was found. Distinct region sessions are stronger retention evidence than the dossier's completed-pending-frame claim. |
| IME popups and grabs | `src/protocols/InputMethodV2.cpp:187-216`; owning protocol containers at `InputMethodV2.hpp:151-154`, with destruction erasure at `InputMethodV2.cpp:347-356`. Owner lists contain weak references and are not independently proven ownership leaks. Multiple grabs exist; rejected IMEs do not become the relay's selected input method (F-PR2-02). |
| Lock surfaces | `src/protocols/SessionLock.cpp:113-119,203-235` checks inert locks/output liveness but not duplicate output use. Multiple surfaces can be retained per accepted lock/output; `SessionLock.hpp:93-95` owns them and **177-182** of the implementation erases them on destruction. This requires a live non-inert lock object, not another client's lock resource. |

**Cleanup excerpt — source-confirmed.** `src/managers/screenshare/ScreenshareManager.cpp:43` explicitly prunes finished pending entries:

```cpp
std::erase_if(m_pendingFrames, [&](const WP<CScreenshareFrame>& frame) {
    return frame.expired() || frame->done();
});
```

**Pointer negative control — source-confirmed host headers.** `/usr/include/hyprutils/memory/WeakPtr.hpp:154-190` distinguishes `expired()` from `valid()`/boolean conversion; `lock()` refuses expired/destroying objects and returns a strong pointer only for lockable ownership. The pending lists do not keep frame payloads alive merely by storing a `WP`. The source ownership declarations and explicit pruning, rather than assumptions about `std::weak_ptr`, determine the findings above.

**Path and preconditions — source-confirmed.** Core surface → idle-inhibitor creation → input-manager visibility classification → `PROTO::idle->setInhibit(true)` is available even to a security-context client. The resulting effect reaches **inhibitor-obeying idle notification timers**, not a direct universal DPMS-off switch (`src/protocols/IdleNotify.cpp:38-49,86-89,118-123`). An external idle daemon's policy determines screen blanking/locking. Destroying the surface/inhibitor triggers cleanup (`IdleInhibit.cpp:9-17`; `IdleInhibitor.cpp:16-18`); “permanent” means while the relevant resource/classification persists. Protocol-resource retention requires continued allocation, and privileged extension access follows the shared filter discussion. Object decoding/allocation failure remains handled by Wayland/resource checks; no resource-exhaustion test was run.

**Severity and fix.** Low for resource-accounting and idle-policy concerns; the dossier has not established a uniquely amplified compositor OOM or all-client event flood. Events normally reach the focused/selected input path, not every client. Add appropriate per-client limits, remove managed sessions on client departure/staleness, retain completed-frame pruning, and determine real visibility rather than unconditionally trusting an unknown surface. Do not impose a universal inhibitor time limit that defeats legitimate long-running media use.

## F-PR2-10

**Claim.** A client already controlling gamma or CTM can alter the locked output's color transform and impair lock-screen visibility.

**Evidence — source-confirmed.** `src/protocols/GammaControl.cpp:13-44` validates output liveness, allows only one gamma controller per monitor and requires a supported gamma size. Its `set_gamma` callback (**49-120**) checks `gammaRampsInUse()` (**59-63**), makes reads nonblocking, and requires the exact LUT byte count with an extra-byte probe (**65-99**). It then marks the table set and applies it. There is **no lock-state condition** in that callback or `applyToMonitor()` (**141-159**):

```cpp
m_monitor->m_output->state->setGammaLut(m_gammaTable);
if (!m_monitor->m_state.test())
    m_monitor->m_output->state->setGammaLut({});
g_pHyprRenderer->damageMonitor(m_monitor.lock());
```

The table is staged for a normal output commit; damage schedules rendering. `src/output/Monitor.cpp:2597-2609` reaches the backend commit, with no lock-specific gamma reset. `gammaRampsInUse()` means an ICC VCGT ramp is currently set (`Monitor.cpp:2546-2571`), not that every color-managed output necessarily blocks the request. On destruction, an installed client table is reset (`GammaControl.cpp:129-135`). This is state restoration, not protection during the resource lifetime.

CTM requests reject blocked resources, dead outputs and invalid matrix components (`src/protocols/CTMControl.cpp:20-47`), then `commit` applies matrices at **50-63**. Only the first CTM manager is active; later resources are blocked (**91-103**). `setCTM()` at **128-173** either applies or animates into `CMonitor::setCTM()` (`Monitor.cpp:1715-1718`), which schedules a frame. `src/render/Renderer.cpp:2628-2677` stages the client CTM subject to fullscreen/color-management interactions. No session-lock restriction appears in these handlers, the animation updates, or the lock manager's lifecycle callbacks.

**Path and preconditions — source-confirmed.** An unsandboxed client with the available gamma-controller slot for a supporting output, or the active CTM-manager slot, can supply a protocol-valid but visibility-destructive color transform. Validation permits such transforms because it checks table size / matrix component validity, not lock-screen visibility. Normal render/output commits apply the state irrespective of the lock-screen scene. The client's control can be established before lock and retained through it; acquiring it before lock is not a special bypass and is not universally required. A pre-existing legitimate controller may prevent acquisition. Security-context clients cannot bind these globals; dynamic screencopy/keyboard permission checks are not gamma/CTM authorization.

**Correction.** This is a local output-availability policy defect, not an unlock or authentication bypass, memory corruption, or proven physical blackout on every GPU. Backend testing may reject a gamma state, ICC VCGT may block it, fullscreen CM may compose/override CTM, and CTM may animate. Both protocols log their operations (`GammaControl.cpp:57,145`; `CTMControl.cpp:47,54`), contrary to “no logs.” The upstream baseline **already has** the VCGT `gammaRampsInUse()` guard; it must not be attributed solely to the fork. The other protocol-control observations in this record are reviewed below.

**Severity and fix.** Medium for impairing lock-screen visibility while the accepted output state is held. Source establishes delivery/staging and absence of a lock guard; actual physical appearance, duration and recovery were not tested. Define which trusted color managers may continue during lock; save/suspend untrusted output color state and restore it safely, including pending animation callbacks. Merely rejecting new requests at lock time does not clear already staged state.

## Dossier appendix and controls

These are the dossier's unnumbered observations, not extra finding IDs. They are not additional counted findings.

| Control record | Verdict | Upstream evidence and correction (source-confirmed) |
|---|---|---|
| Gamma fork comparison | partially | `src/protocols/GammaControl.cpp:59-63,129-159` uses upstream `setGammaLut` and already rejects active VCGT ramps. The fork's separate client-ramp API is absent. A claim that this guard first appears only in the fork is false. No fork commit or historical tree was modified or treated as upstream evidence. |
| Color-management ICC unreachable assertion | partially | Default ICC creation is rejected at `src/protocols/ColorManagement.cpp:136-141`, but **debug mode continues to create a live ICC creator** at **143-150**. `debug:full_cm_proto` defaults false (`src/config/values/ConfigValues.cpp:738`). Raw fd/offset/length are stored at **523-527**; the create path at **489-520** logs a TODO rather than parsing the file. The dossier's fork-specific “dead code” assurance cannot be transferred unconditionally to upstream. These lines alone do not establish a malformed-ICC parser crash. |
| Color-management parametric validation | partially | Named transfer functions and primaries have rejection branches (`ColorManagement.cpp:586-608,630-653`); luminance relations are checked at **674-686,707-719**. Explicit primaries at **660-672** are assigned without the claimed coordinate-range check in that handler. This is a limited control review, not a general clearance of color-management safety or the separate CM dossier. |
| Session-lock seat focus / ordinary grab control | confirmed | `src/managers/SeatManager.cpp:755-758` rejects ordinary grabs when locked; `src/desktop/state/FocusState.cpp:231-236` rejects focus on non-lock surfaces. Lock creation clears focus/grab (`src/managers/SessionLockManager.cpp:98-101`). IME keyboard grabs follow a different path (F-PR2-02). |
| Session-lock role reuse / unlock ownership | partially | `src/protocols/SessionLock.cpp:11-69,203-235` does not set/check a Wayland surface role. Desktop view assignment at `src/desktop/view/SessionLock.cpp:29-30` does not substitute for the `m_role` check used by `src/protocols/XDGShell.cpp:886-902`. `SessionLock.cpp:113-119,122-139` operates on the resource's non-inert lock; another client cannot name it via an arbitrary object ID. No third-party unlock path is established. The role mismatch is a correctness concern, not demonstrated cross-client privilege escalation. |
| HyprlandSurface and AlphaModifier numeric controls | confirmed | Opacity checks surface lifetime and [0,1] at `src/protocols/HyprlandSurface.cpp:26-39`; alpha normalizes a `uint32_t` into [0,1] at `src/protocols/AlphaModifier.cpp:24-31`. These narrow controls hold. They are not whole-protocol safety proofs. |
| BackgroundEffect / CommitTiming blanket validation assurance | partially | Background blur copies a typed nullable region and checks the surface (`src/protocols/BackgroundEffect.cpp:26-39`); duplicate live effect objects are rejected (**111-119**). Commit timing checks surface existence, duplicate pending timestamps and duplicate timers (`src/protocols/CommitTiming.cpp:17-25,99-125`), but the timestamp handler builds a timespec without an explicit nanosecond-range check (**28-33**). “All numeric fields have range checks” is too broad; this report does not establish a new exploitable timing defect from that omission. |

## Validation and scope record

**Executed:** read-only Git identity check returned `e2ad066c9ffd45ddfbe4dc76d2de16b382fbe3eb`; direct source/installed-header inspection; one ordinary-value C++ text-input conversion check under `/home/wilf/data/.claude-scratch/agent-reports/work/07-protocols-misc/`. Source findings are explicitly separated from that executed arithmetic check. No attack client, faulting-mapping harness, exhaustion workload, GPU import, capture, key interception, live lock, compositor process, full build or desktop IPC was run. Therefore physical display, GPU reset, victim application crashes and runtime timing remain untested.

Report completeness was checked against all unique dossier finding IDs, allowed verdict spellings, per-row tracker-note word limits, absence of pending placeholders, and existence/line bounds of cited upstream files. Only this report and assigned scratch artifacts were written. README, dossier, upstream source, other reports and Git state were left untouched.

Local review does not authorize GitHub publication. Repository guidance points contributors to [Hyprland's AI policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md) and [issue guidelines](https://wiki.hypr.land/contributing-and-debugging/issue-guidelines/); violating the project's AI interaction rules can result in repository or organization bans. No GitHub interaction was performed.
