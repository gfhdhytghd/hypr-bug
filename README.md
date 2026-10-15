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

The original CM list has 29 entries. HDR-COM-02 is folded into CM-18; HDR-COM-03 expands a previously unnumbered minor item. The full-audit entries listed below remain unverified candidates (`none`), including informational/control records; they are not 135 confirmed bugs. Community-only records remain in the [triage ledger](agent-report/color-management/_evidence/community-hdr-triage.json). The F-CM-01..09 candidates from dossier 01 targeted the fork, which has fixed or rejected all of them; their upstream halves are tracked as SIDE-07 (F-CM-01/07) and SIDE-08 (F-CM-06) (see [`01-fork-cm-hdr.md`](pending-evaluation/2026-10-full-audit/01-fork-cm-hdr.md) for the raw dossier). The SIDE-xx entries are additional upstream defects found while fixing the fork; see the [side-findings report](agent-report/surface-protocols/_evidence/fork-fix-side-findings.md).

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
| [F-ICC-01 ICC VCGT parsing can read outside buffer bounds](pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md) | none | none | none | none |  |
| [F-ICC-02 ICC binary reads do not handle failed size queries](pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md) | none | none | none | none |  |

### IPC / hyprctl

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-IPC-01 IPC Lua evaluation exposes unrestricted execution](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-02 IPC dispatch interpolates input into Lua source](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-03 IPC plugin loading accepts arbitrary paths](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-04 IPC socket peer identity / permissions rely on directory access](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-05 IPC system information exposes configuration contents](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-06 hyprctl batch delimiters are not escaped](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-07 Synchronous IPC work and connection load block the event loop](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |
| [F-IPC-08 Permissive IPC request parsing](pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md) | none | none | none |  |

### Plugin management (hyprpm)

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PM-01 Unquoted revision handling in shell commands](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-02 Repository names are not confined to the install directory](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-03 Header update trusts build sources and version metadata](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-04 Plugin sources lack integrity / revision enforcement](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-05 Recursive submodule fetching trusts repository configuration](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-06 Plugin output paths are not validated](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-07 Plugin load paths trust stored state](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-08 Header validation trusts writable package metadata](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-09 File-operation races in plugin management](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |
| [F-PM-10 Informational: plugin-manager design concerns](pending-evaluation/2026-10-full-audit/03-hyprpm.md) | none | none | none |  |

### Configuration / Lua

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-CFG-01 Monitor-rule integer conversion can throw](pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md) | none | none | none |  |
| [F-RULE-01 Invalid regular expressions reach window-rule matching](pending-evaluation/2026-10-full-audit/04-config-lua-helpers.md) | none | none | none |  |

### XWayland property parsing

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-XW-01 XWayland property parsing does not validate element format](pending-evaluation/2026-10-full-audit/05-xwayland.md) | none | none | none |  |

### Shell / surface protocols

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PROTO-01 Self-parented popup can loop while accumulating offsets](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-02 Popup grab / reposition uses expired surface references](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-03 Popup grab does not validate ownership](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-04 Unbounded per-surface commit / fence queues](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-05 Unchecked layer-shell geometry arithmetic](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-06 Toplevel parent traversal can overflow or use expired references](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-07 Duplicate surface-role creation is not rejected](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-08 Unbounded region / layer configure bookkeeping](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-09 Weak configure-ack serial validation](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [F-PROTO-10 Resource type and layer-popup null checks depend on external guards](pending-evaluation/2026-10-full-audit/06-protocols-shell.md) | none | none | none |  |
| [SIDE-01 get_subsurface on an orphaned subsurface parent dereferences null in t1Parent (compositor SIGSEGV)](agent-report/surface-protocols/SIDE-01.md) | confirmed | none | none | Repro: [subsurface-orphan-parent](agent-report/surface-protocols/_assets/subsurface-orphan-parent/) |
| [SIDE-02 t1Parent returns the wrong ancestor for subsurface nesting depth ≥ 3](agent-report/surface-protocols/SIDE-02.md) | confirmed | none | none |  |
| [SIDE-03 posRelativeToParent mis-positions subsurfaces at nesting depth ≥ 3](agent-report/surface-protocols/SIDE-03.md) | confirmed | none | none |  |

### Capture / shared memory / input protocols

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PR2-01 Virtual-keyboard keymap backing-file validation](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-02 Input-method context and focus ownership checks](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-03 DMA-BUF plane count / backing-size validation](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-04 Shared-memory backing-file lifetime and mapping failure handling](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-05 Screencopy region geometry and allocation bounds](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-06 Screen capture during session-lock transition](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-07 Text-input-v1 deletion arithmetic and purpose conversion](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-08 DRM lease authorization during session lock / output use](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-09 Unbounded protocol-object and pending-frame allocation](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |
| [F-PR2-10 Gamma / CTM changes during session lock](pending-evaluation/2026-10-full-audit/07-protocols-misc.md) | none | none | none |  |

### XWayland lifecycle and integration

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-XW2-01 X11 socket access without explicit Xauthority: peer-policy validation pending](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-02 WL_SURFACE_ID resource-type and lifetime validation](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-03 X selection property format validation](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-04 Transient-parent property format / length validation](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-05 X selection forwarding lacks throttling / lock gating](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-06 Client-supplied X11 identity can match privileged window rules](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-07 Xdnd completion messages do not validate sender identity](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-08 XWayland restart cleanup can use a reset window manager](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-09 Unbounded X11 window bookkeeping and synchronous queries](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-10 Global X11 lock-file availability / ownership concerns](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |
| [F-XW2-11 Informational: additional XWayland robustness concerns](pending-evaluation/2026-10-full-audit/08-xwayland-chains.md) | none | none | none |  |

### Seat / clipboard / drag and drop

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-SEAT-01 set_selection 不校验请求客户端键盘焦点、serial 完全忽略 → 后台客户端剪贴板/中键粘贴劫持](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-02 wl_data_device.start_drag 无 serial/按钮/焦点校验 → 无按钮事件的注入式拖放](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-03 拖放结束依赖 target 客户端自愿 `offer.finish`，无超时/取消兜底 → dndActive 永久为真，全局丢弃按钮事件](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-04 wl_data_offer.receive 无一次性约束、无速率限制、core 路径未 accept 自动代接受 → 对 source 客户端的 fd/事件洪泛 DoS](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-05 OutputManagement set_custom_mode 无尺寸/刷新率上限，test 为 stub 恒真 → 巨幅 framebuffer OOM / 整数溢出](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-06 OutputManagement 无会话锁/权限门控 → 锁屏期间任意客户端禁用或重配所有显示器](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-07 create_configuration 不校验 done-serial、不限制并发配置、stop 后仍可操作](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-08 set_mode 不校验 mode 与 head 的归属 → 跨 head 模式注入](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-09 set_position 不校验布局合法性（重叠/越界）→ 监视器布局破坏](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-10 pointer.set_cursor 的 serial 校验被注释禁用 → 光标图像注入无输入合法性约束](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-11 光标 shm 像素数据拷贝无尺寸/格式校验；CPU 光标路径每帧原地翻转角色缓冲](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none | Flip-order correction: [side findings](agent-report/surface-protocols/_evidence/fork-fix-side-findings.md#f-seat-11-correction) |
| [SIDE-06 CPU cursor copy assumes the dumb buffer stride and size](agent-report/input/SIDE-06.md) | confirmed | none | none |  |
| [F-SEAT-12 触摸焦点记账是"单槽"模型：per-finger 状态共享一个槽位，合成 up 依赖会被覆盖的监听器](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-13 wl_data_device_manager.get_data_device 完全不使用 seat 参数；数据面无需 seat 即可建立](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-14 serial 存储记账缺陷：enterSerial 永不消费、serials 上限 100 可被挤掉 → serial 重放](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |
| [F-SEAT-15 X11↔Wayland 剪贴板桥与粘贴大小上限不在本域；本 fork 无 ClipboardManager](pending-evaluation/2026-10-full-audit/09-seat-clipboard.md) | none | none | none |  |

### Desktop / group / workspace lifecycle

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-DSK-01 Destroyed windows remain in groups as expired references](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-02 Group destruction can loop on expired members](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-03 Swallow matching can cross workspace / process trust boundaries](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-04 Swallow restore / toggle uses invalid workspace state](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-05 Backend listeners can outlive their window](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-06 Expired focus-history references remain reachable](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-07 Master-layout empty / zero-size arithmetic](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-08 Scrolling-layout index and size arithmetic](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-09 Scrolling fullscreen bookkeeping retains invalid targets](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-10 Duplicate layout targets / stale layout state](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-11 Negative work-area geometry reaches layouts](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-12 Pinned-window focus can use a missing monitor](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-13 Deferred window-map work can target a destroying window](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |
| [F-DSK-14 Group initialization / replacement boundary assumptions](pending-evaluation/2026-10-full-audit/10-desktop-lifecycle.md) | none | none | none |  |

### Resource loading

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-RES-01 PNG decoding lacks decoded-size limits](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-02 BMP parser arithmetic and buffer bounds](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-03 JPEG empty-input and error-path handling](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-04 WebP empty-input and error-path handling](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-05 AVIF resource cleanup and allocation checks](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-06 Async resource loading can lose a wakeup](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-07 Shared-memory image backing can produce SIGBUS](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-08 Cursor theme names / index files are not confined](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-09 Cursor loading symlink and retry behavior](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-10 Unreachable ICC upload path retains file descriptors](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-11 SVG external references and error-path cleanup](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |
| [F-RES-12 Control record: resource-loading negative results (not a defect)](pending-evaluation/2026-10-full-audit/11-resource-loading.md) | none | none | none |  |

### Startup / crash reporting

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-BOOT-01 Crash-report path creation lacks exclusive / symlink protections](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-02 Crash signal handler invokes PATH-resolved shell commands](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-03 Executable lookup trusts PATH entries and the working directory](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-04 Runtime-directory validation is too weak](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-05 Environment-toggle parsing accepts overly broad values](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-06 Systemd notification socket validation / format support](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-07 Data-file writes have path and replacement races](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-08 Session watchdog robustness](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-09 Core-dump visibility depends on system policy](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |
| [F-BOOT-10 Informational: startup / crash handling notes](pending-evaluation/2026-10-full-audit/12-bootchain-crash.md) | none | none | none |  |

### Input devices / keybinds

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-INP-01 Virtual keyboard events can reach execution keybinds](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-02 Release callbacks bypass keybind availability checks](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-03 Touch-device destruction listener ordering](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-04 Keyboard listeners use unchecked weak references](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-05 Shared keyboard pressed-key state can remain stuck](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-06 Virtual-pointer access and session-lock checks](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-07 Release callback recursion and device-removal reentrancy](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-08 CVirtualKeyboardV1Resource::destroy 双通道触发：destroy 事件重复发射、destroyKeyboard 重入](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-09 Keybinds::CResolver 键码缓存以裸指针为键，热插拔指针复用返回过期 keycode；resolveKeycode 数字解析未防溢出](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |
| [F-INP-10 Workspace-creation command rules accept client-provided names](pending-evaluation/2026-10-full-audit/13-input-keybinds.md) | none | none | none |  |

### Permissions / dialogs / tokens

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-PERM-01 Permission decisions trust dialog output and executable lookup](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-PERM-02 Spawned applications inherit non-CLOEXEC file descriptors](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-PERM-03 Dialog privileges are keyed only by process ID](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-PERM-04 Permission cache does not follow process lifetime](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-PERM-05 Informational: enforcement is disabled by default](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-TOK-01 Control record: token-manager checks and low-risk notes](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-RULE-02 Execution-token rules trust client-supplied environment](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-RULE-03 Execution-token matching performs synchronous process-environment reads](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-PERM-06 Permission-dialog text is not escaped](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |
| [F-PERM-07 Missing / invalid dialog results can allow access](pending-evaluation/2026-10-full-audit/14-permission-dialogs-tokens.md) | none | none | none |  |

### Event-loop timers

| Issue | Agent confirmed | Manually confirmed | PR status | Notes |
|---|---|---|---|---|
| [F-TMR-01 Screenshare stop timer is not unregistered](pending-evaluation/2026-10-full-audit/15-eventloop-timers.md) | none | none | none |  |

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
