# 安全审计 20 — 输出管理非 CM 面（驱动/电源/模式/缓冲生命周期）

- 仓库：/home/wilf/data/Hyprland fork，基线 main @ e38076ac9
- 审计范围：`src/output/Monitor.cpp` 非色彩管理部分（模式应用/commit 回滚/DPMS/VRR/连接-断开生命周期）、`MonitorResources.cpp`、`WorkBufferPool.cpp`、`OutputCommitCoordinator.cpp`、`MonitorFrameScheduler.cpp`、`DamageRing.cpp`，以及 DPMS 的协议入口 `src/protocols/OutputPower.cpp`、`src/config/shared/monitor/MonitorRuleManager.cpp::ensureVRR`、`src/managers/SessionLockManager.cpp` 与 `src/protocols/core/Output.cpp` 的交互面
- 威胁模型：恶意 Wayland 客户端（bind `zwlr_output_power_v1`/`zwlr_output_management_v1`/任意客户端）触发输出重配竞态、DPMS 风暴、与锁屏交互、测试提交失败后的状态残留
- 去重说明：Monitor.cpp 色彩管理/HDR 面（F-CM-*，01 域）与 output-management **协议校验面**（F-SEAT-05..09，09 域）不在此重复；本档案全部为**实现层**缺陷。F-SEAT 系列是本域攻击链的"入口放大器"，但即使协议面修复，以下实现层状态机缺陷依旧独立存在。
- 已排查未发现问题的面（覆盖记录）：`DamageRing`（事务 commit/rollback 语义、age 环正确）、`WorkBufferPool`（数值校验、上限回收正确，主池 byteLimit=UINT64_MAX 但条目数封顶 8 且尺寸钉死为 monitor 尺寸，见 MonitorResources.cpp:31 —— 可接受）、`MonitorFrameScheduler`（弱引用防护正确，`render:new_render_scheduling`/`render:async_commit` 默认 false）、`MonitorZoomController`（无攻击者面）、fadeout（`CFadingOutState::cleanupForMonitor` 持有 WP 过期即回收，正确）、DRMLease（connector 资源监听 destroyMon 清理，正确；offer 仅 non-desktop）。

---

## F-OUT-01 ｜DPMS 提交重试一次性失败后永久去同步，无对账路径 — 高

**位置**
- `src/output/Monitor.cpp:2379-2413`（`commitDPMSState`）：首次 `m_state.commit()` 失败 → 建 `m_dpmsRetryTimer` 延迟 ~2 帧重试**一次**；重试再失败时 `:2398-2401` 仅记日志 `return`，既不再重试、也不清 `m_dpmsRetryTimer`。
- `src/output/Monitor.cpp:2347-2350`（`setDPMS`）：`if (m_dpmsStatus == on) return;` —— 一旦 `m_dpmsStatus` 与硬件实际 enabled 状态去同步，**协议与任何 API 都无法再触发对该目标的重新提交**（值未变化 → 全程 no-op）。
- `src/output/Monitor.cpp:2824-2827` + `src/output/OutputCommitCoordinator.cpp:48-54`：`CMonitorState::commit()` 在有异步提交挂起时 `deferStateCommit()` **返回 true 伪装成功** → `commitDPMSState` 认为成功、跳过整个重试机制；flush 时（`OutputCommitCoordinator.cpp:252-261`）若真提交失败，只做 `damageEntire + scheduleRecovery`，**没有任何 DPMS 层重试**。`m_stateCommitPending` 无超时。
- 信任 `m_dpmsStatus` 的下游：`src/managers/SessionLockManager.cpp:103`（NOACTIVEMONS → 立即 `sendLocked` 不渲染任何锁屏帧）、`:169`（锁帧完成判定）、`src/pointer/PointerManager.cpp:383/416`（指针进入/光标渲染跳过该 monitor）、`src/ipc/s1/Commands.cpp:289/310`、`src/protocols/OutputPower.cpp:20/30`（回发给所有 power 客户端）。

**攻击路径**
恶意客户端（自由 bind `zwlr_output_power_v1`，无权限门）对目标输出高频交替 `set_mode(ON)/set_mode(OFF)`。代码注释自证（`Monitor.cpp:2386-2387`："could happen when the DRM backend rejects our commit because disable + enable were sent almost instantly"）：快速 disable+enable 会被后端拒绝。两次连续拒绝（风暴中常见）即产生 `m_dpmsStatus=false 但输出实际 enabled`（或反之）的永久去同步。后果：
1. 电源 DoS——"关屏"请求后屏幕持续点亮（反向：亮屏请求后黑屏，`setDPMS(true)` 被早退吞掉，无法唤醒）；
2. 与锁屏交互——攻击者可让全部 monitor 的 `m_dpmsStatus=false`（真实硬件仍亮），随后屏幕锁定器触发锁屏时 `NOACTIVEMONS=true` → 合成器**立刻 `sendLocked` 而从不渲染锁屏帧**（锁屏客户端从未获得机会画任何锁帧；虽然渲染器 lockdead 兜底 `Renderer.cpp:1175-1178` 会遮住内容，但锁屏客户端的 "locked 前必须渲染一帧" 安全契约被绕过，锁屏若依赖 locked 事件后才遮罩自身敏感 UI 即被破坏）；
3. 指针封锁——被谎报为 off 的可见输出上光标不可达（PointerManager 早退）。
（开启 `render:async_commit` 且后端支持 QUEUED 时后果加重：整个 DPMS 失败检测被 defer 假成功旁路。）

**PoC 思路**
libwayland 脚本：bind `zwlr_output_power_manager_v1` → 对每个 `zwlr_output_power_v1` 以 5-20ms 交替发 `set_mode(0/1)` 持续 10s。日志出现 `Couldn't commit output ... for DPMS`、`Couldn't retry committing ...`（后者即永久去同步信号）。随后执行 `hyprctl dispatch dpms on`（`ConfigActions.cpp:1288` toggle 依据谎报值算出 enable=false）无法恢复；只有恰好把 `m_dpmsStatus` 扳到相反值再扳回才恢复。第二幕：去同步后用屏幕锁定器锁屏，验证 lockscreen 客户端未收到任何锁帧 commit 即收到 locked。

**修复建议**
- `commitDPMSState` 引入 `m_committedDpmsState`，重试改为带指数退避的循环直到与 `m_dpmsStatus` 对齐；`setDPMS` 早退条件改为 `m_dpmsStatus == on && m_committedDpmsState == on`。
- `deferStateCommit()` 返回语义拆分：对策略型提交（DPMS/模式/VRR）不允许假成功，或 flush 失败路径调用 `commitDPMSState` 的同一重试机制；`m_stateCommitPending` 加超时对账。
- SessionLockManager/PointerManager 改查询 `m_output->state->state().enabled`（实际状态）而非裸 `m_dpmsStatus`。

---

## F-OUT-02 ｜m_dpmsRetryTimer 在断开/销毁时不取消：向已断开输出重新上电，或空 m_output 解引用崩溃 — 高

**位置**
- `src/output/Monitor.cpp:2389-2406`：`m_dpmsRetryTimer` 回调捕获 `[this, self=m_self]`，仅判 `self` 存活，**不判 `m_output`**，回调内直接 `m_output->state->resetExplicitFences(); ... m_state.commit()`。
- `src/output/Monitor.cpp:390-392`（`onDisconnect` 开头）：只 `m_frameScheduler.reset()`、`m_commitCoordinator->cancelPending()`、`clearModeRetry()`——**不取消 `m_dpmsRetryTimer`**（grep 证实该 timer 全文件仅 2389-2406 出现，断开路径无解除）。
- `src/output/Monitor.cpp:222-229`（destroy 监听）：`onDisconnect(true)` 之后 `m_output = nullptr; State::monitorState()->remove(...)`。
- 对比正确姿势：`scheduleModeRetry` 回调自带 `if (!PMONITOR->m_output || !PMONITOR->m_enabled) return;`（`:1103`）。

**攻击路径**
两阶段，全部由恶意客户端 + 常规用户事件武装：
1. 客户端用 F-OUT-01 的 DPMS 风暴使某次 `commitDPMSState` 失败、挂起 33ms（2000/60Hz）量级的重试定时器；
2a. 同一事件循环轮内该 monitor 走完全销毁（`m_listeners.destroy` → `m_output=nullptr`）而 CMonitor 对象仍被外部引用暂存（fadeout/延后回调等持有 SP/WP）→ 定时器触发，`self` 仍可 lock、`m_output` 已空 → **空指针解引用 SEGV，合成器整机崩溃**（整机 DoS）。
2b. 更稳的半程变体：断开（未销毁，`MonitorRuleManager.cpp:186` 规则禁用路径）→ `onDisconnect` 已在 `:463-470` 提交 `setEnabled(false)` → 挂起的重试定时器随后 `setEnabled(m_dpmsStatus)` + commit → **已断开/已禁用的输出在 DRM 层被重新上电**，且 `m_enabled=false` 使合成器不再为其渲染/上报（wl_output 不存在、IPC 显示 disabled），黑亮屏 + 功耗绕过。
附带：重试定时器触发后再次失败时 `m_dpmsRetryTimer` 指针不清（`:2398-2401` 提前 return），下一次风暴直接 `makeShared` 覆盖旧 SP——旧定时器仍挂在 EventLoopManager 里排队，形成多重僵尸提交回调。

**PoC 思路**
脚本 A（2b）：对目标 monitor output-power 风暴 → 命中一次提交失败（日志 `will retry.`）→ 立即经 wlr-output-management `disable_head`+`apply`（F-SEAT-06 面，锁屏下亦可）→ 用 `drm_info`/`modetest` 观察该 connector CRTC 在断开后仍为 ACTIVE。脚本 B（2a）：风暴武装定时器 + 触发虚拟（wayland backend）输出销毁；对物理热插拔场景则在拔线瞬间风暴。

**修复建议**
`onDisconnect` 顶部与 destroy 监听中 `m_dpmsRetryTimer->cancel(); g_pEventLoopManager->removeTimer(...); reset()`（与 `clearModeRetry` 同构）；重试回调加 `if (!m_output) return;`；重试失败路径把定时器置空或继续排重试。

---

## F-OUT-03 ｜applyMonitorRule 全模式失败后残留被拒 pending 模式/swapchain/format，m_currentMode 置空且不回滚——后续任意客户端提交"实体化"垃圾模式 — 高

**位置**
- `src/output/Monitor.cpp:854-855`：测试循环**前**即清空 `m_customDrmMode = {}`、`m_currentMode = nullptr`；
- `:874-913`、`:916-934`、`:937-958`：每一轮 `m_state.applyModeWithSwapchain / applyCustomModeWithSwapchain`（`Monitor.cpp:2870-2878`）先把 mode/customMode 写进 **pending state** 并把 swapchain `reconfigure` 到该尺寸（`updateSwapchain` `:2848-2869`，3 张 scanout buffer 实际分配），再 `m_state.test()`；test 失败仅 `continue`——**pending 不回滚**。
- `:960-965`：全部失败 → `m_activeMonitorRule` 记录新规则、`scheduleModeRetry()`、`return true`——无回滚、无禁用输出、且**跳过 `:1078 m_events.modeChanged.emit()`**（OutputPower/wl_output/head 端停留在旧 current mode 认知）。
- `:1083-1111`：重试封顶 3 次后永久放弃 → 稳态：monitor 硬件跑旧模式，`m_currentMode==nullptr`、`m_customDrmMode=={}`（自定义模式被抹掉）、pending 里躺着最后一个被 test 拒绝的 mode（swapchain 也钉在该尺寸）；`OutputManagement.cpp:202-206` 对 null `m_currentMode` 走 "sending current mode ... null (fake)" 分支。
- `:2966-2981`（`trySetFormat`）同族缺陷：两种格式都 test 失败时 `m_drmFormat` 停留在最后失败的格式、`m_prevDrmFormat` 不回滚。
- 触发入口（实现层）：wlr-output-management apply → `OutputManagement.cpp:351-435`（`newState.enabled = true` 强制启用 + `scheduleReload()`）→ `MonitorRuleManager.cpp:170` `applyMonitorRule`；custom mode 尺寸本身无上限属 F-SEAT-05，此处是其**实现层持久化放大器**。

**攻击路径**
恶意客户端经 output-management 提交一个恰好使 test 全拒的规则（超大自定义 modeline 内核拒；或重配风暴中 CRTC 资源竞争窗口使标准模式 test 也瞬时全拒）→ 落入 3 次重试后放弃的僵尸稳态。此后**任意客户端可触发的普通提交路径**——output-power `set_mode`→`commitDPMSState`（Monitor.cpp:2383）、全屏切换→`ensureVRR` 的 `m_state.commit()`（MonitorRuleManager.cpp:211/235/244）、断开清理 `onDisconnect:468`——都会把 pending 中的被拒模式随 `m_output->commit()` 真正下发：要么 modeset 到从未通过 test 的垃圾模式（撕裂/错分辨率，配合缩放计算 `:992` 的整数性校验被绕过产生分数像素路径），要么该模式永久被内核拒绝 → **每帧提交失败**：`failed()`→`damageEntire`→`scheduleRecovery`→`scheduleFrame` 循环使该输出永久黑屏并空烧 GPU（OutputCommitCoordinator.cpp:219-250）。

**PoC 思路**
`wlr-randr --output eDP-1 --mode 3840x2160@400`（构造 test 必拒）；确认日志 `REJECTED ... mode` ×N、`NO FALLBACK MODES`；然后对同 monitor 连发 output-power `set_mode OFF/ON`，用 `modetest -c` 观察 CRTC 属性被改成被拒模式或持续 modeset 失败刷屏；`hyprctl monitors` 中该 monitor 的 mode 与实际不符。

**修复建议**
测试循环开始前快照完整 state（mode/customMode/format/enabled/swapchain options），全失败路径整体恢复快照 + 恢复旧 `m_currentMode/m_customDrmMode/m_drmFormat` + `m_events.modeChanged.emit()`；`updateSwapchain` 仅在 test 成功后才 `reconfigure`（或失败后立即 reconfigure 回旧尺寸）；重试次数耗尽后把输出置为禁用态而非带僵尸 pending。

---

## F-OUT-04 ｜disable→enable 周期不重置 DPMS 动画状态：enabled 但永久黑屏 + 光标不可达的 monitor — 中

**位置**
- `src/output/Monitor.cpp:117-378`（`onConnect`）：`m_enabled = true; setEnabled(true); m_state.commit();` 全程**不触碰 `m_dpmsStatus`、`m_dpmsBlackOpacity`、`m_pendingDpmsAnimation`**；`m_dpmsStatus` 唯一写入者是 `setDPMS`（`:2352`）。
- `:2347-2376`：OFF 路径把 `m_dpmsBlackOpacity` 动画到 1 后提交 DPMS；只有 ON 路径的 `m_pendingDpmsAnimation` + 5 次 present 才把黑遮罩清回 0（`:166-181` presented 监听）。
- `src/render/Renderer.cpp:2358-2362`：`m_dpmsBlackOpacity != 0` 时每帧叠加全黑遮罩。
- `src/pointer/PointerManager.cpp:383/416`、`src/managers/SessionLockManager.cpp:103/169`：把 `m_dpmsStatus=false` 的 monitor 按"不存在"处理。

**攻击路径**
单客户端完整链（无需物理操作）：
1. 经 `zwlr_output_power_v1` 合法地把目标 monitor `set_mode(OFF)` → `m_dpmsStatus=false`，黑遮罩 opacity=1，DPMS commit 成功；
2. 经 `zwlr_output_management_v1`（F-SEAT-06 面：无锁屏/权限门控）对该 head `disable_head` + apply → `MonitorRuleManager.cpp:186` `onDisconnect()`（此时 m_enabled=true 走完整路径，`m_enabled=false`）；
3. 再 apply 一个 enable 配置（`applyTestConfiguration` 无条件 `newState.enabled=true`，OutputManagement.cpp:381）→ 规则 re-enable → `onConnect(true)`：`setEnabled(true)` + commit，硬件点亮，但 `m_dpmsStatus` 仍 false、opacity 仍 1 且**永远不会被清**（黑遮罩每帧绘制，presented 里的 5 帧动画计数只在 `m_pendingDpmsAnimation=true` 时归零，而该标志从未置位）。
结果：输出上电显示纯黑，光标无法进入（pointer 按 DPMS-off 跳过），锁屏 NOACTIVEMONS 把它计为"关"，IPC/`hyprctl monitors` 显示 enabled 正常——用户无法从任何 UI 察觉，也无法用一次 `dpmstoggle` 恢复（toggle 依据 `m_dpmsStatus=false` 给出 true 可恢复，但攻击者持有 output-power 对象可再次 OFF 形成循环压制）。

**PoC 思路**
weston 风格脚本：output_power OFF eDP-1 → 用 wlr-output-management 配置对象 disable+enable head eDP-1 → 屏幕全黑但 `hyprctl monitors` 显示 enabled、`getinputs` 光标进不去。

**修复建议**
`onConnect` 中：`if (!m_dpmsStatus || m_dpmsBlackOpacity->value() != 0.f) setDPMS(true);`（或显式 `m_dpmsStatus=true; m_dpmsBlackOpacity->setValueAndWarp(0.f); m_events.dpmsChanged.emit();`）；`m_enabled` 与 `m_dpmsStatus` 的不变式应在 enable 提交点统一维护。

---

## F-OUT-05 ｜OutputPower：可向"已禁用"输出强制上电 + 构造器空 monitor 解引用崩溃；无锁屏/速率门控 — 中

**位置**
- `src/protocols/OutputPower.cpp:13-18`：`set_mode` 直通 `setDPMS`，无 `m_enabled`、无会话锁、无速率限制检查；
- `src/output/Monitor.cpp:2379-2413`：`commitDPMSState` 不检查 `m_enabled`/`m_output`，对禁用输出照样 `setEnabled(true)+commit`；
- `src/protocols/OutputPower.cpp:59-69`：`onGetOutputPower` 把 `OUTPUT->m_monitor.lock()`（**可为 null**）直接传入构造函数；构造函数 `:20/:30-32` 无条件解引用 `m_monitor`（`:14` 的判空只保护了 set_mode 回调）。
- 陈旧 wl_output 可达性：`src/protocols/core/Output.cpp:118-130/149-155`——monitor 移除只把协议对象标 `defunct` 并撤 global，**客户端已 bind 的 `wl_output` 资源一直存活**直到客户端自行 release/destroy；`m_monitor` 是 WP，monitor 销毁后过期 → `fromResource` 仍返回该 `CWLOutputResource`。

**攻击路径**
(a) **策略绕过**：用户/规则/管理协议把某 monitor 禁用（笔电副屏、锁屏策略）后，任意客户端对其旧 `zwlr_output_power_v1` 对象发 `set_mode(ON)` → DRM 层重新上电：合成器按 disabled 不渲染、无 wl_output、`ensureBufferPresent`（`Monitor.cpp:2805-2822`）随手 attach 一张 swapchain buffer → 输出显示未定义内容/纯黑但**耗电点亮**，且 `m_dpmsStatus` 被翻转为 true 污染下游判定（F-OUT-01 同族）。反向 OFF 同礼。
(b) **崩溃**：恶意客户端 bind output_power，对每个 `wl_output` 建立 power 对象并保留旧代理；一次 monitor 移除（物理拔线、head 销毁——攻击者可持续向 output-power `get_output_power` 循环重试，把崩溃窗口武装到下一次任何移除事件）后，用**陈旧 `wl_output`** 发 `get_output_power` → `OUTPUT->m_monitor.lock()==null` → 构造函数 `m_monitor->m_dpmsStatus` 空解引用 → 合成器 SEGV（整机 DoS）。

**PoC 思路**
(a)：`hyprctl` 或规则禁用 DP-2 → 客户端对 DP-2 的 power 对象 set_mode ON → `cat /sys/class/drm/.../status`、功率计/`modetest` 验证 CRTC 激活。(b)：循环 `wl_registry` 中历史 output 代理调 `zwlr_output_power_manager_v1.get_output_power`，同时另一终端拔插显示器；gdb 断 `COutputPower::COutputPower` 可验证 `pMonitor_==null`。

**修复建议**
构造函数：`if (!pMonitor_) { 资源 sendFailed 并返回; }`，`:30-32` 监听注册置于判空后；`setSetMode` 增加 `if (!m_monitor->m_enabled) { post no-op/ignore }`；`setDPMS` 顶部 `if (!m_enabled) return;`；对 `set_mode` 加每客户端速率限制；锁屏期间只允许把状态朝"更省电"方向变化（或由 lockscreen 独占 power 控制权）。

---

## F-OUT-06 ｜ensureVRR 的 m_vrrActive 无条件翻转：test 被拒/commit 失败仍置位，2/3 档禁用不提交——VRR 状态机永久说谎 — 中

**位置**
- `src/config/shared/monitor/MonitorRuleManager.cpp:226-238`（USEVRR==1）：`:230` `m_state.test()` 失败会把 pending adaptiveSync 改回 false（`:232`），`:235` commit 失败仅记日志，但 **`:238 m->m_vrrActive = true;` 无条件执行**。
- `:206-214`（USEVRR==0）：`:211` 禁用 commit 失败仍 `:214 m_vrrActive = false`。
- `:250-276`（USEVRR==2/3）：else 分支 `:274-276` 只 `setAdaptiveSync(false)` **无 commit**——空闲无 damage 的 monitor 上硬件 VRR 保持开启（与 `m_vrrActive=false` 去同步）；want 分支同样 test 后不 commit 即置 `:263 m_vrrActive = true`。
- `m_state.commit()` 在此路径还叠加 F-OUT-01 的 `deferStateCommit` 假成功面（`OutputCommitCoordinator.cpp:48-54`）。
- 消费方：`src/protocols/OutputManagement.cpp:148/193`（head 的 `adaptive_sync` 状态广播——被管理客户端据此决策）、`src/output/Monitor.cpp:2118`/`OutputCommitCoordinator.cpp:119`（presentation 时间戳按 `vrr` 打标，`canSubmitAsync` `:72` 用 `frame.vrr` 做能力门控）、`src/ipc/s1/Commands.cpp:289/310`。

**攻击路径**
任意客户端可触发 `ensureVRR` 重估：反复全屏/退全屏自己的窗口（`Monitor.cpp:1471/1479` fullscreen fade、workspace 切换 `:1474/1543/1658` 都调 `ensureVRR`），配合 F-OUT-03 的僵尸 pending 模式或 DPMS 风暴（F-OUT-01）制造 commit 失败/挂起 → `m_vrrActive` 与硬件 adaptive_sync 去同步。此后 USEVRR==1 的启用分支被 `if (!m->m_vrrActive)`（`:226`）短路**永不再真正提交启用**，禁用分支同理（`if (m->m_vrrActive)` `:240`）：状态机卡死在谎言上；管理协议向所有配置的客户端持续广播错误的 VRR 状态；presentation/tearing 打标错误还会连带影响 commit-timing 类客户端（如游戏帧队列）。

**PoC 思路**
开 `misc:vrr=1`；脚本交替 (i) 客户端 A 每 50ms 全屏↔还原，(ii) 客户端 B output-power 风暴制造提交失败；30s 后核对 `hyprctl monitors all` 的 `vrr:` 字段 vs `cat /sys/kernel/debug/dri/*/*-crtc-*/vrr_enabled`（或 `drm_info`）——出现 vrr:true 而 debugfs false（或反之）即实锤；`wlr-output-management` dump head 的 adaptive_sync 同样说谎。

**修复建议**
`m_vrrActive` 只在**提交成功**（含 async flush 成功回调）后置位；以 `m_output->state->state().adaptiveSync` 为权威做对账（commit 前先比对）；USEVRR==2/3 的 disable 分支补 `resetExplicitFences + m_state.commit()`；commit 失败路径不修改 `m_vrrActive` 并安排重试（与 F-OUT-01 的重试框架共用）。

---

### 与其他域的关系

| 本域发现 | 依赖/放大的其他域发现 | 独立性 |
|---|---|---|
| F-OUT-01 | 入口是自由绑定的 output-power（09 域 F-SEAT-06 只覆盖 output-management 的门控缺失） | 提交/重试机制缺陷独立成立 |
| F-OUT-02 | 武装需 F-OUT-01 | timer 生命周期缺陷独立（模式提交失败也会武装同源回调） |
| F-OUT-03 | 尺寸无界是 F-SEAT-05；此处是失败后残留 | pending 不回滚为纯实现层缺陷 |
| F-OUT-04 | 触发用 F-SEAT-06 的 disable/apply | 即使协议门控修好，用户主动 disable/enable + 屏保 DPMS 也会自发命中 |
| F-OUT-05 | 无 | 纯实现层（构造判空 + m_enabled 检查缺失） |
| F-OUT-06 | 可被 F-OUT-01/03 加重 | 逻辑本身独立错误 |
