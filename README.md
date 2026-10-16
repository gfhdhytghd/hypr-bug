# hypr-bug

## CM bounties

Bounties are currently available **only for color-management (CM) issues**. You may pick any issue in the CM sections below and contact me by email at [me@wilflin.me](mailto:me@wilflin.me) or on Discord as `wilflin_82651` with your quote for:

- Manually confirming the issue.
- Submitting a fix PR that is accepted and merged by Hyprland.

Please quote each task separately. **Payment is in cryptocurrency.**

**Claim the issue and agree the price with me before beginning public work** (including publishing a report, reproduction, patch, or upstream PR). Bounty payment requires both a prior claim and an agreed price, followed by completion of the agreed task. Work started publicly before claiming, or without an agreed price, is not eligible for payment. Record the claimant in **Claim status** and the agreed bounty and scope in **Notes**.

## About

An agent-maintained issue list for Hyprland, covering the areas I am
interested in. Each entry is a self-contained dossier: audit report,
verification status per finding, and — where applicable — a compilable
reproduction client.

The existing audit material was produced by AI agents under my supervision: static review
against the relevant specs (Wayland protocol XML, color standards), live
verification in nested/headless sessions, and code reading at pinned commits.
**Treat agent-only claims as machine-generated** — each dossier states its own
verification level. Human-reviewed and tested submissions are recorded separately
under `human-confirmed/`.

## Index

| # | Area | Summary | Status | Reports | Repro clients |
|---|------|---------|--------|---------|---------------|
| 1 | HDR / color management (`cm/tonemap-ref-scaling`) | 29-item audit of `color-management-v1` handling; 2 High always-crash protocol bugs | verified (nested-session crashes observed); no upstream fix yet | [overall table](agent-report/color-management/_evidence/color-management-verification.md) | [cm-feedback-inert](agent-report/color-management/_assets/cm-feedback-inert/) · [cm-failed-desc](agent-report/color-management/_assets/cm-failed-desc/) |
| 2 | Full-codebase sweep @ `397cb0efb` — 15 areas: fork CM/HDR, IPC/hyprctl, hyprpm, config/Lua/ICC, XWayland ×2, xdg/layer-shell, misc protocols, seat/clipboard/DnD, desktop lifecycle, resource loading, boot/crash chain, input/keybinds, permission dialogs, event-loop timers | **135 findings / 15 dossiers**; top: virtual-keyboard → exec-keybind **RCE from any Wayland client** (F-INP-01), clipboard/DnD missing serial+focus checks, X11 socket 0666 w/o `-auth` (cross-user, pending live test), SIGBUS crashes (shm/vkbd keymap), IPC Lua eval/dispatch injection, permission-system fail-open paths | **static analysis only — none reproduced yet** | [batch index](pending-evaluation/2026-10-full-audit/README.md) | — |
| 3 | Community HDR/CM validation @ official `e2ad066c9` vs local fork `3d52e94fd` | **3 confirmed mechanisms: 1 new CPU-copy cursor defect, 2 extensions of existing findings**, plus addendum HDR-COM-04 (SDR presets advertise EDID HDR headroom, PR #16525); 2 implementation limitations recorded separately; 184 community records triaged | source-confirmed; 2 shader counterexamples executed in isolated EGL; physical HDR acceptance open; fork already contains corrective source for 2 findings | [validation and exclusions](agent-report/color-management/_evidence/community-hdr-validation.md) · [per-record ledger](agent-report/color-management/_evidence/community-hdr-triage.json) | [shader numerics](agent-report/color-management/_assets/hdr-community-numerics/) |
| 4 | Side findings while fixing the fork @ official `e2ad066c9` vs fork `bd598253a` | **8 upstream defects + 1 correction**: orphaned-subsurface `get_subsurface` **SIGSEGV from any client** (SIDE-01), wrong top-level/position for subsurface nesting ≥ 3, stale hardware cursor after output CM changes, cursor-surface image description ignored, CPU cursor stride; F-SEAT-11 flip-order correction | SIDE-01..03 executed against upstream `Subcompositor.cpp` (SIGSEGV / wrong values observed); SIDE-04..06 source-confirmed; fork fixes all six | [side findings](agent-report/surface-protocols/_evidence/fork-fix-side-findings.md) | [subsurface-orphan-parent](agent-report/surface-protocols/_assets/subsurface-orphan-parent/) |

Raw agent output awaiting triage (pre-verification snapshots) is parked in
[`pending-evaluation/`](pending-evaluation/).

*(To be continued as more areas get audited.)*

## Issue list

Both confirmation columns support `none` (default / not yet validated), `confirmed` (the claim is supported), `partially` (only part of the original claim or reported path is supported; see Notes), and `rejected` (reviewed and rejected). A real bug that was subsequently fixed retains its confirmation status; `rejected` is for a rejected diagnosis, not a resolved bug. PR status is `none` (default), `PRed` (a matching upstream PR has been submitted but not merged), or `accepted` (the matching fix has been merged upstream). A Discussion, feature request, or local fork fix alone does not change PR status.

`Agent confirmed = confirmed` means the linked report contains independent source/specification, numeric, or runtime confirmation **at its pinned revision**. It does not imply current-version reproduction or physical-display acceptance. A manual `confirmed`, `partially`, or `rejected` requires an explicit human validation record for this tracker; authorship, screenshots, or an agent-run test alone do not set that column, so it currently defaults to `none`.

The original CM list has 29 entries. HDR-COM-02 is folded into CM-18; HDR-COM-03 expands a previously unnumbered minor item. The full-audit entries listed below now carry an agent verdict (`confirmed` / `partially` / `rejected`, a few `none`) from source review against upstream `e2ad066c9`, with the per-finding reports under `agent-report/<category>/`; **Manually confirmed** stays `none` until a human reviews and tests them. They include informational/control records and are not 135 confirmed bugs. Community-only records remain in the [triage ledger](agent-report/color-management/_evidence/community-hdr-triage.json). The F-CM-01..09 candidates from dossier 01 targeted the fork, which has fixed or rejected all of them; their upstream halves are tracked as SIDE-07 (F-CM-01/07) and SIDE-08 (F-CM-06) (see [`01-fork-cm-hdr.md`](pending-evaluation/2026-10-full-audit/01-fork-cm-hdr.md) for the raw dossier). The SIDE-xx entries are additional upstream defects found while fixing the fork; see the [side-findings report](agent-report/surface-protocols/_evidence/fork-fix-side-findings.md).

CM tables also include **Claim status**: `none` means unclaimed; use `claimed (@handle)` to identify the claimant once an issue is claimed.

The **Notes** column records bounty amounts, other details, and the reasons for `partially` or `rejected` verdicts. It is blank by default; a `partially` verdict must explain which part is supported and which part is not. Bounties currently apply only to CM issues.

### CM — protocol

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [CM-01 Feedback survives surface destruction and later crashes the compositor](agent-report/color-management/CM-01.md) | confirmed | none | none | none |  |
| [CM-02 Failed image descriptions remain usable and cause null dereferences](agent-report/color-management/CM-02.md) | confirmed | none | none | none |  |
| [CM-03 Image-description changes are applied before the surface commit](agent-report/color-management/CM-03.md) | confirmed | none | none | none |  |
| [CM-04–11 Protocol validation bundle: PQ min + 10000 rule (04), incomplete Windows scRGB preset luminances (05), incomplete parametric descriptions accepted (06), MaxCLL / MaxFALL not validated (07), invalid mastering-luminance bounds (08), unadvertised transfer functions accepted (09), wrong set_tf_power error code (10), no inert error on inert CM surfaces (11)](agent-report/color-management/CM-04-11.md) | confirmed | none | none | none | Bundled: all eight concern `color-management-v1` request / preset handling (mostly `protocols/ColorManagement.cpp`; 05 also touches the preset definitions) and fit one PR; priced and claimed as one bundle. |
| [CM-12 ICC description replacement omits change notifications](agent-report/color-management/CM-12.md) | confirmed | none | none | none |  |

### CM — shaders and transfer functions

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [CM-13 BT.1886 uses the inverse BT.709 curve instead of the display EOTF](agent-report/color-management/CM-13.md) | confirmed | none | none | none |  |
| [CM-14 HLG decoding omits the OOTF / system gamma](agent-report/color-management/CM-14.md) | confirmed | none | none | none |  |
| [CM-15 Tonemapping clips negative FP16 wide-gamut components](agent-report/color-management/CM-15.md) | confirmed | none | none | none |  |
| [CM-16 Generic-film primaries are in the wrong channel order](agent-report/color-management/CM-16.md) | confirmed | none | none | none |  |
| [CM-17 CIE XYZ primaries can produce singular conversion matrices](agent-report/color-management/CM-17.md) | confirmed | none | none | none |  |
| [CM-18 Client minimum luminance is ignored; parametric EXT_LINEAR encoding range is ignored](agent-report/color-management/CM-18.md) | partially | none | none | none | The original blanket claim was too broad: max/reference luminance do affect tonemapping, while client minimum luminance is omitted. The parametric EXT_LINEAR range subcase was independently reproduced (1.624 vs 203 nit). |

### CM — rendering pipeline

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [CM-19 HLG passthrough lacks HDR metadata](agent-report/color-management/CM-19.md) | confirmed | none | none | none |  |
| [CM-20 Forced HDR without usable luminance data falls back to an 80-nit peak](agent-report/color-management/CM-20.md) | confirmed | none | none | none |  |
| [CM-21 HDR mirroring uses an sRGB intermediate and loses HDR range](agent-report/color-management/CM-21.md) | confirmed | none | none | none |  |
| [CM-22 HDR passthrough does not fully check source/output compatibility](agent-report/color-management/CM-22.md) | confirmed | none | none | none |  |
| [CM-23 HDR metadata uses container primaries and substitutes unknown light levels](agent-report/color-management/CM-23.md) | confirmed | none | none | none |  |
| [CM-24 FP16 affects capture brightness on work-buffer / mirror paths](agent-report/color-management/CM-24.md) | partially | none | none | none | FP16 dependence is confirmed for work-buffer / mirror capture paths. Direct surface re-render capture retains the PQ description in both modes, so a brightness difference is not guaranteed for every screenshot path. |
| [CM-25 FP16 blur omits SDR brightness / saturation adjustments](agent-report/color-management/CM-25.md) | confirmed | none | none | none |  |
| [CM-26 Auto-HDR exit can mismatch content and mode or wait for further damage](agent-report/color-management/CM-26.md) | partially | none | none | none | The mode state changes in the same commit, not one commit late. The confirmed issue is content/mode mismatch; without new damage, the damage gate can delay the switch indefinitely. |

### CM — reference white and tonemapping

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [CM-27 Tonemap peak reference scaling disagrees with absolute-nit pixels](agent-report/color-management/CM-27.md) | confirmed | none | [PRed](https://github.com/hyprwm/Hyprland/pull/16529) | none |  |
| [CM-28 Reference white is not anchored across image descriptions](agent-report/color-management/CM-28.md) | confirmed | none | none | none |  |
| [CM-29 Output reference white is hardcoded to 203 nit](agent-report/color-management/CM-29.md) | confirmed | none | none | none |  |

CM-18, CM-24, and CM-26 use the corrected, narrower findings from the verification report. CM-27’s PR #16529 is closed without merging; it remains `PRed` within the three requested PR states.

### CM — additional community-validated mechanisms

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [HDR-COM-01 CPU-copy hardware cursor bypasses output color management](agent-report/color-management/HDR-COM-01.md) | confirmed | none | none | none |  |
| [HDR-COM-03 SDR brightness / saturation applied after PQ encoding](agent-report/color-management/HDR-COM-03.md) | confirmed | none | none | none |  |
| [HDR-COM-04 SDR presets advertise the panel's EDID HDR luminances as target headroom](agent-report/color-management/HDR-COM-04.md) | confirmed | none | [PRed](https://github.com/hyprwm/Hyprland/pull/16525) | none | PR #16525 is by a third party (open). Fork fixed in `06304f5d7`. |

### CM — side findings from fork fixes

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [SIDE-04 Hardware cursor not re-rendered when the output image description changes (soft monitor rules, auto HDR)](agent-report/color-management/SIDE-04.md) | confirmed | none | none | none |  |
| [SIDE-05 Cursor surface image description ignored by software and GPU hardware cursors](agent-report/color-management/SIDE-05.md) | confirmed | none | none | none |  |
| [SIDE-07 Interned image descriptions / primaries are never freed and looked up linearly (client-driven memory / CPU growth)](agent-report/color-management/SIDE-07.md) | confirmed | none | none | none | Upstream half of fork dossier F-CM-01/07. |
| [SIDE-08 get_preferred on an orphaned subsurface crashes in t1Parent](agent-report/color-management/SIDE-08.md) | confirmed | none | none | none | Upstream half of fork dossier F-CM-06; same crash site as SIDE-01. |

### CM — known implementation limitations

These are confirmed restrictions or explicit policies, tracked separately from defects.

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [HDR-LIM-01 Capture targets remain sRGB without HDR / wide-gamut negotiation](agent-report/color-management/HDR-LIM-01.md) | confirmed | none | none | none |  |
| [HDR-LIM-02 Incompatible direct scanout allowed by non_shader_cm=ignore](agent-report/color-management/HDR-LIM-02.md) | confirmed | none | none | none |  |

### CM — rejected diagnoses

These diagnoses were withdrawn by their reporters and are retained for traceability. They are not confirmed defects.

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [Community #14504 Red shift attributed to Hyprland CM (reporter identified hyprsunset)](agent-report/color-management/COMMUNITY-14504.md) | rejected | none | none | none | The reporter withdrew the Hyprland CM diagnosis after identifying hyprsunset as the source of the red shift. |
| [Community #15195 Elevated SDR black attributed to a renderer bug (reporter identified black-floor settings)](agent-report/color-management/COMMUNITY-15195.md) | rejected | none | none | none | The reporter withdrew the renderer-bug diagnosis after identifying the configured SDR black floor; adjusting the luminance settings resolved their case. |

### CM — ICC loader candidates

| Issue | Agent confirmed | Manually confirmed | PR status | Claim status | Notes |
|---|---|---|---|---|---|
| [F-ICC-01 ICC VCGT parsing can read outside buffer bounds](agent-report/config-lua/F-ICC-01.md) | confirmed | none | none | Singleton VCGT tables pass validation and reach out-of-bounds logging reads; deterministic SIGSEGV and useful disclosure are unproven. |
| [F-ICC-02 ICC binary reads do not handle failed size queries](agent-report/config-lua/F-ICC-02.md) | confirmed | none | none | Failed size queries become huge unsigned lengths; uncaught length_error can terminate a configured load. FIFO open may block first. |

### IPC / hyprctl

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-IPC-01 IPC Lua evaluation exposes unrestricted execution](agent-report/ipc/F-IPC-01.md) | partially | none | none | Unrestricted owner-accessible evaluation exists; Critical, remote, and cross-user privilege-escalation claims lack a reachable boundary crossing. |
| [F-IPC-02 IPC dispatch interpolates input into Lua source](agent-report/ipc/F-IPC-02.md) | partially | none | none | Lua expression evaluation is real and intentional; injection requires an unsafe trusted wrapper, which the dossier does not identify. |
| [F-IPC-03 IPC plugin loading accepts arbitrary paths](agent-report/ipc/F-IPC-03.md) | partially | none | none | Arbitrary native paths load when permission enforcement is off by default; enabled enforcement can deny or require user approval before dlopen. |
| [F-IPC-04 IPC socket peer identity / permissions rely on directory access](agent-report/ipc/F-IPC-04.md) | partially | none | none | Missing UID checks and socket chmod hold; mandatory 0700 instance directories block the claimed cross-user access through a shared parent. |
| [F-IPC-05 IPC system information exposes configuration contents](agent-report/ipc/F-IPC-05.md) | rejected | none | none | Lua configuration dumps return a fixed unsupported message; rolling logs remain accessible to socket-authorized peers, without demonstrated secret exposure. |
| [F-IPC-06 hyprctl batch delimiters are not escaped](agent-report/ipc/F-IPC-06.md) | partially | none | none | Batch text is intentionally raw; ordinary dispatch does not split semicolons. Injection requires external data inserted into explicit batch syntax. |
| [F-IPC-07 Synchronous IPC work and connection load block the event loop](agent-report/ipc/F-IPC-07.md) | partially | none | none | Synchronous work and absent aggregate peer quotas hold; a 250 ms watchdog limits ordinary Lua loops, and input is not retained as claimed. |
| [F-IPC-08 Permissive IPC request parsing](agent-report/ipc/F-IPC-08.md) | confirmed | none | none | Unknown flags and command-prefix aliases are accepted; slashes after a space preserve arguments, and no authorization bypass is demonstrated. |

### Plugin management (hyprpm)

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PM-01 Unquoted revision handling in shell commands](agent-report/hyprpm/F-PM-01.md) | confirmed | none | none | Positional revision reaches an unquoted shell command after confirmation; no --rev option exists. User-context execution, not unconditional privileged execution. |
| [F-PM-02 Repository names are not confined to the install directory](agent-report/hyprpm/F-PM-02.md) | confirmed | none | none | Repository name escapes the cache during privileged directory creation and installation; filenames and serialized state constrain writes. Root authorization remains required. |
| [F-PM-03 Header update trusts build sources and version metadata](agent-report/hyprpm/F-PM-03.md) | partially | none | none | Privileged make consumes user-writable build inputs and version fields reach shell commands; fake running-socket metadata does not drive header checkout. |
| [F-PM-04 Plugin sources lack integrity / revision enforcement](agent-report/hyprpm/F-PM-04.md) | partially | none | none | Unauthenticated transports and unsigned updates are accepted; fixed revisions receive resets, Git checks objects, and ext execution is disabled by default. |
| [F-PM-05 Recursive submodule fetching trusts repository configuration](agent-report/hyprpm/F-PM-05.md) | rejected | none | none | Recursive fetch exists, but Git blocks ext by default and confirmation precedes clone; historical Git vulnerabilities require separate version-specific evidence. |
| [F-PM-06 Plugin output paths are not validated](agent-report/hyprpm/F-PM-06.md) | confirmed | none | none | Unchecked output paths reach privileged copying; existence checks do not require read permission. Accessible metadata and root authorization are necessary. |
| [F-PM-07 Plugin load paths trust stored state](agent-report/hyprpm/F-PM-07.md) | partially | none | none | Stored load paths lack confinement; normal state is root-protected, permission and ABI gates apply, and compositor-supplied names reach unload only. |
| [F-PM-08 Header validation trusts writable package metadata](agent-report/hyprpm/F-PM-08.md) | rejected | none | none | Compatibility checks are not authenticity checks, but headers remain root-owned and appended package paths do not normally override the managed package. |
| [F-PM-09 File-operation races in plugin management](agent-report/hyprpm/F-PM-09.md) | partially | none | none | Fixed work/state paths permit same-user concurrency races; normal private runtime directories block cross-user placement. Socket environment trust is not a race. |
| [F-PM-10 Informational: plugin-manager design concerns](agent-report/hyprpm/F-PM-10.md) | partially | none | none | Build trust and permissive validators exist; deprecated notification calls a null manager, but stateless notify does not establish the claimed deterministic crash. |

### Configuration / Lua

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-CFG-01 Monitor-rule integer conversion can throw](agent-report/config-lua/F-CFG-01.md) | rejected | none | none | Throwing conversions remain, but have no upstream callers; Lua uses bounded integer fields and direct assignment. |
| [F-RULE-01 Invalid regular expressions reach window-rule matching](agent-report/config-lua/F-RULE-01.md) | partially | none | none | Missing regex validation and inverted failure match hold; debug-library abort is unsupported, and installed RE2 reports errors then returns false. |

### XWayland property parsing

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-XW-01 XWayland property parsing does not validate element format](agent-report/xwayland/F-XW-01.md) | confirmed | none | none | Three atom-list readers lack format validation and can overread replies; compositor crash reliability remains untested. |

### Shell / surface protocols

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PROTO-01 Self-parented popup can loop while accumulating offsets](agent-report/surface-protocols/F-PROTO-01.md) | partially | none | none | Self-parenting is accepted, but the proposed hang chain lacks a desktop popup; duplicate ownership can instead abort, covered by F-PROTO-07. |
| [F-PROTO-02 Popup grab / reposition uses expired surface references](agent-report/surface-protocols/F-PROTO-02.md) | confirmed | none | none | Destroy-before-role leaves a live popup with expired content; grab dereferences it. Reposition also lacks a content-lifetime guard. [EXECUTED nested SIGABRT (+real-session crash)] |
| [F-PROTO-03 Popup grab does not validate ownership](agent-report/surface-protocols/F-PROTO-03.md) | partially | none | none | Cross-client grab allowlisting is confirmed; passive keylogging and permanent grab retention are not. Input delivery still needs hit-testing and focus. |
| [F-PROTO-04 Unbounded per-surface commit / fence queues](agent-report/surface-protocols/F-PROTO-04.md) | confirmed | none | none | A valid unsignaled fence retains an unbounded state queue; scans amplify work. Raw eventfd is not a dma-buf fence. [EXECUTED real session: unsignaled exported timeline rejected at import, no queue growth; valid-GPU-fence path not exercised] |
| [F-PROTO-05 Unchecked layer-shell geometry arithmetic](agent-report/surface-protocols/F-PROTO-05.md) | partially | none | none | Margin sums use doubles, rejecting the cited overflow site; applyExclusive converts margins to integers and performs unchecked signed addition. |
| [F-PROTO-06 Toplevel parent traversal can overflow or use expired references](agent-report/surface-protocols/F-PROTO-06.md) | partially | none | none | Unbounded recursive descendant traversal can exhaust the stack; destructor unlinking defeats the claimed expired-child UAF path. |
| [F-PROTO-07 Duplicate surface-role creation is not rejected](agent-report/surface-protocols/F-PROTO-07.md) | partially | none | none | Role checks are absent, but duplicate toplevel creation aborts on CWLSurface ownership before unbounded window accumulation. |
| [F-PROTO-08 Unbounded region / layer configure bookkeeping](agent-report/surface-protocols/F-PROTO-08.md) | partially | none | none | Complex regions and outstanding layer serials are unbounded; identical rectangles coalesce, and unchanged mapped sizes do not always produce configure events. |
| [F-PROTO-09 Weak configure-ack serial validation](agent-report/surface-protocols/F-PROTO-09.md) | confirmed | none | none | Unsent and repeated serials are accepted; numeric ordering poisons later acknowledgments and mishandles serial wraparound. |
| [F-PROTO-10 Resource type and layer-popup null checks depend on external guards](agent-report/surface-protocols/F-PROTO-10.md) | rejected | none | none | Typed nonnullable arguments and resource lifetime prevent the alleged confusion/null input; libwayland older than 1.21 is outside supported dependencies. |
| [SIDE-01 get_subsurface on an orphaned subsurface parent dereferences null in t1Parent (compositor SIGSEGV)](agent-report/surface-protocols/SIDE-01.md) | confirmed | none | none | Repro: [subsurface-orphan-parent](agent-report/surface-protocols/_assets/subsurface-orphan-parent/) |
| [SIDE-02 t1Parent returns the wrong ancestor for subsurface nesting depth ≥ 3](agent-report/surface-protocols/SIDE-02.md) | confirmed | none | none |  |
| [SIDE-03 posRelativeToParent mis-positions subsurfaces at nesting depth ≥ 3](agent-report/surface-protocols/SIDE-03.md) | confirmed | none | none |  |

### Capture / shared memory / input protocols

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PR2-01 Virtual-keyboard keymap backing-file validation](agent-report/capture-shm/F-PR2-01.md) | confirmed | none | none | Unbounded string parsing of client mappings persists; the one-byte example is invalid and sandboxed clients cannot bind. |
| [F-PR2-02 Input-method context and focus ownership checks](agent-report/capture-shm/F-PR2-02.md) | partially | none | none | Unsandboxed first-IME trust and ignored serial hold; injection requires a focused text-input, and password disclosure depends on application behavior. |
| [F-PR2-03 DMA-BUF plane count / backing-size validation](agent-report/capture-shm/F-PR2-03.md) | partially | none | none | Backing-size validation is absent; plane continuity and import-failure guards exist; GPU hangs and cross-client reads are unproven. |
| [F-PR2-04 Shared-memory backing-file lifetime and mapping failure handling](agent-report/capture-shm/F-PR2-04.md) | confirmed | none | none | Mutable SHM backing and destructive remap remain unsafe; creation bounds checks do not protect later access. [EXECUTED nested SIGBUS] |
| [F-PR2-05 Screencopy region geometry and allocation bounds](agent-report/capture-shm/F-PR2-05.md) | partially | none | none | Region clipping is missing and allocation uses requested dimensions; claimed INT32_MAX SHM/OOM recipe and signed-overflow mechanism are unproven. |
| [F-PR2-06 Screen capture during session-lock transition](agent-report/capture-shm/F-PR2-06.md) | partially | none | none | No capture lock gate; transition and retained snapshots need policy. Mirror includes lock rendering, contradicting unconditional frozen-desktop disclosure. |
| [F-PR2-07 Text-input-v1 deletion arithmetic and purpose conversion](agent-report/capture-shm/F-PR2-07.md) | partially | none | none | Purpose uses hint incorrectly; deletion conversion has real semantic/bounds issues, but signed negative indices and client-provided v1 serials are legitimate. |
| [F-PR2-08 DRM lease authorization during session lock / output use](agent-report/capture-shm/F-PR2-08.md) | rejected | none | none | Only offered non-desktop outputs are leaseable; DPMS-off desktop connectors are not offered. Missing lock gate alone does not prove desktop takeover. |
| [F-PR2-09 Unbounded protocol-object and pending-frame allocation](agent-report/capture-shm/F-PR2-09.md) | partially | none | none | Object quotas are absent and non-desktop inhibitors affect idle policy; completed pending frames are pruned, and event delivery is guarded. |
| [F-PR2-10 Gamma / CTM changes during session lock](agent-report/capture-shm/F-PR2-10.md) | confirmed | none | none | Unsandboxed owners can change output color state during lock; gamma support, VCGT and single-controller guards limit reachability; physical blackout untested. |

### XWayland lifecycle and integration

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-XW2-01 X11 socket access without explicit Xauthority: peer-policy validation pending](agent-report/xwayland/F-XW2-01.md) | none | none | none | inconclusive — Socket permissions and missing auth are confirmed; default cross-UID admission depends on Xwayland peer ACLs, not established here. |
| [F-XW2-02 WL_SURFACE_ID resource-type and lifetime validation](agent-report/xwayland/F-XW2-02.md) | partially | none | none | Unchecked resource cast remains; duplicate-surface guard defeats the claimed theft of an already-associated surface. |
| [F-XW2-03 X selection property format validation](agent-report/xwayland/F-XW2-03.md) | confirmed | none | none | TARGETS import treats element counts as atom counts without checking format, permitting an out-of-bounds read. |
| [F-XW2-04 Transient-parent property format / length validation](agent-report/xwayland/F-XW2-04.md) | partially | none | none | Zero-length transient property permits an out-of-bounds read; arbitrary parent selection already works with valid properties. |
| [F-XW2-05 X selection forwarding lacks throttling / lock gating](agent-report/xwayland/F-XW2-05.md) | partially | none | none | Ungated selection replacement is reachable while locked after X focus; default activation theft and automatic command execution are unsupported. |
| [F-XW2-06 Client-supplied X11 identity can match privileged window rules](agent-report/xwayland/F-XW2-06.md) | rejected | none | none | Class/title are intentionally client-provided matching metadata; no authenticated identity or privilege boundary is demonstrated. |
| [F-XW2-07 Xdnd completion messages do not validate sender identity](agent-report/xwayland/F-XW2-07.md) | partially | none | none | Completion forwarding lacks destination/phase checks; Wayland notification is reachable, but file deletion and the proposed field check are unsupported. |
| [F-XW2-08 XWayland restart cleanup can use a reset window manager](agent-report/xwayland/F-XW2-08.md) | rejected | none | none | Hyprutils reset retains access during destruction; the connection outlives transfers. Persistent wl_client leakage is not demonstrated. |
| [F-XW2-09 Unbounded X11 window bookkeeping and synchronous queries](agent-report/xwayland/F-XW2-09.md) | confirmed | none | none | Unmapped top-level windows allocate persistent compositor state and synchronous reply waits; no event-drain budget. Runtime starvation magnitude remains unmeasured. |
| [F-XW2-10 Global X11 lock-file availability / ownership concerns](agent-report/xwayland/F-XW2-10.md) | partially | none | none | Global lock-name exhaustion can prevent startup across 33 displays; cross-user socket replacement and MITM are not established. |
| [F-XW2-11 Informational: additional XWayland robustness concerns](agent-report/xwayland/F-XW2-11.md) | partially | none | none | Startup failure lacks explicit retry; zombie accumulation is prevented. PATH and CurrentTime behavior do not establish escalation or unauthorized disclosure. |

### Seat / clipboard / drag and drop

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-SEAT-01 set_selection 不校验请求客户端键盘焦点、serial 完全忽略 → 后台客户端剪贴板/中键粘贴劫持](agent-report/seat-clipboard/F-SEAT-01.md) | confirmed | none | none | Background selection replacement confirmed; this does not itself disclose the previous clipboard contents. |
| [F-SEAT-02 wl_data_device.start_drag 无 serial/按钮/焦点校验 → 无按钮事件的注入式拖放](agent-report/seat-clipboard/F-SEAT-02.md) | confirmed | none | none | Drag activation ignores implicit-grab serial and origin focus; downstream drop still depends on input and target behavior. |
| [F-SEAT-03 拖放结束依赖 target 客户端自愿 `offer.finish`，无超时/取消兜底 → dndActive 永久为真，全局丢弃按钮事件](agent-report/seat-clipboard/F-SEAT-03.md) | confirmed | none | none | Post-drop source retention keeps dndActive true and suppresses client pointer buttons until explicit cleanup. |
| [F-SEAT-04 wl_data_offer.receive 无一次性约束、无速率限制、core 路径未 accept 自动代接受 → 对 source 客户端的 fd/事件洪泛 DoS](agent-report/seat-clipboard/F-SEAT-04.md) | partially | none | none | Repeated forwarding exists; repeat receives are legal, MIME validation exists, and victim exhaustion is unproven. |
| [F-SEAT-05 OutputManagement set_custom_mode 无尺寸/刷新率上限，test 为 stub 恒真 → 巨幅 framebuffer OOM / 整数溢出](agent-report/seat-clipboard/F-SEAT-05.md) | partially | none | none | Test is a success stub and dimensions reach swapchain setup; backend checks exist, so OOM and overflow remain unverified. |
| [F-SEAT-06 OutputManagement 无会话锁/权限门控 → 锁屏期间任意客户端禁用或重配所有显示器](agent-report/seat-clipboard/F-SEAT-06.md) | partially | none | none | No lock gate for unsandboxed output-management clients; sandbox visibility is restricted, and authentication bypass is unproven. |
| [F-SEAT-07 create_configuration 不校验 done-serial、不限制并发配置、stop 后仍可操作](agent-report/seat-clipboard/F-SEAT-07.md) | partially | none | none | Stale serial and stopped-manager requests lack checks; concurrent objects alone are not a protocol violation or demonstrated DoS. |
| [F-SEAT-08 set_mode 不校验 mode 与 head 的归属 → 跨 head 模式注入](agent-report/seat-clipboard/F-SEAT-08.md) | confirmed | none | none | Mode-to-head ownership is unchecked; foreign same-client mode dimensions reach the other head’s rule, with backend testing still present. |
| [F-SEAT-09 set_position 不校验布局合法性（重叠/越界）→ 监视器布局破坏](agent-report/seat-clipboard/F-SEAT-09.md) | partially | none | none | Overlap prohibition and signed-16-bit limits are unsupported; extreme accepted coordinates can exceed integer extents during automatic arrangement. |
| [F-SEAT-10 pointer.set_cursor 的 serial 校验被注释禁用 → 光标图像注入无输入合法性约束](agent-report/seat-clipboard/F-SEAT-10.md) | partially | none | none | Serial validation is disabled, but current client focus is enforced; focus-loss cursor injection is unsupported. |
| [F-SEAT-11 光标 shm 像素数据拷贝无尺寸/格式校验；CPU 光标路径每帧原地翻转角色缓冲](agent-report/seat-clipboard/F-SEAT-11.md) | partially | none | none | C8/RGB565 explanation rejected; CPU cache mutation confirmed, with an unchecked padding tail in channel swapping. |
| [SIDE-06 CPU cursor copy assumes the dumb buffer stride and size](agent-report/input/SIDE-06.md) | confirmed | none | none |  |
| [F-SEAT-12 触摸焦点记账是"单槽"模型：per-finger 状态共享一个槽位，合成 up 依赖会被覆盖的监听器](agent-report/seat-clipboard/F-SEAT-12.md) | partially | none | none | Shared touch focus and destroy listeners can misroute contacts; physical multi-touch is required, and permanent lock underflow is unproven. |
| [F-SEAT-13 wl_data_device_manager.get_data_device 完全不使用 seat 参数；数据面无需 seat 即可建立](agent-report/seat-clipboard/F-SEAT-13.md) | rejected | none | none | Seat is required by typed wire validation; ignored seat storage is single-seat routing, while privileged clipboard-manager delivery is intentional. |
| [F-SEAT-14 serial 存储记账缺陷：enterSerial 永不消费、serials 上限 100 可被挤掉 → serial 重放](agent-report/seat-clipboard/F-SEAT-14.md) | partially | none | none | Latest enter serial bypasses consumption for repeated token creation; normal serials are consumed, motion adds none, and move/resize use stronger validation. |
| [F-SEAT-15 X11↔Wayland 剪贴板桥与粘贴大小上限不在本域；本 fork 无 ClipboardManager](agent-report/seat-clipboard/F-SEAT-15.md) | confirmed | none | none | Pure Wayland selection transfer forwards file descriptors without caching payload; XWayland buffering remains a separate audit boundary. |

### Desktop / group / workspace lifecycle

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-DSK-01 Destroyed windows remain in groups as expired references](agent-report/desktop-lifecycle/F-DSK-01.md) | none | none | none | inconclusive — Destroy lacks fallback group cleanup, but ordinary role/X11 teardown unmaps first; no client-reachable bypass established. |
| [F-DSK-02 Group destruction can loop on expired members](agent-report/desktop-lifecycle/F-DSK-02.md) | partially | none | none | Non-progress loop exists for an already expired first member with multiple entries; a reachable stale-member producer remains unproved. |
| [F-DSK-03 Swallow matching can cross workspace / process trust boundaries](agent-report/desktop-lifecycle/F-DSK-03.md) | partially | none | none | Cross-workspace ancestry matching is real and opt-in; legitimate descendant matching does not establish a process trust-boundary bypass. |
| [F-DSK-04 Swallow restore / toggle uses invalid workspace state](agent-report/desktop-lifecycle/F-DSK-04.md) | rejected | none | none | Unmap clears swallow relations before workspace reset; destruction fallback locks endpoints and guards workspace access, defeating the claimed stale-state path. |
| [F-DSK-05 Backend listeners can outlive their window](agent-report/desktop-lifecycle/F-DSK-05.md) | rejected | none | none | Listeners are RAII handles; backend ownership and automatic member destruction prevent the claimed post-window callback. |
| [F-DSK-06 Expired focus-history references remain reachable](agent-report/desktop-lifecycle/F-DSK-06.md) | partially | none | none | Workspace GC retains an extra first-monitor record; window GC removes deleted references, and consumers safely use saved identity or checked locks. |
| [F-DSK-07 Master-layout empty / zero-size arithmetic](agent-report/desktop-lifecycle/F-DSK-07.md) | partially | none | none | Empty-cycle and single-window claims fail guards; zero work-area smart resizing can produce NaN, without demonstrated crash or allocation exhaustion. |
| [F-DSK-08 Scrolling-layout index and size arithmetic](agent-report/desktop-lifecycle/F-DSK-08.md) | partially | none | none | Empty next() is unsafe in isolation; guarded insertion callers and membership-based lookup leave the proposed client-triggered bounds violation unproved. |
| [F-DSK-09 Scrolling fullscreen bookkeeping retains invalid targets](agent-report/desktop-lifecycle/F-DSK-09.md) | rejected | none | none | Expired keys are erased by iterator; weak-pointer identity hashing prevents null from matching arbitrary expired entries. |
| [F-DSK-10 Duplicate layout targets / stale layout state](agent-report/desktop-lifecycle/F-DSK-10.md) | rejected | none | none | Assignment guards and remove-before-move prevent the proposed duplicates; Space removal erases all matches and monocle skips dead targets. |
| [F-DSK-11 Negative work-area geometry reaches layouts](agent-report/desktop-lifecycle/F-DSK-11.md) | confirmed | none | none | Configured reservations/gaps can produce zero or negative work areas; geometry propagation is confirmed, while compositor crash and output-protocol abuse remain unverified. |
| [F-DSK-12 Pinned-window focus can use a missing monitor](agent-report/desktop-lifecycle/F-DSK-12.md) | none | none | none | inconclusive — Pinned focus lacks a monitor guard; startup mapping is guarded and client self-pinning is unsupported. Monitor-removal event ordering still needs validation. |
| [F-DSK-13 Deferred window-map work can target a destroying window](agent-report/desktop-lifecycle/F-DSK-13.md) | rejected | none | none | Weak validity differs during destruction, but idle execution cannot overlap synchronous window destruction on the compositor event loop. |
| [F-DSK-14 Group initialization / replacement boundary assumptions](agent-report/desktop-lifecycle/F-DSK-14.md) | rejected | none | none | All group creators supply a live singleton; replacement validates both windows and identity. Expired-slot claims require the unproven F-DSK-01 state. |

### Resource loading

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-RES-01 PNG decoding lacks decoded-size limits](agent-report/resource-loading/F-RES-01.md) | confirmed | none | none | Locked decoder lacks byte budget and Cairo status check; selected local assets, not client filenames, reach it. |
| [F-RES-02 BMP parser arithmetic and buffer bounds](agent-report/resource-loading/F-RES-02.md) | partially | none | none | Wrong 32-bpp destination and leaks hold; claimed 24-bpp conversion/allocation size mismatch does not. |
| [F-RES-03 JPEG empty-input and error-path handling](agent-report/resource-loading/F-RES-03.md) | partially | none | none | Cleanup and MIME lifetime defects hold; MIME detection blocks stable empty files, and native decoding is serialized. |
| [F-RES-04 WebP empty-input and error-path handling](agent-report/resource-loading/F-RES-04.md) | partially | none | none | Cairo failure leak and MIME lifetime hold; empty-file reachability and second full decoder-buffer leak do not. |
| [F-RES-05 AVIF resource cleanup and allocation checks](agent-report/resource-loading/F-RES-05.md) | partially | none | none | Cleanup and Cairo status defects hold; optional AVIF support and libheif limits constrain the claimed bomb. |
| [F-RES-06 Async resource loading can lose a wakeup](agent-report/resource-loading/F-RES-06.md) | confirmed | none | none | Completion before waiter registration can strand synchronous overlay rendering; completion fields also race. |
| [F-RES-07 Shared-memory image backing can produce SIGBUS](agent-report/resource-loading/F-RES-07.md) | confirmed | none | none | Mutable SHM backing is read without fault containment; initial size/layout checks do not prevent later SIGBUS. [EXECUTED nested SIGBUS] |
| [F-RES-08 Cursor theme names / index files are not confined](agent-report/resource-loading/F-RES-08.md) | partially | none | none | Unconfined inherited names and rescans hold; the equals-sign guard rejects the alleged prefixed-key spoofing. |
| [F-RES-09 Cursor loading symlink and retry behavior](agent-report/resource-loading/F-RES-09.md) | partially | none | none | Symlinks and unrewound retry exist; no privilege escalation, universal EOF failure, or applicable libXcursor overflow is established. |
| [F-RES-10 Unreachable ICC upload path retains file descriptors](agent-report/resource-loading/F-RES-10.md) | partially | none | none | Descriptor leaks hold and debug mode makes them reachable; default rejection and absence of client ICC parsing still hold. |
| [F-RES-11 SVG external references and error-path cleanup](agent-report/resource-loading/F-RES-11.md) | partially | none | none | Explicitly sized SVG failures leak; native zero-size guard and librsvg policy break claimed file/network/XXE reachability. |
| [F-RES-12 Control record: resource-loading negative results (not a defect)](agent-report/resource-loading/F-RES-12.md) | partially | none | none | Plain-text titles and fixed content-sniffed assets hold; complete SHM arithmetic assurance, unconditional ICC unreachability, and ownership assumptions do not. |

### Startup / crash reporting

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-BOOT-01 Crash-report path creation lacks exclusive / symlink protections](agent-report/startup-crash/F-BOOT-01.md) | confirmed | none | none | Unsafe report creation is real; exploitation requires an attacker-writable report namespace and a crash. Default private cache directories block other users. |
| [F-BOOT-02 Crash signal handler invokes PATH-resolved shell commands](agent-report/startup-crash/F-BOOT-02.md) | confirmed | none | none | Crash reporting invokes shell helpers and interpolates the executable path; environment/path control is required. Independent client-to-code-execution escalation is unproved. |
| [F-BOOT-03 Executable lookup trusts PATH entries and the working directory](agent-report/startup-crash/F-BOOT-03.md) | partially | none | none | Executable detection is wrong; PATH execution is intentional. Both splitters remove empty components, contradicting the claimed lookup-to-current-directory mechanism. |
| [F-BOOT-04 Runtime-directory validation is too weak](agent-report/startup-crash/F-BOOT-04.md) | partially | none | none | Unsafe runtime roots are accepted, but private instance directories block direct cross-user reads and unlinking. Parent-directory replacement remains a conditional namespace-integrity problem. |
| [F-BOOT-05 Environment-toggle parsing accepts overly broad values](agent-report/startup-crash/F-BOOT-05.md) | rejected | none | none | Nonempty values except exactly zero intentionally enable these switches. Setting another process’s launch environment is an unmet prerequisite, not a demonstrated security bypass. |
| [F-BOOT-06 Systemd notification socket validation / format support](agent-report/startup-crash/F-BOOT-06.md) | partially | none | none | Length and abstract-address handling are defective; environment-selected notification routing is intentional, and the alleged fd-prefix compatibility requirement is unsupported. |
| [F-BOOT-07 Data-file writes have path and replacement races](agent-report/startup-crash/F-BOOT-07.md) | partially | none | none | Symlink-following truncation and directory races remain conditional on writable data storage. Donation startup guards missing data home; deterministic nullopt crash is overstated. |
| [F-BOOT-08 Session watchdog robustness](agent-report/startup-crash/F-BOOT-08.md) | partially | none | none | Parent allocations leak and initialized crashes restart without backoff. Early failures stop; the claimed multithreaded-fork deadlock and untrusted child-protocol boundary are unproved. |
| [F-BOOT-09 Core-dump visibility depends on system policy](agent-report/startup-crash/F-BOOT-09.md) | none | none | none | inconclusive — Abort can request a core, but production limits, collection policy, contents, and other-user readability are unverified. Directory permissions alone do not establish disclosure. |
| [F-BOOT-10 Informational: startup / crash handling notes](agent-report/startup-crash/F-BOOT-10.md) | partially | none | none | Missing HOME breaks tilde expansion; empty-string UB is false. Overlay retention is source-supported with weak keys; environment propagation and packaging notes are informational. |

### Input devices / keybinds

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-INP-01 Virtual keyboard events can reach execution keybinds](agent-report/input/F-INP-01.md) | partially | none | none | Unrestricted clients reach configured binds; security-context sandbox clients are filtered. Arbitrary command execution and universal sandbox escape are not established. |
| [F-INP-02 Release callbacks bypass keybind availability checks](agent-report/input/F-INP-02.md) | confirmed | none | none | Armed release callbacks bypass lock checks; drag release stops dragging, rather than starting locked-state movement. Other callback effects depend on configuration. |
| [F-INP-03 Touch-device destruction listener ordering](agent-report/input/F-INP-03.md) | confirmed | none | none | Touch destruction can drop the last owner during emission, then reset a member of the freed wrapper; physical/backend removal required. |
| [F-INP-04 Keyboard listeners use unchecked weak references](agent-report/input/F-INP-04.md) | rejected | none | none | Missing null checks are real, but owner-scoped synchronous signals and strong locals break the alleged late-event crash path. |
| [F-INP-05 Shared keyboard pressed-key state can remain stuck](agent-report/input/F-INP-05.md) | partially | none | none | Held shared keys suppress physical presses/releases while the device persists; removal leaves a recoverable stale key, not permanent keyboard loss. |
| [F-INP-06 Virtual-pointer access and session-lock checks](agent-report/input/F-INP-06.md) | partially | none | none | Unrestricted pointer injection and prearmed kill-mode lock gap hold; sandbox clients are filtered, normal binds remain gated, Escape cancels kill mode. |
| [F-INP-07 Release callback recursion and device-removal reentrancy](agent-report/input/F-INP-07.md) | rejected | none | none | Synthetic release recurses once with pressed=false; extracted callbacks own binds and are removed before invocation. No claimed unbounded recursion demonstrated. |
| [F-INP-08 CVirtualKeyboardV1Resource::destroy 双通道触发：destroy 事件重复发射、destroyKeyboard 重入](agent-report/input/F-INP-08.md) | partially | none | none | Protocol destruction emits twice, but normal teardown destroys the device listener and drains callbacks; repeated InputManager teardown is not established. |
| [F-INP-09 Keybinds::CResolver 键码缓存以裸指针为键，热插拔指针复用返回过期 keycode；resolveKeycode 数字解析未防溢出](agent-report/input/F-INP-09.md) | partially | none | none | Cache and numeric conversion defects affect configured shortcut forwarding; ordinary binding matching uses different parsing. No client-supplied binding-string path established. |
| [F-INP-10 Workspace-creation command rules accept client-provided names](agent-report/input/F-INP-10.md) | rejected | none | none | Empty-workspace commands exist, but ext-workspace advertises no creation capability and implements no create-workspace request handler. |

### Permissions / dialogs / tokens

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PERM-01 Permission decisions trust dialog output and executable lookup](agent-report/permissions/F-PERM-01.md) | confirmed | none | none | Dialog stdout controls decisions and malformed completion leaves PENDING; executable substitution additionally requires control of the compositor’s helper search path. |
| [F-PERM-02 Spawned applications inherit non-CLOEXEC file descriptors](agent-report/permissions/F-PERM-02.md) | partially | none | none | Dialog pipe inheritance is real and spawn lacks descriptor cleanup; Xwayland sockets use CLOEXEC and its notify writer is scoped to startup. |
| [F-PERM-03 Dialog privileges are keyed only by process ID](agent-report/permissions/F-PERM-03.md) | partially | none | none | PID-only recognition exists; live PID collision is impossible, X11 focus is excluded, and claimed guaranteed lock-screen takeover is unproved. |
| [F-PERM-04 Permission cache does not follow process lifetime](agent-report/permissions/F-PERM-04.md) | confirmed | none | none | Plugin permission cache uses requester PID, path and type without process-start identity or death cleanup; keyboard calls use a sentinel, not process PID. |
| [F-PERM-05 Informational: enforcement is disabled by default](agent-report/permissions/F-PERM-05.md) | confirmed | none | none | Enforcement defaults off and both permission APIs immediately allow; this is an explicit default policy, not an independent privilege-escalation defect. |
| [F-TOK-01 Control record: token-manager checks and low-risk notes](agent-report/permissions/F-TOK-01.md) | partially | none | none | UUID generation and lazy expiry hold; activation consumes its authorization entry, while retained live token resources can outgrow the delivery-list cap. |
| [F-RULE-02 Execution-token rules trust client-supplied environment](agent-report/permissions/F-RULE-02.md) | rejected | none | none | Launch tokens are random per spawn and rules are consumed on mapping; the alleged plaintext configuration secret and arbitrary self-declaration are absent. |
| [F-RULE-03 Execution-token matching performs synchronous process-environment reads](agent-report/permissions/F-RULE-03.md) | confirmed | none | none | Eligible execution-token rules reread process environments synchronously during mapping; no cache exists, but latency and denial-of-service magnitude remain unmeasured. |
| [F-PERM-06 Permission-dialog text is not escaped](agent-report/permissions/F-PERM-06.md) | partially | none | none | Unescaped variable substitution into markup-bearing text is confirmed; visual phishing depends on the external renderer, and virtual-keyboard names are derived rather than arbitrary. |
| [F-PERM-07 Missing / invalid dialog results can allow access](agent-report/permissions/F-PERM-07.md) | partially | none | none | Helper lookup weaknesses hold, but a null dialog is dereferenced before fallback: conditional compositor crash, not allow-all; startup also warns. |

### Event-loop timers

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-TMR-01 Screenshare stop timer is not unregistered](agent-report/eventloop-timers/F-TMR-01.md) | rejected | none | none | Ownerless timers are skipped and garbage-collected; session destruction cannot produce the claimed permanent timer leak or late callback. |

## Repository structure and contribution requirements

- **`pending-evaluation/`** keeps the existing raw audit snapshots and category layout. These are candidates awaiting evaluation; moving or linking a candidate does not confirm it.
- **`agent-report/<category>/<issue-id>.md`** contains one Markdown report per evaluated issue, including automated analysis, reproduction results, tested revisions, limitations, and links to supporting artifacts. Existing shared source reports are preserved under each category's `_evidence/`; agent-run code and results live under `_assets/`. These supporting directories do not confer human confirmation.
- **`human-confirmed/<category>/<issue-id>/`** contains the human-reviewed report and tested reproduction code, patches, logs, or other evidence for one issue. AI-generated material is allowed **only after a human has reviewed it, confirmed it is correct, and tested it**. Record the reviewer, review date, exact revision/environment, test procedure, results, and remaining limitations. An agent-run test alone does not qualify.

All existing reproduction artifacts remain under `agent-report/` because this tracker has no explicit human validation records yet. `human-confirmed/` currently contains requirements only; add an issue directory after the human review and testing requirements are met. Both confirmation columns continue to use `none`, `confirmed`, `partially`, or `rejected`; explain partial or rejected findings in **Notes** and the report.

Existing executable reproductions retain their own build instructions in their `_assets/` directories. Run crash reproductions only in an isolated nested/headless session with no unsaved data. See the linked issue report for its actual verification scope. Keep sensitive crash reproductions private until the corresponding fixes are merged upstream.

## Methodology notes

- Findings are pinned to an exact branch + commit; line numbers refer to that tree.
- Severity was assigned by reachability (any client → whole compositor = High).
- Where a claim could not be independently confirmed, it is marked as such in the dossier.

## Disclosure

When reporting upstream, follow Hyprland's
[AI usage policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md)
and the [issue guidelines](https://wiki.hypr.land/contributing-and-debugging/issue-guidelines/):
all interaction with the development team must be done by a human, not by an
agent.
